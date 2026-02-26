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
// 全局状态变量
// =================================================================
const char* g_configPath = "/sdcard/jkchess_config.ini";

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
float g_lastFontUpdateTime = 0.0f;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// 六边形顶点缓存
struct HexPoints {
    ImVec2 pts[6];
    float lastSz = 0.0f;
} g_hexPoints;

// =================================================================
// 配置管理
// =================================================================
void SaveConfig() { /* 保持不变 */ }
void LoadConfig() { /* 保持不变 */ }

// =================================================================
// 纹理加载与字体更新
// =================================================================
GLuint LoadTextureFromFile(const char* filename) {
    int w, h, channels;
    unsigned char* data = stbi_load(filename, &w, &h, &channels, 4);
    if (!data) return 0;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return texture;
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.y <= 0) return;
    float screenH = io.DisplaySize.y;
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::min(20.0f * g_autoScale * g_scale, 140.0f);
    float changeRatio = std::abs(targetSize - g_current_rendered_size) / (g_current_rendered_size + 0.1f);
    float currentTime = ImGui::GetTime();
    if (!force && changeRatio < 0.1f) return;
    if (!force && currentTime - g_lastFontUpdateTime < 1.0f) return;
    g_lastFontUpdateTime = currentTime;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    ImFontConfig config;
    config.OversampleH = 1;
    config.PixelSnapH = true;
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 棋盘绘制
// =================================================================
void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded || g_heroTexture == 0) return;
    drawList->AddImage((void*)(intptr_t)g_heroTexture,
        ImVec2(center.x - size, center.y - size),
        ImVec2(center.x + size, center.y + size));
}

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

    // 交互（保持不变，略）
    static bool isDragging = false, isScaling = false;
    static ImVec2 dragOffset;
    // ... 棋盘拖拽和缩放逻辑（与之前相同）...

    // 缓存六边形顶点
    if (std::abs(sz - g_hexPoints.lastSz) > 0.1f) {
        for (int i = 0; i < 6; i++) {
            float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
            g_hexPoints.pts[i] = ImVec2(cosf(a) * sz, sinf(a) * sz);
        }
        g_hexPoints.lastSz = sz;
    }

    // 绘制六边形网格
    float time = (float)ImGui::GetTime();
    for(int r = 0; r < 4; r++) {
        for(int c = 0; c < 7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            if (g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz);
            float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            ImVec2 pts[6];
            for (int i = 0; i < 6; i++) {
                pts[i] = ImVec2(cx + g_hexPoints.pts[i].x, cy + g_hexPoints.pts[i].y);
            }
            d->AddPolyline(pts, 6, IM_COL32((int)(rf*255), (int)(gf*255), (int)(bf*255), 255), 
                ImDrawFlags_Closed, 4.0f * g_autoScale);
        }
    }
}

