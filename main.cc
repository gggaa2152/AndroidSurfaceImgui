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
#include <chrono>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;
bool g_boardLocked = false; 

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
float g_menuX = 100.0f, g_menuY = 100.0f;
float g_menuW = 350.0f, g_menuH = 550.0f; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 

// 字体更新防抖变量
bool g_needUpdateFontSafe = false;
float g_fontUpdateTimer = -1.0f; 

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置管理
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << (g_predict_enemy ? "1" : "0") << "\n";
        out << "predictHex=" << (g_predict_hex ? "1" : "0") << "\n";
        out << "espBoard=" << (g_esp_board ? "1" : "0") << "\n";
        out << "espBench=" << (g_esp_bench ? "1" : "0") << "\n";
        out << "espShop=" << (g_esp_shop ? "1" : "0") << "\n";
        out << "autoBuy=" << (g_auto_buy ? "1" : "0") << "\n";
        out << "instant=" << (g_instant ? "1" : "0") << "\n";
        out << "boardLocked=" << (g_boardLocked ? "1" : "0") << "\n";
        out << "startX=" << g_startX << "\n";
        out << "startY=" << g_startY << "\n";
        out << "manualScale=" << g_boardManualScale << "\n";
        out << "menuX=" << g_menuX << "\n";
        out << "menuY=" << g_menuY << "\n";
        out << "menuW=" << g_menuW << "\n";
        out << "menuH=" << g_menuH << "\n";
        out << "menuScale=" << g_scale << "\n";
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
                if (k == "predictEnemy") g_predict_enemy = (v == "1");
                else if (k == "predictHex") g_predict_hex = (v == "1");
                else if (k == "espBoard") g_esp_board = (v == "1");
                else if (k == "espBench") g_esp_bench = (v == "1");
                else if (k == "espShop") g_esp_shop = (v == "1");
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "instant") g_instant = (v == "1");
                else if (k == "boardLocked") g_boardLocked = (v == "1");
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
}

// =================================================================
// 3. Hex Shader 与 纹理加载
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
                         "    float d = sdHex(vec2(p.y, p.x), 0.92);\n"
                         "    float alpha = 1.0 - smoothstep(-0.02, 0.02, d);\n"
                         "    if(alpha <= 0.0) discard;\n"
                         "    Out_Color = texture(Texture, Frag_UV) * alpha;\n"
                         "}";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
    void Cleanup() {
        if (program) { glDeleteProgram(program); program = 0; }
    }
} g_HexShader;
bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

