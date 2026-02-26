#include <stdarg.h>
#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#include <thread>      
#include <cmath>       
#include <fstream>      
#include <string>
#include <vector>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态管理
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

struct Settings {
    bool predict_enemy = false;
    bool predict_hex = false;
    bool esp_board = true;
    bool esp_bench = false; 
    bool esp_shop = false;  
    bool auto_buy = false;
    bool instant = false;
    
    float startX = 400.0f;
    float startY = 400.0f;
    float boardManualScale = 1.0f;
    float menuScale = 1.0f;
} g_cfg;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 
float g_autoScale = 1.0f;        
float g_current_font_size = 0.0f; 

GLuint g_heroTexture = 0;           
bool g_resLoaded = false; 
bool g_needUpdateFont = false;

// 模拟数据：敌人棋盘布局
int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置序列化
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_cfg.predict_enemy << "\n"
            << "predictHex=" << g_cfg.predict_hex << "\n"
            << "espBoard=" << g_cfg.esp_board << "\n"
            << "startX=" << g_cfg.startX << "\n"
            << "startY=" << g_cfg.startY << "\n"
            << "manualScale=" << g_cfg.boardManualScale << "\n"
            << "menuScale=" << g_cfg.menuScale << "\n";
        out.close();
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue; 
            std::string k = line.substr(0, pos), v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy") g_cfg.predict_enemy = (v == "1");
                else if (k == "predictHex") g_cfg.predict_hex = (v == "1");
                else if (k == "espBoard") g_cfg.esp_board = (v == "1");
                else if (k == "startX") g_cfg.startX = std::stof(v);
                else if (k == "startY") g_cfg.startY = std::stof(v);
                else if (k == "manualScale") g_cfg.boardManualScale = std::stof(v);
                else if (k == "menuScale") g_cfg.menuScale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        g_needUpdateFont = true; 
    }
}

// =================================================================
// 3. 渲染引擎：Hex Shader & Texture
// =================================================================
struct HexRenderer {
    GLuint program = 0;
    GLint resLoc = -1;

    void Init() {
        if (program != 0) return;
        const char* vs = "#version 300 es\n"
                         "layout(location=0) in vec2 Position;\n"
                         "layout(location=1) in vec2 UV;\n"
                         "out vec2 Frag_UV;\n"
                         "uniform vec2 u_Res;\n"
                         "void main() {\n"
                         "    Frag_UV = UV;\n"
                         "    gl_Position = vec4((Position / u_Res) * 2.0 - 1.0, 0.0, 1.0);\n"
                         "    gl_Position.y *= -1.0;\n"
                         "}";
        const char* fs = "#version 300 es\n"
                         "precision mediump float;\n"
                         "uniform sampler2D Texture;\n"
                         "in vec2 Frag_UV;\n"
                         "out vec4 Out_Color;\n"
                         "float sdHex(vec2 p, float r) {\n"
                         "    vec3 k = vec3(-0.866025, 0.5, 0.57735);\n"
                         "    p = abs(p);\n"
                         "    p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;\n"
                         "    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);\n"
                         "    return length(p)*sign(p.y);\n"
                         "}\n"
                         "void main() {\n"
                         "    vec2 p = (Frag_UV - 0.5) * 2.0;\n"
                         "    float d = sdHex(vec2(p.y, p.x), 0.95);\n"
                         "    float alpha = 1.0 - smoothstep(-0.01, 0.01, d);\n"
                         "    if(alpha <= 0.0) discard;\n"
                         "    Out_Color = texture(Texture, Frag_UV) * alpha;\n"
                         "}";
        program = glCreateProgram();
        auto compile = [](GLenum type, const char* src) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, NULL);
            glCompileShader(s);
            return s;
        };
        GLuint v = compile(GL_VERTEX_SHADER, vs);
        GLuint f = compile(GL_FRAGMENT_SHADER, fs);
        glAttachShader(program, v); glAttachShader(program, f);
        glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexEngine;

GLuint LoadTexture(const char* path) {
    int w, h, c;
    unsigned char* data = stbi_load(path, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return tid;
}

// =================================================================
// 4. 核心绘制函数
// =================================================================
void DrawHexHero(ImDrawList* dl, ImVec2 center, float size, GLuint tex) {
    if (!tex) return;
    g_HexEngine.Init();
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexEngine.program);
        glUniform2f(g_HexEngine.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    dl->AddImage((ImTextureID)(intptr_t)tex, center - ImVec2(size, size), center + ImVec2(size, size));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void RenderChessBoard() {
    if (!g_cfg.esp_board) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    // 基础参数计算
    const float baseRad = 38.0f * 2.2f * g_autoScale;
    const float curRad = baseRad * g_cfg.boardManualScale;
    const float xStep = curRad * 1.73205f;
    const float yStep = curRad * 1.5f;

    // 手柄逻辑
    ImVec2 handlePos = ImVec2(g_cfg.startX + 6.5f * xStep, g_cfg.startY + 3.0f * yStep);
    static bool dragging = false, scaling = false;
    
    if (ImGui::IsMouseClicked(0)) {
        if (ImLengthSqr(io.MousePos - handlePos) < 2500.0f) scaling = true;
        else if (ImRect(ImVec2(g_cfg.startX - curRad, g_cfg.startY - curRad), handlePos).Contains(io.MousePos)) dragging = true;
    }
    if (ImGui::IsMouseReleased(0)) { dragging = scaling = false; SaveConfig(); }

    if (dragging) {
        g_cfg.startX += io.MouseDelta.x;
        g_cfg.startY += io.MouseDelta.y;
    }
    if (scaling) {
        float dist = io.MousePos.x - g_cfg.startX;
        g_cfg.boardManualScale = std::clamp(dist / (xStep * 6.5f / g_cfg.boardManualScale), 0.3f, 3.0f);
    }

    // 绘制棋盘
    float time = (float)ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_cfg.startX + c * xStep + (r % 2 ? xStep * 0.5f : 0);
            float cy = g_cfg.startY + r * yStep;
            
            // 英雄图标
            if(g_enemyBoard[r][c] && g_resLoaded) 
                DrawHexHero(dl, ImVec2(cx, cy), curRad * 0.96f, g_heroTexture);
            
            // 霓虹边框
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + curRad * cosf(a), cy + curRad * sinf(a));
            }
            ImU32 col = ImColor::HSV(fmodf(time * 0.2f + (cx+cy)*0.001f, 1.0f), 0.8f, 1.0f, 0.8f);
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, 2.5f * g_autoScale);
        }
    }
    // 绘制调节手柄
    dl->AddCircleFilled(handlePos, 10.0f * g_autoScale, IM_COL32(255, 255, 255, 150));
    dl->AddCircle(handlePos, (10.0f + sinf(time*5)*3.0f) * g_autoScale, IM_COL32(255, 200, 0, 200), 16, 2.0f);
}

