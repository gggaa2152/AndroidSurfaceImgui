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
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 2. 全局状态变量
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f;    
float g_startY = 400.0f;    

float g_menuX = 100.0f;
float g_menuY = 100.0f;
float g_menuW = 320.0f; 
float g_menuH = 500.0f; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 

bool g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 3. 配置管理
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n"
            << "predictHex=" << g_predict_hex << "\n"
            << "espBoard=" << g_esp_board << "\n"
            << "espBench=" << g_esp_bench << "\n"
            << "espShop=" << g_esp_shop << "\n"
            << "autoBuy=" << g_auto_buy << "\n"
            << "instant=" << g_instant << "\n"
            << "startX=" << g_startX << "\n"
            << "startY=" << g_startY << "\n"
            << "manualScale=" << g_boardManualScale << "\n"
            << "menuX=" << g_menuX << "\n"
            << "menuY=" << g_menuY << "\n"
            << "menuW=" << g_menuW << "\n"
            << "menuH=" << g_menuH << "\n"
            << "menuScale=" << g_scale << "\n";
        out.close();
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (!in.is_open()) return;
    
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string k = line.substr(0, pos), v = line.substr(pos + 1);
        try {
            if (k == "predictEnemy") g_predict_enemy = (v == "1");
            else if (k == "predictHex") g_predict_hex = (v == "1");
            else if (k == "espBoard") g_esp_board = (v == "1");
            else if (k == "espBench") g_esp_bench = (v == "1");
            else if (k == "espShop") g_esp_shop = (v == "1");
            else if (k == "autoBuy") g_auto_buy = (v == "1");
            else if (k == "instant") g_instant = (v == "1");
            else if (k == "startX") g_startX = std::stof(v);
            else if (k == "startY") g_startY = std::stof(v);
            else if (k == "manualScale") g_boardManualScale = std::stof(v);
            else if (k == "menuX") g_menuX = std::stof(v);
            else if (k == "menuY") g_menuY = std::stof(v);
            else if (k == "menuW") g_menuW = std::stof(v);
            else if (k == "menuH") g_menuH = std::stof(v);
            else if (k == "menuScale") g_scale = std::stof(v);
        } catch (...) {}
    }
    in.close();
    g_needUpdateFontSafe = true; 
}

// =================================================================
// 4. 渲染辅助
// =================================================================
class HexShader {
public:
    GLuint program = 0;
    GLint resLoc = -1;
    