// =================================================================
// 4. 字体与显示适配
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    
    float targetSize = std::clamp(18.0f * g_autoScale * g_scale, 12.0f, 100.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.1f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1; 
    config.PixelSnapH = true;
    
    const char* fonts[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/SourceHanSansCN-Regular.otf"
    };
    
    bool loaded = false;
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    
    for(const char* path : fonts) {
        if (access(path, R_OK) == 0) {
            if (io.Fonts->AddFontFromFileTTF(path, targetSize, &config, ranges)) {
                loaded = true; break;
            }
        }
    }
    
    if(!loaded) io.Fonts->AddFontDefault();

    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 棋盘绘制逻辑
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const float baseUnit = 38.0f * g_boardScale * g_autoScale;
    const float baseStepX = baseUnit * 1.73205f;
    const float baseStepY = baseUnit * 1.5f;

    float curUnit = baseUnit * g_boardManualScale;
    float curStepX = baseStepX * g_boardManualScale;
    float curStepY = baseStepY * g_boardManualScale;

    auto CalcHandlePos = [&](float startX, float startY, float scale) -> ImVec2 {
        float sX = baseStepX * scale;
        float sY = baseStepY * scale;
        float u  = baseUnit * scale;
        float cx = startX + 6 * sX + (3 % 2 == 1 ? sX * 0.5f : 0);
        float cy = startY + 3 * sY;
        return ImVec2(cx + u, cy + u * 0.5f);
    };

    ImVec2 p_handle = CalcHandlePos(g_startX, g_startY, g_boardManualScale);

    if (!g_boardLocked) {
        static bool isScaling = false;
        static bool isDragging = false;
        static ImVec2 dragOffset;

        if (!ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseClicked(0)) {
                float distSq = ImLengthSqr(io.MousePos - p_handle);
                if (distSq < (6000.0f * g_autoScale)) {
                    isScaling = true;
                    dragOffset = io.MousePos - p_handle; 
                } 
                else {
                    ImRect area(ImVec2(g_startX - curUnit*2, g_startY - curUnit*2), 
                                ImVec2(p_handle.x + curUnit, p_handle.y + curUnit));
                    if (area.Contains(io.MousePos)) {
                        isDragging = true;
                        dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
                    }
                }
            }
        }

        if (ImGui::IsMouseDown(0)) {
            if (isScaling) {
                ImVec2 currentHandleCenter = io.MousePos - dragOffset;
                float deltaX = currentHandleCenter.x - g_startX;
                float newScale = deltaX / (baseStepX * 6.5f + baseUnit);
                g_boardManualScale = std::clamp(newScale, 0.2f, 5.0f);
                p_handle = CalcHandlePos(g_startX, g_startY, g_boardManualScale);
            } 
            else if (isDragging) {
                g_startX = io.MousePos.x - dragOffset.x;
                g_startY = io.MousePos.y - dragOffset.y;
                p_handle = CalcHandlePos(g_startX, g_startY, g_boardManualScale);
            }
        } else {
            isScaling = false;
            isDragging = false;
        }

        d->AddCircleFilled(p_handle, 16.0f * g_autoScale, IM_COL32(255, 215, 0, 240));
        d->AddCircle(p_handle, (16.0f + 2.5f * sinf(ImGui::GetTime()*12)) * g_autoScale, IM_COL32(255, 255, 255, 180), 32, 2.5f);
    }

    float time = (float)ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * curStepX + (r % 2 == 1 ? curStepX * 0.5f : 0);
            float cy = g_startY + r * curStepY;
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), curUnit * 0.95f); 
            float hue = fmodf(time * 0.4f + (cx + cy) * 0.0005f, 1.0f);
            float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 1.0f, rf, gf, bf);
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + curUnit * cosf(a), cy + curUnit * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 240), ImDrawFlags_Closed, 3.5f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 菜单 UI
// =================================================================
bool ModernToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight() * 0.85f;
    float w = h * 2.1f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); }
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.25f; 
    ImVec4 col_bg = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0.15f, 0.70f, 0.45f, 1.0f), g_anim[idx]);
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(col_bg), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y*0.5f), label);
    return pressed;
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); 
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f * g_autoScale;
    style.FrameRounding = 6.0f * g_autoScale;
    style.ItemSpacing = ImVec2(10 * g_autoScale, 15 * g_autoScale);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v2.5", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        
        g_menuX = ImGui::GetWindowPos().x;
        g_menuY = ImGui::GetWindowPos().y;
        float curW = ImGui::GetWindowSize().x;
        float curH = ImGui::GetWindowSize().y;
        g_menuCollapsed = ImGui::IsWindowCollapsed();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGuiID resizeId = window->GetID("##Resize");
        if (ImGui::GetActiveID() == resizeId && ImGui::IsMouseDragging(0)) {
            g_menuW = curW; 
            g_menuH = curH;
            g_scale = curW / (350.0f * g_autoScale);
            g_fontUpdateTimer = 0.5f; 
        }

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            
            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), (const char*)u8"· VSYNC 同步中 | FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8" 智能预测 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手预测", &g_predict_enemy, 1); 
                ModernToggle((const char*)u8"海克斯辅助", &g_predict_hex, 2); 
                ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8" 透视显示 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); 
                ModernToggle((const char*)u8"备战席透视", &g_esp_bench, 4); 
                ModernToggle((const char*)u8"商店概率透视", &g_esp_shop, 5); 
                ImGui::Unindent();
            }
            ImGui::Separator();
            ModernToggle((const char*)u8"锁定棋盘位置", &g_boardLocked, 8); 
            ModernToggle((const char*)u8"智能拿牌", &g_auto_buy, 6); 
            ModernToggle((const char*)u8"极速退房", &g_instant, 7);
            
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 55 * g_autoScale))) {
                SaveConfig();
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 主循环
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    // 1. 先加载配置
    LoadConfig(); 
    
    // 2. 启动输入处理线程
    static bool running = true; 
    std::thread it([&] { 
        while(running) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::yield(); 
        } 
    });
    
    // 3. 进入循环后再初始化字体，确保环境已完全建立
    bool firstFrame = true;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (running) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;

        // 防抖计时
        if (g_fontUpdateTimer > 0.0f) {
            g_fontUpdateTimer -= deltaTime;
            if (g_fontUpdateTimer <= 0.0f) g_needUpdateFontSafe = true;
        }

        imgui.BeginFrame(); 
        
        // 关键修复：在 BeginFrame 之后检查字体构建状态
        if (firstFrame || g_needUpdateFontSafe) {
            UpdateFontHD(true); 
            g_needUpdateFontSafe = false;
            firstFrame = false;
        }

        if (!g_resLoaded) { 
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); 
            g_resLoaded = true; 
        }

        DrawBoard(); 
        DrawMenu();
        
        imgui.EndFrame(); 
    }

    g_HexShader.Cleanup();
    running = false; 
    if (it.joinable()) it.join(); 
    return 0;
}