// =================================================================
// 5. 交互 UI 模块
// =================================================================
bool CustomSwitch(const char* label, bool* v, int id) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID im_id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float height = ImGui::GetFrameHeight() * 0.8f;
    float width = height * 2.0f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width + 10 + label_size.x, height));
    
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, im_id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, im_id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }

    g_anim[id] = ImLerp(g_anim[id], *v ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime * 12.0f);

    ImU32 col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.2f, 0.25f, 1.0f), ImVec4(0.15f, 0.75f, 0.5f, 1.0f), g_anim[id]));
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(width, height), col_bg, height * 0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(height * 0.5f + g_anim[id] * (width - height), height * 0.5f), height * 0.4f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + width + 10, bb.Min.y), label);

    return pressed;
}

void UpdateGlobalFont() {
    ImGuiIO& io = ImGui::GetIO();
    g_autoScale = io.DisplaySize.y / 1080.0f;
    float targetSize = 18.0f * g_autoScale * g_cfg.menuScale;
    
    if (std::abs(targetSize - g_current_font_size) < 0.1f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x4e00, 0x9fa5, 0 };
    ImFontConfig font_cfg; font_cfg.PixelSnapH = true;
    
    if (access("/system/fonts/NotoSansCJK-Regular.ttc", R_OK) == 0)
        io.Fonts->AddFontFromFileTTF("/system/fonts/NotoSansCJK-Regular.ttc", targetSize, &font_cfg, ranges);
    else
        io.Fonts->AddFontDefault();

    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_font_size = targetSize;
}

void DrawMainUI() {
    ImGui::SetNextWindowSize(ImVec2(360 * g_autoScale * g_cfg.menuScale, 500 * g_autoScale * g_cfg.menuScale), ImGuiCond_FirstUseEver);
    
    ImGui::Begin((const char*)u8"JK-Chess Premium", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    
    // 自定义标题栏
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), (const char*)u8"JK-CHESS 助手 v2.5");
    ImGui::SameLine(ImGui::GetWindowWidth() - 60);
    if(ImGui::SmallButton(g_menuCollapsed ? "[+]" : "[-]")) g_menuCollapsed = !g_menuCollapsed;
    ImGui::Separator();

    if (!g_menuCollapsed) {
        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem((const char*)u8"视觉增强")) {
                CustomSwitch((const char*)u8"棋盘透视", &g_cfg.esp_board, 1);
                CustomSwitch((const char*)u8"对手预测", &g_cfg.predict_enemy, 2);
                CustomSwitch((const char*)u8"海克斯辅助", &g_cfg.predict_hex, 3);
                ImGui::SliderFloat((const char*)u8"菜单缩放", &g_cfg.menuScale, 0.8f, 2.0f, "%.1f");
                if (ImGui::IsItemDeactivated()) g_needUpdateFont = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem((const char*)u8"智能自动化")) {
                CustomSwitch((const char*)u8"极速自动拿牌", &g_cfg.auto_buy, 4);
                CustomSwitch((const char*)u8"一键速八退房", &g_cfg.instant, 5);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40 * g_autoScale);
        if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 30 * g_autoScale))) SaveConfig();
    }
    
    ImGui::End();
}

// =================================================================
// 6. 入口与循环
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    LoadConfig();
    UpdateGlobalFont();
    
    bool running = true;
    std::thread inputThread([&] { 
        while(running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } 
    });

    while (running) {
        if (g_needUpdateFont) { UpdateGlobalFont(); g_needUpdateFont = false; }
        
        imgui.BeginFrame(); 

        if (!g_resLoaded) {
            // 延迟加载纹理以确保 GL 上下文就绪
            g_heroTexture = LoadTexture("/data/local/tmp/hero.png"); 
            g_resLoaded = true;
        }

        RenderChessBoard();
        DrawMainUI();
        
        imgui.EndFrame();
    }
    
    running = false;
    if (inputThread.joinable()) inputThread.join();
    return 0;
}
