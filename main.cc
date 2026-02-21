#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

// 系统头文件
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

// ========== 业务数据 (com.tencent.jkchess) ==========
int gold = 100;
int level = 8;
int hp = 85;

// ========== 功能开关 ==========
bool g_featurePredict = false;
bool g_featureESP = false;
bool g_featureInstantQuit = false;

// ========== 棋盘配置 ==========
const int CHESSBOARD_ROWS = 4;
const int CHESSBOARD_COLS = 7;
float g_chessboardScale = 1.0f;
float g_chessboardPosX = 200.0f;
float g_chessboardPosY = 200.0f;

// ========== 动画变量 ==========
float g_toggleAnimProgress[10] = {0.0f};

// ========== 1. 字体加载逻辑 (安全增强版) ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    // 常见的 Android 字体路径
    const char* fontPaths[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansSC-Regular.otf"
    };

    ImFontConfig config;
    config.SizePixels = 18.0f;
    config.OversampleH = 2;
    config.OversampleV = 2;

    bool fontLoaded = false;
    for (const char* path : fontPaths) {
        if (access(path, R_OK) == 0) { // 检查文件是否可读
            if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull())) {
                fontLoaded = true;
                break;
            }
        }
    }
    
    if (!fontLoaded) {
        printf("[!] Warning: Chinese font not found, using default.\n");
    }
}

// ========== 2. 精美滑动开关 (修正了 Context 引用) ==========
bool ToggleSwitch(const char* label, bool* v, int animIdx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const float height = ImGui::GetFrameHeight() * 0.8f;
    const float width = height * 2.0f;
    const ImVec2 pos = window->DC.CursorPos;
    
    const ImRect total_bb(pos, ImVec2(pos.x + width + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x, pos.y + height));
    ImGui::ItemSize(total_bb);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool pressed = ImGui::ButtonBehavior(total_bb, id, NULL, NULL, ImGuiButtonFlags_PressedOnClick);
    if (pressed) *v = !*v;

    // 平滑插值动画
    float target = *v ? 1.0f : 0.0f;
    g_toggleAnimProgress[animIdx] += (target - g_toggleAnimProgress[animIdx]) * 0.2f;

    // 绘制背景
    ImU32 col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.2f, 0.2f, 1.0f), ImVec4(0.25f, 0.75f, 0.35f, 1.0f), g_toggleAnimProgress[animIdx]));
    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), col_bg, height * 0.5f);
    
    // 绘制滑块
    float radius = height * 0.4f;
    float offset = radius + 2.0f + g_toggleAnimProgress[animIdx] * (width - radius * 2.0f - 4.0f);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + offset, pos.y + height * 0.5f), radius, IM_COL32_WHITE);
    
    // 绘制标签文字
    ImGui::RenderText(ImVec2(pos.x + width + ImGui::GetStyle().ItemInnerSpacing.x, pos.y), label);
    return pressed;
}

// ========== 3. 棋盘透视绘制 ==========
void DrawChessboardOverlay() {
    if (!g_featureESP) return;
    
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    float cellSize = 50.0f * g_chessboardScale;
    
    for (int r = 0; r < CHESSBOARD_ROWS; r++) {
        for (int c = 0; c < CHESSBOARD_COLS; c++) {
            ImVec2 center(g_chessboardPosX + c * cellSize + cellSize/2, 
                          g_chessboardPosY + r * cellSize + cellSize/2);
            ImU32 col = (r + c) % 2 == 0 ? IM_COL32(0, 180, 255, 150) : IM_COL32(255, 80, 80, 150);
            drawList->AddCircle(center, cellSize * 0.35f, col, 32, 2.0f);
        }
    }
}

// ========== 4. 主程序入口 ==========
int main(int argc, char** argv) {
    printf("[*] JKChess Assistant Starting...\n");

    // AImGui 实例配置 (必须最先执行)
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) {
        printf("[!] Error: Failed to initialize AImGui!\n");
        return -1;
    }

    // 初始化 UI 样式
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.WindowBorderSize = 0.0f;

    // 加载中文字体
    LoadChineseFont();

    bool running = true;

    // 输入处理线程
    std::thread inputThread([&] {
        while (running) {
            imgui.ProcessInputEvent(); // 底层 poll 等待事件
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // 主渲染循环
    while (running) {
        imgui.BeginFrame();

        // 1. 绘制背景层 (棋盘)
        DrawChessboardOverlay();

        // 2. 绘制菜单窗口
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("金铲铲助手", &running, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Separator();

            if (ImGui::CollapsingHeader("基础功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ToggleSwitch("预测模式", &g_featurePredict, 0);
                ToggleSwitch("棋盘透视", &g_featureESP, 1);
                ToggleSwitch("瞬时退赛", &g_featureInstantQuit, 2);
            }

            if (ImGui::CollapsingHeader("游戏数值")) {
                ImGui::BulletText("金币: %d", gold);
                ImGui::BulletText("等级: %d", level);
                float hp_f = hp / 100.0f;
                ImGui::ProgressBar(hp_f, ImVec2(-1, 0), "当前血量");
            }

            if (g_featureESP) {
                ImGui::Separator();
                ImGui::Text("棋盘位置微调");
                ImGui::SliderFloat("缩放", &g_chessboardScale, 0.5f, 2.5f);
                ImGui::DragFloat2("坐标", &g_chessboardPosX); // 支持手动拖拽坐标
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 60);
            if (ImGui::Button("彻底退出", ImVec2(-1, 40))) {
                running = false;
            }
        }
        ImGui::End();

        imgui.EndFrame();
    }

    // 退出清理
    running = false;
    if (inputThread.joinable()) {
        inputThread.join();
    }

    printf("[*] JKChess Assistant Exited.\n");
    return 0;
}