// =================================================================
// 滑动开关组件
// =================================================================
bool ToggleSwitch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const float w = 52.0f;
    const float h = 30.0f;
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
    ImU32 bg_col = ImGui::GetColorU32(*v ? ImVec4(0.0f, 0.6f, 1.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    window->DrawList->AddRectFilled(switch_bb.Min, switch_bb.Max, bg_col, radius);
    float circle_pos = *v ? switch_bb.Max.x - radius - 2 : switch_bb.Min.x + radius + 2;
    window->DrawList->AddCircleFilled(ImVec2(circle_pos, pos.y + radius), radius - 4, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(switch_bb.Max.x + style.ItemInnerSpacing.x, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f), label);
    return pressed;
}

// =================================================================
// 菜单UI（专业缩放实现）
// =================================================================
void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float baseW = 320.0f;
    float baseH = 500.0f;
    float windowW = baseW * g_scale;
    float windowH = g_menuCollapsed ? 40.0f : baseH * g_scale;
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowW, windowH), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, flags)) {
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        float titleBarHeight = 35.0f;
        ImRect titleBarRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + titleBarHeight));

        // 绘制标题栏
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(titleBarRect.Min, titleBarRect.Max, IM_COL32(40, 40, 50, 240), 8.0f, ImDrawFlags_RoundCornersTop);
        const char* title = g_menuCollapsed ? (const char*)u8"🔍 金铲铲助手 ▼" : (const char*)u8"🔍 金铲铲助手 ▲";
        ImVec2 textPos = ImVec2(windowPos.x + 15, windowPos.y + (titleBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), title);

        // 缩放控制区域
        float handleSize = 40.0f;
        ImVec2 br(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
        ImRect scaleHandle(br - ImVec2(handleSize, handleSize), br);
        bool handleHovered = scaleHandle.Contains(io.MousePos);
        drawList->AddTriangleFilled(
            ImVec2(br.x - 10, br.y - handleSize + 10),
            ImVec2(br.x - handleSize + 10, br.y - 10),
            ImVec2(br.x - 10, br.y - 10),
            handleHovered ? IM_COL32(0, 150, 255, 255) : IM_COL32(100, 100, 100, 200));

        // 专业缩放逻辑：使用 GetMouseDragDelta，字体只在结束时更新
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
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(0);
                float delta = dragDelta.x * 0.005f; // 灵敏度系数
                g_scale = std::clamp(startScale + delta, 0.5f, 2.5f);
            } else {
                isScaling = false;
                g_needUpdateFontSafe = true; // 缩放结束时更新字体
                SaveConfig();
            }
        }

        // 窗口拖动（标题栏）
        static bool isDragging = false;
        if (titleBarRect.Contains(io.MousePos) && !scaleHandle.Contains(io.MousePos)) {
            if (ImGui::IsMouseClicked(0)) isDragging = true;
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

        // 窗口内容（略，保持不变）
        if (!g_menuCollapsed) {
            ImGui::SetCursorPosY(titleBarHeight + 10);
            static float lastFontScale = 0;
            float fontSize = 18.0f;
            float fontScale = fontSize / g_current_rendered_size;
            if (std::abs(fontScale - lastFontScale) > 0.01f) {
                ImGui::SetWindowFontScale(fontScale);
                lastFontScale = fontScale;
            }
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            if (ImGui::CollapsingHeader((const char*)u8"📊 预测功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(15.0f);
                ToggleSwitch((const char*)u8"预测对手分布", &g_predict_enemy);
                ToggleSwitch((const char*)u8"海克斯强化预测", &g_predict_hex);
                ImGui::Unindent(15.0f);
            }
            if (ImGui::CollapsingHeader((const char*)u8"👁️ 透视功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(15.0f);
                ToggleSwitch((const char*)u8"对手棋盘透视", &g_esp_board);
                ToggleSwitch((const char*)u8"对手备战席透视", &g_esp_bench);
                ToggleSwitch((const char*)u8"对手商店透视", &g_esp_shop);
                ImGui::Unindent(15.0f);
            }
            ImGui::Separator();
            ToggleSwitch((const char*)u8"⚡ 全自动拿牌", &g_auto_buy);
            ToggleSwitch((const char*)u8"⏱️ 极速秒退助手", &g_instant);
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"💾 保存设置", ImVec2(-1, 45.0f))) {
                SaveConfig();
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 程序入口（单线程输入处理）
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    eglSwapInterval(eglGetCurrentDisplay(), 1);
    LoadConfig();
    UpdateFontHD(true);
    const char* texturePath = "/data/1/heroes/FUX/aurora.png";
    g_heroTexture = LoadTextureFromFile(texturePath);
    g_textureLoaded = (g_heroTexture != 0);
    g_resLoaded = true;

    while (true) {
        // 单线程：每帧处理输入
        imgui.ProcessInputEvent();

        if (g_needUpdateFontSafe) {
            UpdateFontHD(true);
            g_needUpdateFontSafe = false;
        }

        imgui.BeginFrame();
        DrawBoard();
        DrawMenu();
        imgui.EndFrame();

        std::this_thread::yield(); // 让出CPU，避免100%占用
    }
    return 0;
}
