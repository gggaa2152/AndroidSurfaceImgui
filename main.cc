#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

// 系统头文件
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sstream>
#include <thread>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <map>
#include <vector>

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
const int CHESSBOARD_ROWS = 4;
const int CHESSBOARD_COLS = 7;
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

// ========== 共享内存数据结构 ==========
struct SharedGameData {
    int gold;
    int level;
    int hp;
    int autoBuy;
    int autoRefresh;
    int timestamp;
    int version;
    char scriptName[64];
    int reserved[16];
};

int g_shm_fd = -1;
SharedGameData* g_sharedData = nullptr;
int g_lastTimestamp = 0;

// --- 初始化与读取共享内存 ---
bool InitSharedMemory() {
    g_shm_fd = open("/data/local/tmp/jcc_shared_mem", O_CREAT | O_RDWR, 0666);
    if (g_shm_fd < 0) return false;
    ftruncate(g_shm_fd, sizeof(SharedGameData));
    g_sharedData = (SharedGameData*)mmap(NULL, sizeof(SharedGameData), PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_sharedData == MAP_FAILED) { g_sharedData = nullptr; return false; }
    if (g_sharedData->version == 0) {
        g_sharedData->gold = 100; g_sharedData->version = 1;
    }
    return true;
}

void ReadFromSharedMemory() {
    if (!g_sharedData) return;
    if (g_sharedData->timestamp != g_lastTimestamp) {
        gold = g_sharedData->gold; level = g_sharedData->level; hp = g_sharedData->hp;
        autoBuy = (g_sharedData->autoBuy != 0); autoRefresh = (g_sharedData->autoRefresh != 0);
        g_lastTimestamp = g_sharedData->timestamp;
    }
}

void CleanupSharedMemory() {
    if (g_sharedData) munmap(g_sharedData, sizeof(SharedGameData));
    if (g_shm_fd >= 0) close(g_shm_fd);
}

// ========== 字体加载 ==========
bool LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* fontPaths[] = { "/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/DroidSansFallback.ttf" };
    ImFontConfig config;
    config.OversampleH = 2; config.OversampleV = 2;
    for (const char* path : fontPaths) {
        if (io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesChineseFull())) break;
    }
    return true;
}

// ========== 动画变量与开关 ==========
float g_toggleAnimProgress[10] = {0};
bool ToggleSwitch(const char* label, bool* v, int animIdx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(label);
    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.8f;
    ImVec2 pos = window->DC.CursorPos;
    ImRect total_bb(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::ItemSize(total_bb);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    float target = *v ? 1.0f : 0.0f;
    g_toggleAnimProgress[animIdx] += (target - g_toggleAnimProgress[animIdx]) * 0.15f;
    
    ImU32 col = ImGui::GetColorU32(*v ? ImVec4(0.2f, 0.8f, 0.3f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), col, height * 0.5f);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + height * 0.5f + g_toggleAnimProgress[animIdx] * (width - height), pos.y + height * 0.5f), height * 0.4f, IM_COL32_WHITE);
    
    bool pressed = ImGui::ButtonBehavior(total_bb, id, NULL, NULL, ImGuiButtonFlags_PressedOnClick);
    if (pressed) *v = !*v;
    return pressed;
}

// ========== 棋盘绘制 ==========
void DrawChessboard() {
    if (!g_featureESP) return;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    float cellSize = 40 * g_chessboardScale;
    for (int r = 0; r < CHESSBOARD_ROWS; r++) {
        for (int c = 0; c < CHESSBOARD_COLS; c++) {
            ImVec2 center(g_chessboardPosX + c * cellSize + cellSize/2, g_chessboardPosY + r * cellSize + cellSize/2);
            drawList->AddCircleFilled(center, cellSize * 0.3f, (r+c)%2 ? IM_COL32(255, 100, 100, 150) : IM_COL32(100, 100, 255, 150));
        }
    }
}

// ========== 配置存取 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";
void SaveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "predict=%d\nesp=%d\nscale=%.2f\n", g_featurePredict, g_featureESP, g_globalScale);
        fclose(f);
    }
}
void LoadConfig() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (f) { /* 简单的解析逻辑，此处略，可按需添加 */ fclose(f); }
}

// ========== 主函数 ==========
int main(int argc, char** argv)
{
    InitSharedMemory();
    LoadConfig();

    // 实例化 AImGui
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) return -1;
    LoadChineseFont();

    bool state = true;
    std::thread inputThread([&] {
        while (state) {
            imgui.ProcessInputEvent();
            usleep(5000);
        }
    });

    while (state) {
        ReadFromSharedMemory();
        imgui.BeginFrame();

        DrawChessboard();

        ImGui::SetNextWindowPos(g_windowPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(g_windowSize, ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("金铲铲助手", &state)) {
            ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
            ImGui::Separator();
            ToggleSwitch("预测", &g_featurePredict, 0);
            ToggleSwitch("透视", &g_featureESP, 1);
            ToggleSwitch("秒退", &g_featureInstantQuit, 2);
            ImGui::Separator();
            ImGui::Text("金币: %d | 等级: %d", gold, level);
            if (ImGui::Button("退出")) state = false;
        }
        ImGui::End();

        imgui.EndFrame();
    }

    state = false;
    if (inputThread.joinable()) inputThread.join();
    CleanupSharedMemory();
    SaveConfig();
    return 0;
}
