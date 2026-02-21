#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

// 系统头文件
#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <string>
#include "imgui.h" // 确保引入 imgui.h 头文件

// ========== 金铲铲助手数据 ==========
int gold = 100;
int level = 8;
int hp = 85;
bool autoBuy = true;
bool autoRefresh = true;

// ========== 功能开关 ==========
bool g_featurePredict = false;     // 预测
bool g_featureESP = false;         // 透视
bool g_featureInstantQuit = false; // 秒退

// ========== 棋盘设置 ==========
const int CHESSBOARD_ROWS = 4;       // 4行
const int CHESSBOARD_COLS = 7;       // 7列
float g_chessboardScale = 1.0f;
float g_chessboardPosX = 200;
float g_chessboardPosY = 200;
bool g_chessboardDragging = false;

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;
const float MIN_SCALE = 0.3f;
const float MAX_SCALE = 5.0f;

// ========== 窗口位置和大小 ==========
ImVec2 g_windowPos = ImVec2(50, 100);
ImVec2 g_windowSize = ImVec2(280, 400);
bool g_windowPosInitialized = false;

// ========== 配置文件路径 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

// ========== 帧率计算 ==========
float g_currentFPS = 0.0f;
int g_frameCount = 0;
auto g_fpsTimer = std::chrono::high_resolution_clock::now();

// ========== 动画变量 ==========
float g_toggleAnimProgress[10] = {0};
int g_toggleAnimTarget[10] = {0};

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    
    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        printf("[+] Trying font: %s\n", path);
        font = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        if (font) {
            printf("[+] Loaded font: %s\n", path);
            io.FontDefault = font;
            break;
        }
    }
    
    if (!font) {
        printf("[-] No Chinese font found, using default\n");
        io.Fonts->AddFontDefault();
    }
    
    io.Fonts->Build();
}

// ========== 保存配置 ==========
void SaveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "# JCC Assistant Config\n");
        fprintf(f, "version=1.0\n");
        fprintf(f, "scale=%.2f\n", g_globalScale);
        fprintf(f, "predict=%d\n", g_featurePredict ? 1 : 0);
        fprintf(f, "esp=%d\n", g_featureESP ? 1 : 0);
        fprintf(f, "instantQuit=%d\n", g_featureInstantQuit ? 1 : 0);
        fprintf(f, "windowPosX=%.0f\n", g_windowPos.x);
        fprintf(f, "windowPosY=%.0f\n", g_windowPos.y);
        fprintf(f, "windowWidth=%.0f\n", g_windowSize.x);
        fprintf(f, "windowHeight=%.0f\n", g_windowSize.y);
        fprintf(f, "chessboardScale=%.2f\n", g_chessboardScale);
        fprintf(f, "chessboardPosX=%.0f\n", g_chessboardPosX);
        fprintf(f, "chessboardPosY=%.0f\n", g_chessboardPosY);
        fclose(f);
    }
}

// ========== 加载配置 ==========
void LoadConfig() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            float fval; int ival;
            if (sscanf(line, "scale=%f", &fval) == 1) {
                g_globalScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
            }
            else if (sscanf(line, "predict=%d", &ival) == 1) g_featurePredict = (ival != 0);
            else if (sscanf(line, "esp=%d", &ival) == 1) g_featureESP = (ival != 0);
            else if (sscanf(line, "instantQuit=%d", &ival) == 1) g_featureInstantQuit = (ival != 0);
            else if (sscanf(line, "windowPosX=%f", &fval) == 1) g_windowPos.x = fval;
            else if (sscanf(line, "windowPosY=%f", &fval) == 1) g_windowPos.y = fval;
            else if (sscanf(line, "windowWidth=%f", &fval) == 1) g_windowSize.x = fval;
            else if (sscanf(line, "windowHeight=%f", &fval) == 1) g_windowSize.y = fval;
            else if (sscanf(line, "chessboardScale=%f", &fval) == 1) g_chessboardScale = fval;
            else if (sscanf(line, "chessboardPosX=%f", &fval) == 1) g_chessboardPosX = fval;
            else if (sscanf(line, "chessboardPosY=%f", &fval) == 1) g_chessboardPosY = fval;
        }
        fclose(f);
        ImGui::GetIO().FontGlobalScale = g_globalScale;
        g_windowPosInitialized = true;
    } else {
        SaveConfig();
    }
}

// ========== 精美滑动开关 ==========
bool ToggleSwitch(const char* label, bool* v, int animIdx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.8f;
    const float radius = height * 0.45f;
    ImVec2 pos = window->DC.CursorPos;
    ImRect total_bb(pos, ImVec2(pos.x + width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + height));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;
    float target = (*v ? 1.0f : 0.0f);
    g_toggleAnimTarget[animIdx] = target;
    float& progress = g_toggleAnimProgress[animIdx];
    float speed = 0.15f;
    progress += (target - progress) * speed;
    if (fabs(progress - target) < 0.01f) progress = target;
    ImVec4 bgColorOn(0.2f, 0.8f, 0.3f, 0.9f);
    ImVec4 bgColorOff(0.3f, 0.3f, 0.3f, 0.9f);
    ImVec4 currentBgColor(
        bgColorOff.x + (bgColorOn.x - bgColorOff.x) * progress,
        bgColorOff.y + (bgColorOn.y - bgColorOff.y) * progress,
        bgColorOff.z + (bgColorOn.z - bgColorOff.z) * progress,
        bgColorOff.w + (bgColorOn.w - bgColorOff.w) * progress
    );
    ImRect frame_bb(pos, ImVec2(pos.x + width, pos.y + height));
    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(currentBgColor), height * 0.5f);
    float shift = progress * (width - 2 * radius - 4); // 添加了分号
    ImVec2 thumbCenter(pos.x + radius + shift + (radius / 2), pos.y + height / 2);
    window->