    void Init() {
        const char* vs = "#version 300 es\n"
            "layout(location=0) in vec2 Position;\n"
            "layout(location=1) in vec2 UV;\n"
            "out vec2 Frag_UV;\n"
            "uniform vec2 u_Res;\n"
            "void main() {\n"
            "    Frag_UV = UV;\n"
            "    vec2 ndc = (Position / u_Res) * 2.0 - 1.0;\n"
            "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
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
            "    vec2 rotated_p = vec2(p.y, p.x);\n"
            "    float d = sdHex(rotated_p, 0.92);\n"
            "    float w = fwidth(d);\n"
            "    float m = 1.0 - smoothstep(-w, w, d);\n"
            "    vec4 tex = texture(Texture, Frag_UV);\n"
            "    if(m <= 0.0) discard;\n"
            "    Out_Color = tex * m;\n"
            "}";
            
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        
        glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexShader;

bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    
    GLuint tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, (void*)(intptr_t)g_heroTexture);
    
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, 
        center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    
    float targetSize = std::min(18.0f * g_autoScale * g_scale, 120.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 1;
    config.PixelSnapH = true;
    
    if (access("/system/fonts/SysSans-Hans-Regular.ttf", R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF("/system/fonts/SysSans-Hans-Regular.ttf", 
            targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 棋盘绘制
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f;
    float yStep = sz * 1.5f;
    
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    
    // 绘制缩放控制点
    float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + sz * 0.6f, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 240));
    
    // 交互
    static bool isDragging = false, isScaling = false;
    static ImVec2 dragOffset;
    
    if (ImGui::IsMouseClicked(0)) {
        ImRect hRect(p_top, p_ext);
        hRect.Expand(40.0f);
        
        if (hRect.Contains(io.MousePos)) {
            isScaling = true;
        } else if (ImRect(ImVec2(g_startX-sz, g_startY-sz), 
                   ImVec2(lastCX+sz, lastCY+sz)).Contains(io.MousePos)) {
            isDragging = true;
            dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    
    if (isScaling) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - g_startX;
            float baseW = (6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale;
            g_boardManualScale = std::max(curW / baseW, 0.1f);
        } else {
            isScaling = false;
            SaveConfig();
        }
    }
    
    if (isDragging && !isScaling) {
        if (ImGui::IsMouseDown(0)) {
            g_startX = io.MousePos.x - dragOffset.x;
            g_startY = io.MousePos.y - dragOffset.y;
        } else {
            isDragging = false;
            SaveConfig();
        }
    }
    
    // 绘制六边形网格
    float time = (float)ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            
            if(g_enemyBoard[r][c] && g_textureLoaded) 
                DrawHero(d, ImVec2(cx, cy), sz);
            
            // 彩色边框
            float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 255), 
                ImDrawFlags_Closed, 4.0f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 菜单UI (精简版)
// =================================================================
bool Toggle(const char* label, bool* v, int idx) {
    bool pressed = ImGui::Checkbox(label, v);
    if (pressed) SaveConfig();
    return pressed;
}

void DrawMenu() {
    static bool isScaling = false;
    static float startScale;
    static ImVec2 startMouse;
    
    ImGuiIO& io = ImGui::GetIO();
    float baseW = 320.0f * g_autoScale;
    float baseH = 500.0f * g_autoScale;
    
    ImGui::SetNextWindowSize(ImVec2(baseW * g_scale, 
        g_menuCollapsed ? ImGui::GetFrameHeight() : baseH * g_scale), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;
    if (g_menuCollapsed) flags |= ImGuiWindowFlags_NoScrollWithMouse;
    
    if (ImGui::Begin(u8"金铲铲助手", NULL, flags)) {
        // 标题栏点击切换折叠
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && 
            io.MousePos.y < g_menuY + ImGui::GetFrameHeight()) {
            g_menuCollapsed = !g_menuCollapsed;
            SaveConfig();
        }
        
        // 缩放控制（右下角）
        ImVec2 br = ImGui::GetWindowPos() + ImGui::GetWindowSize();
        float handleSize = 40.0f * g_autoScale * g_scale;
        
        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale(18.0f * g_autoScale * g_scale / g_current_rendered_size);
            
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader(u8"预测功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                Toggle(u8"预测对手分布", &g_predict_enemy, 1);
                Toggle(u8"海克斯强化预测", &g_predict_hex, 2);
            }
            
            if (ImGui::CollapsingHeader(u8"透视功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                Toggle(u8"对手棋盘透视", &g_esp_board, 3);
                Toggle(u8"对手备战席透视", &g_esp_bench, 4);
                Toggle(u8"对手商店透视", &g_esp_shop, 5);
            }
            
            ImGui::Separator();
            Toggle(u8"全自动拿牌", &g_auto_buy, 6);
            Toggle(u8"极速秒退助手", &g_instant, 7);
            
            if (ImGui::Button(u8"保存设置", ImVec2(-1, 45 * g_autoScale * g_scale))) {
                SaveConfig();
            }
            
            // 缩放手柄
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                br, br - ImVec2(handleSize, 0), br - ImVec2(0, handleSize), 
                IM_COL32(0, 120, 215, 200));
        }
        
        // 缩放交互
        ImRect scaleHandle(br - ImVec2(handleSize, handleSize), br);
        if (scaleHandle.Contains(io.MousePos) && ImGui::IsMouseClicked(0)) {
            isScaling = true;
            startScale = g_scale;
            startMouse = io.MousePos;
        }
        
        if (isScaling) {
            if (ImGui::IsMouseDown(0)) {
                float oldScale = g_scale;
                g_scale = std::clamp(startScale + (io.MousePos.x - startMouse.x) / baseW, 0.5f, 5.0f);
                g_menuX -= (baseW * g_scale - baseW * oldScale) * 0.5f;
                g_menuY -= (baseH * g_scale - baseH * oldScale) * 0.5f;
                g_needUpdateFontSafe = true;
            } else {
                isScaling = false;
                SaveConfig();
            }
        }
        
        // 窗口拖动
        if (!isScaling && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0)) {
            g_menuX += io.MouseDelta.x;
            g_menuY += io.MouseDelta.y;
            if (ImGui::IsMouseReleased(0)) SaveConfig();
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 程序入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    eglSwapInterval(eglGetCurrentDisplay(), 1);
    
    LoadConfig();
    UpdateFontHD(true);
    
    std::thread inputThread([&] {
        while (true) { imgui.ProcessInputEvent(); std::this_thread::yield(); }
    });
    
    while (true) {
        if (g_needUpdateFontSafe) {
            UpdateFontHD(true);
            g_needUpdateFontSafe = false;
        }
        
        imgui.BeginFrame();
        
        if (!g_resLoaded) {
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png");
            g_textureLoaded = (g_heroTexture != 0);
            g_resLoaded = true;
        }
        
        DrawBoard();
        DrawMenu();
        
        imgui.EndFrame();
        std::this_thread::yield();
    }
    
    inputThread.detach();
    return 0;
}
