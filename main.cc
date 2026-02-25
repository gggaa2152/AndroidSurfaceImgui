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
#include <chrono>

// =================================================================
// 2. 全局状态变量
// =================================================================
const char* g_configPath = "/sdcard/jkchess_config.ini";  // 改为SD卡，避免权限问题

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

bool g_menuCollapsed = false; 

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
float g_lastFontUpdateTime = 0.0f;  // 上次字体更新时间

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
    
    float targetSize = std::min(20.0f * g_autoScale * g_scale, 140.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 2.0f) return;
    
    // 限制字体更新频率（最多每秒2次）
    float currentTime = ImGui::GetTime();
    if (!force && currentTime - g_lastFontUpdateTime < 0.5f) return;
    g_lastFontUpdateTime = currentTime;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 2;
    config.PixelSnapH = false;
    
    if (access("/system/fonts/SysSans-Hans-Regular.ttf", R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF("/system/fonts/SysSans-Hans-Regular.ttf", 
            targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 棋盘绘制（优化版）
// =================================================================
struct HexGridCache {
    ImVec2 points[6];  // 单位向量
    bool valid = false;
} g_hexCache;

void DrawBoard() {
    if (!g_esp_board) return;
    
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    // 预计算六边形顶点（只计算一次）
    if (!g_hexCache.valid) {
        for(int i=0; i<6; i++) {
            float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
            g_hexCache.points[i] = ImVec2(cosf(a), sinf(a));
        }
        g_hexCache.valid = true;
    }
    
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
            
            float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                pts[i] = ImVec2(cx + sz * g_hexCache.points[i].x, 
                               cy + sz * g_hexCache.points[i].y);
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 255), 
                ImDrawFlags_Closed, 4.0f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 滑动开关组件
// =================================================================
bool ToggleSwitch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const float w = 52.0f * g_scale;
    const float h = 30.0f * g_scale;
    const float radius = h * 0.5f;
    
    ImVec2 pos = window->DC.CursorPos;
    float textWidth = ImGui::CalcTextSize(label).x;
    ImRect total_bb(pos, ImVec2(pos.x + w + style.ItemInnerSpacing.x + textWidth, pos.y + h));
    ImRect switch_bb(pos, ImVec2(pos.x + w, pos.y + h));
    
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;
    
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(switch_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        SaveConfig();
    }
    
    // 绘制背景
    ImU32 bg_col = ImGui::GetColorU32(*v ? ImVec4(0.0f, 0.6f, 1.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    window->DrawList->AddRectFilled(switch_bb.Min, switch_bb.Max, bg_col, radius);
    
    // 绘制滑块
    float circle_pos = *v ? switch_bb.Max.x - radius - 2 : switch_bb.Min.x + radius + 2;
    window->DrawList->AddCircleFilled(ImVec2(circle_pos, pos.y + radius), radius - 4, IM_COL32_WHITE);
    
    // 绘制文字
    ImGui::RenderText(ImVec2(switch_bb.Max.x + style.ItemInnerSpacing.x, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f), label);
    
    return pressed;
}

// =================================================================
// 7. 菜单UI（优化版）
// =================================================================
void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float baseW = 320.0f * g_autoScale;
    float baseH = 500.0f * g_autoScale;
    
    // 窗口大小
    float windowW = baseW * g_scale;
    float windowH = g_menuCollapsed ? 40.0f * g_scale : baseH * g_scale;
    
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowW, windowH), ImGuiCond_Always);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar;
    
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, flags)) {
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        float titleBarHeight = 35.0f * g_scale;
        
        // 标题栏区域
        ImRect titleBarRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + titleBarHeight));
        
        // 绘制标题栏
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(titleBarRect.Min, titleBarRect.Max, IM_COL32(40, 40, 50, 240), 8.0f, ImDrawFlags_RoundCornersTop);
        
        // 标题文字
        const char* title = g_menuCollapsed ? (const char*)u8"🔍 金铲铲助手 ▼" : (const char*)u8"🔍 金铲铲助手 ▲";
        ImVec2 textPos = ImVec2(windowPos.x + 15, windowPos.y + (titleBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), title);
        
        // 缩放控制（右下角）
        float handleSize = 40.0f * g_scale;
        ImVec2 br(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
        ImRect scaleHandle(br - ImVec2(handleSize, handleSize), br);
        
        // 绘制缩放手柄
        bool handleHovered = scaleHandle.Contains(io.MousePos);
        drawList->AddTriangleFilled(
            ImVec2(br.x - 10, br.y - handleSize + 10),
            ImVec2(br.x - handleSize + 10, br.y - 10),
            ImVec2(br.x - 10, br.y - 10),
            handleHovered ? IM_COL32(0, 150, 255, 255) : IM_COL32(100, 100, 100, 200));
        
        // 缩放交互（优化：使用乘法而不是加法，更线性）
        static bool isScaling = false;
        static float startScale = 1.0f;
        static float startMouseX = 0;
        
        if (handleHovered && ImGui::IsMouseClicked(0)) {
            isScaling = true;
            startScale = g_scale;
            startMouseX = io.MousePos.x;
        }
        
        if (isScaling) {
            if (ImGui::IsMouseDown(0)) {
                // 使用乘法缩放，更自然
                float ratio = 1.0f + (io.MousePos.x - startMouseX) / 500.0f;
                float newScale = std::clamp(startScale * ratio, 0.5f, 2.5f);
                
                // 减少字体更新频率
                if (std::abs(newScale - g_scale) > 0.02f) {
                    g_scale = newScale;
                    g_needUpdateFontSafe = true;
                }
            } else {
                isScaling = false;
                SaveConfig();
            }
        }
        
        // 标题栏点击切换折叠
        if (titleBarRect.Contains(io.MousePos) && 
            !scaleHandle.Contains(io.MousePos) && 
            ImGui::IsMouseReleased(0)) {
            g_menuCollapsed = !g_menuCollapsed;
            SaveConfig();
        }
        
        // 窗口拖动（标题栏区域）
        static bool isDragging = false;
        if (titleBarRect.Contains(io.MousePos) && !scaleHandle.Contains(io.MousePos)) {
            if (ImGui::IsMouseClicked(0)) {
                isDragging = true;
            }
        }
        
        if (isDragging) {
            if (ImGui::IsMouseDown(0)) {
                g_menuX += io.MouseDelta.x;
                g_menuY += io.MouseDelta.y;
            } else {
                isDragging = false;
                SaveConfig();
            }
        }
        
        // 窗口内容
        if (!g_menuCollapsed) {
            ImGui::SetCursorPosY(titleBarHeight + 10);
            
            // 设置字体（限制缩放时的更新频率）
            float fontSize = 18.0f * g_scale;
            if (std::abs(fontSize / g_current_rendered_size - 1.0f) > 0.1f) {
                ImGui::SetWindowFontScale(fontSize / g_current_rendered_size);
            }
            
            // FPS显示
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            // 预测功能
            if (ImGui::CollapsingHeader((const char*)u8"📊 预测功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(15.0f * g_scale);
                ToggleSwitch((const char*)u8"预测对手分布", &g_predict_enemy);
                ToggleSwitch((const char*)u8"海克斯强化预测", &g_predict_hex);
                ImGui::Unindent(15.0f * g_scale);
            }
            
            // 透视功能
            if (ImGui::CollapsingHeader((const char*)u8"👁️ 透视功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(15.0f * g_scale);
                ToggleSwitch((const char*)u8"对手棋盘透视", &g_esp_board);
                ToggleSwitch((const char*)u8"对手备战席透视", &g_esp_bench);
                ToggleSwitch((const char*)u8"对手商店透视", &g_esp_shop);
                ImGui::Unindent(15.0f * g_scale);
            }
            
            ImGui::Separator();
            
            // 其他功能
            ToggleSwitch((const char*)u8"⚡ 全自动拿牌", &g_auto_buy);
            ToggleSwitch((const char*)u8"⏱️ 极速秒退助手", &g_instant);
            ImGui::Spacing();
            
            // 保存按钮
            if (ImGui::Button((const char*)u8"💾 保存设置", ImVec2(-1, 45 * g_scale))) {
                SaveConfig();
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 8. 程序入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    eglSwapInterval(eglGetCurrentDisplay(), 1);
    
    LoadConfig();
    UpdateFontHD(true);
    
    std::thread inputThread([&] {
        while (true) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 减少CPU占用
        }
    });
    
    int frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
    
    while (true) {
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制帧率，避免CPU过载
        if (deltaTime < 0.016f) {  // 约60fps
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        if (g_needUpdateFontSafe) {
            UpdateFontHD(true);
            g_needUpdateFontSafe = false;
        }
        
        imgui.BeginFrame();
        
        if (!g_resLoaded) {
            g_heroTexture = LoadTextureFromFile("/sdcard/heroes/FUX/aurora.png");  // 改为SD卡路径
            g_textureLoaded = (g_heroTexture != 0);
            g_resLoaded = true;
        }
        
        DrawBoard();
        DrawMenu();
        
        imgui.EndFrame();
        
        frameCount++;
    }
    
    inputThread.detach();
    return 0;
}
