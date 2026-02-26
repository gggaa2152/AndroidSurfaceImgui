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
#include <map>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态定义
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

struct Config {
    bool predictEnemy = false;
    bool predictHex = false;
    bool espBoard = true;
    bool espBench = false; 
    bool espShop = false;  
    bool autoBuy = false;
    bool fastExit = false;
    
    float menuX = 100.0f, menuY = 100.0f;
    float menuW = 380.0f, menuH = 600.0f;
    float menuScale = 1.0f;
    
    float boardStartX = 400.0f, boardStartY = 400.0f;
    float boardScale = 1.0f;
} g_Cfg;

float g_autoScale = 1.0f;        
float g_fontRenderedSize = 0.0f; 
bool g_needUpdateFont = false;
float g_anims[20] = {0.0f}; // 存储各UI组件动画状态

GLuint g_heroTex = 0;           
bool g_texLoaded = false; 

// 模拟敌方阵容分布
int g_enemyGrid[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 持久化管理
// =================================================================
void SaveSettings() {
    std::ofstream out(g_configPath);
    if (!out.is_open()) return;
    out << "p_enemy=" << g_Cfg.predictEnemy << "\n";
    out << "p_hex=" << g_Cfg.predictHex << "\n";
    out << "e_board=" << g_Cfg.espBoard << "\n";
    out << "m_x=" << g_Cfg.menuX << "\n";
    out << "m_y=" << g_Cfg.menuY << "\n";
    out << "m_scale=" << g_Cfg.menuScale << "\n";
    out << "b_x=" << g_Cfg.boardStartX << "\n";
    out << "b_y=" << g_Cfg.boardStartY << "\n";
    out << "b_scale=" << g_Cfg.boardScale << "\n";
    out.close();
}

void LoadSettings() {
    std::ifstream in(g_configPath);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos), v = line.substr(pos + 1);
        try {
            if (k == "p_enemy") g_Cfg.predictEnemy = (v == "1");
            else if (k == "m_x") g_Cfg.menuX = std::stof(v);
            else if (k == "m_y") g_Cfg.menuY = std::stof(v);
            else if (k == "m_scale") g_Cfg.menuScale = std::stof(v);
            else if (k == "b_x") g_Cfg.boardStartX = std::stof(v);
            else if (k == "b_y") g_Cfg.boardStartY = std::stof(v);
            else if (k == "b_scale") g_Cfg.boardScale = std::stof(v);
        } catch (...) {}
    }
    g_needUpdateFont = true;
}

// =================================================================
// 3. 渲染引擎：Hex Shader & Texture
// =================================================================
class HexRenderer {
public:
    GLuint program = 0;
    GLint uRes = -1;
    
    void Init() {
        const char* vs = "#version 300 es\n"
                         "layout(location=0) in vec2 Position;\n"
                         "layout(location=1) in vec2 UV;\n"
                         "out vec2 v_uv;\n"
                         "uniform vec2 u_res;\n"
                         "void main() {\n"
                         "    v_uv = UV;\n"
                         "    vec2 ndc = (Position / u_res) * 2.0 - 1.0;\n"
                         "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
                         "}";
        const char* fs = "#version 300 es\n"
                         "precision mediump float;\n"
                         "uniform sampler2D u_tex;\n"
                         "in vec2 v_uv;\n"
                         "out vec4 o_color;\n"
                         "float sdHex(vec2 p, float r) {\n"
                         "    vec3 k = vec3(-0.866, 0.5, 0.577);\n"
                         "    p = abs(p); p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;\n"
                         "    p -= vec2(clamp(p.x, -k.z*r, k.z*r), r);\n"
                         "    return length(p)*sign(p.y);\n"
                         "}\n"
                         "void main() {\n"
                         "    vec2 p = (v_uv - 0.5) * 2.0;\n"
                         "    float d = sdHex(vec2(p.y, p.x), 0.95);\n"
                         "    float edge = 1.0 - smoothstep(-0.03, 0.01, d);\n"
                         "    o_color = texture(u_tex, v_uv) * edge;\n"
                         "    if(o_color.a < 0.1) discard;\n"
                         "}";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        uRes = glGetUniformLocation(program, "u_res");
    }
} g_HexRender;

GLuint LoadTex(const char* path) {
    int w, h, n;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return 0;
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return id;
}

// =================================================================
// 4. UI 绘制逻辑
// =================================================================
void UpdateHDText(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    g_autoScale = io.DisplaySize.y / 1080.0f;
    float target = std::clamp(18.0f * g_autoScale * g_Cfg.menuScale, 12.0f, 80.0f);
    
    if (!force && std::abs(target - g_fontRenderedSize) < 0.2f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) != 0) fontPath = "/system/fonts/DroidSansFallback.ttf";
    
    io.Fonts->AddFontFromFileTTF(fontPath, target, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_fontRenderedSize = target;
}

void DrawHexHero(ImDrawList* dl, ImVec2 pos, float size) {
    if (!g_texLoaded) return;
    static bool inited = false; if(!inited) { g_HexRender.Init(); inited=true; }
    
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexRender.program);
        glUniform2f(g_HexRender.uRes, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    dl->AddImage((ImTextureID)(intptr_t)g_heroTex, pos - ImVec2(size, size), pos + ImVec2(size, size));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void RenderWorldESP() {
    if (!g_Cfg.espBoard) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    float baseCell = 40.0f * 2.2f * g_autoScale;
    float cellSz = baseCell * g_Cfg.boardScale;
    float xStep = cellSz * 1.732f;
    float yStep = cellSz * 1.5f;

    // 交互手柄计算 (放在棋盘右下角)
    ImVec2 handlePos = ImVec2(g_Cfg.boardStartX + 6.5f * xStep, g_Cfg.boardStartY + 3.5f * yStep);
    
    static bool dragging = false, scaling = false;
    static ImVec2 offset;

    // 交互逻辑
    if (ImGui::IsMouseClicked(0)) {
        if (ImLengthSqr(io.MousePos - handlePos) < 2000.0f) scaling = true;
        else if (ImRect(ImVec2(g_Cfg.boardStartX - cellSz, g_Cfg.boardStartY - cellSz), handlePos).Contains(io.MousePos)) {
            dragging = true; offset = io.MousePos - ImVec2(g_Cfg.boardStartX, g_Cfg.boardStartY);
        }
    }
    if (scaling && ImGui::IsMouseDown(0)) {
        g_Cfg.boardScale = std::max((io.MousePos.x - g_Cfg.boardStartX) / (baseCell * 7.0f), 0.3f);
    } else if (dragging && ImGui::IsMouseDown(0)) {
        g_Cfg.boardStartX = io.MousePos.x - offset.x; g_Cfg.boardStartY = io.MousePos.y - offset.y;
    } else if (scaling || dragging) {
        scaling = dragging = false; SaveSettings();
    }

    // 绘制棋盘
    float time = ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_Cfg.boardStartX + c * xStep + (r%2 ? xStep*0.5f : 0);
            float cy = g_Cfg.boardStartY + r * yStep;
            
            if (g_enemyGrid[r][c]) DrawHexHero(dl, ImVec2(cx, cy), cellSz * 0.92f);
            
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * M_PI / 180.0f;
                pts[i] = ImVec2(cx + cellSz * cosf(a), cy + cellSz * sinf(a));
            }
            dl->AddPolyline(pts, 6, IM_COL32(0, 255, 200, 180), ImDrawFlags_Closed, 2.5f);
        }
    }
    // 绘制缩放手柄指示
    dl->AddCircleFilled(handlePos, 15.0f * g_autoScale, IM_COL32(255, 200, 0, 230));
    dl->AddText(handlePos + ImVec2(20, -10), IM_COL32_WHITE, (const char*)u8"调整位置/大小");
}

// =================================================================
// 5. 现代 UI 控件
// =================================================================
bool CustomToggle(const char* label, bool* v, int id) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID im_id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float height = ImGui::GetFrameHeight() * 0.8f;
    float width = height * 2.2f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width + 10 + label_size.x, height));
    
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, im_id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, im_id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveSettings(); }

    g_anims[id] += ((*v ? 1.0f : 0.0f) - g_anims[id]) * 0.15f;
    
    ImU32 col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.2f, 0.25f, 1.0f), ImVec4(0.1f, 0.75f, 0.55f, 1.0f), g_anims[id]));
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(width, height), col_bg, height*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(height*0.5f + g_anims[id]*(width-height), height*0.5f), height*0.4f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + width + 8, bb.Min.y), label);
    return pressed;
}

void RenderMenu() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 12.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.95f);
    s.Colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.25f, 0.7f);

    ImGui::SetNextWindowPos(ImVec2(g_Cfg.menuX, g_Cfg.menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_Cfg.menuW, g_Cfg.menuH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v2.5", NULL, ImGuiWindowFlags_NoCollapse)) {
        g_Cfg.menuX = ImGui::GetWindowPos().x;
        g_Cfg.menuY = ImGui::GetWindowPos().y;
        
        float winW = ImGui::GetWindowWidth();
        if (ImGui::IsMouseReleased(0) && std::abs(winW - g_Cfg.menuW) > 10.0f) {
            g_Cfg.menuW = winW; g_Cfg.menuScale = winW / 380.0f;
            g_needUpdateFont = true; SaveSettings();
        }

        ImGui::SetWindowFontScale((18.0f * g_autoScale * g_Cfg.menuScale) / g_fontRenderedSize);

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), (const char*)u8"状态: 注入成功 | 帧率: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();

        if (ImGui::BeginChild("##Func", ImVec2(0, 0), true)) {
            if (ImGui::CollapsingHeader((const char*)u8" 战局辅助 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                CustomToggle((const char*)u8"对手预测 (智能)", &g_Cfg.predictEnemy, 1);
                CustomToggle((const char*)u8"海克斯强化分析", &g_Cfg.predictHex, 2);
                ImGui::Unindent();
            }
            
            if (ImGui::CollapsingHeader((const char*)u8" 视觉透视 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                CustomToggle((const char*)u8"显示敌方棋盘", &g_Cfg.espBoard, 3);
                CustomToggle((const char*)u8"备战席棋子详情", &g_Cfg.espBench, 4);
                CustomToggle((const char*)u8"商店刷新概率", &g_Cfg.espShop, 5);
                ImGui::Unindent();
            }

            ImGui::Separator();
            CustomToggle((const char*)u8"自动抢牌模式", &g_Cfg.autoBuy, 6);
            CustomToggle((const char*)u8"对局极速重连", &g_Cfg.fastExit, 7);

            ImGui::Spacing(); ImGui::Spacing();
            if (ImGui::Button((const char*)u8"手动保存当前布局", ImVec2(-1, 45 * g_autoScale))) SaveSettings();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// =================================================================
// 6. 系统入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    LoadSettings();
    UpdateHDText(true);

    static bool running = true;
    std::thread inputThread([&]{
        while(running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    });

    while (running) {
        if (g_needUpdateFont) { UpdateHDText(true); g_needUpdateFont = false; }
        
        imgui.BeginFrame();
        
        if (!g_texLoaded) {
            g_heroTex = LoadTex("/data/local/tmp/hero.png"); // 请确保路径正确或改为动态检测
            g_texLoaded = (g_heroTex != 0);
        }

        RenderWorldESP();
        RenderMenu();

        imgui.EndFrame();
        std::this_thread::yield();
    }

    running = false;
    if(inputThread.joinable()) inputThread.join();
    return 0;
}
