#include "Global.h"
#include "AImGui.h"
#include <android/log.h>
#include <thread>
#include <chrono>
#include <vector>

// 安卓日志定义
#define LOG_TAG "JKChess"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ========== 核心数据 ==========
int gold = 100;
int level = 8;
bool g_featurePredict = false;
bool g_featureESP = false;
bool g_featureInstantQuit = false;

// 棋盘绘制参数
const int CHESSBOARD_ROWS = 4;
const int CHESSBOARD_COLS = 7;
float g_chessboardScale = 1.0f;
float g_chessboardPosX = 200.0f;
float g_chessboardPosY = 500.0f;

// ========== 棋盘绘制函数 (Background) ==========
void DrawChessboard() {
    if (!g_featureESP) return;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    float cellSize = 50.0f * g_chessboardScale;
    
    for (int r = 0; r < CHESSBOARD_ROWS; r++) {
        for (int c = 0; c < CHESSBOARD_COLS; c++) {
            ImVec2 center(g_chessboardPosX + c * cellSize, g_chessboardPosY + r * cellSize);
            ImU32 col = (r + c) % 2 == 0 ? IM_COL32(0, 255, 255, 100) : IM_COL32(255, 0, 255, 100);
            drawList->AddCircleFilled(center, cellSize * 0.3f, col);
        }
    }
}

int main(int argc, char** argv) {
    LOGI("=== Assistant Start ===");

    // 1. 初始化 AImGui 实例
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) {
        LOGI("Critical Error: AImGui context failed.");
        return -1;
    }

    // 2. 强行修复上下文绑定 (解决 0x50 崩溃的关键)
    ImGui::SetCurrentContext(ImGui::GetCurrentContext());
    LOGI("Context re-bound at: %p", ImGui::GetCurrentContext());

    // 3. 字体配置 (先尝试系统路径)
    ImGuiIO& io = ImGui::GetIO();
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    }

    // 4. 输入线程
    bool running = true;
    std::thread inputThread([&] {
        while (running) {
            imgui.ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // 5. 渲染循环
    while (running) {
        imgui.BeginFrame();

        // --- 绘制棋盘透视 ---
        DrawChessboard();

        // --- 绘制主窗口 ---
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 450), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("金铲铲助手", &running)) {
            ImGui::Text("状态: 运行中 | FPS: %.1f", io.Framerate);
            ImGui::Separator();

            // 功能开关
            ImGui::Checkbox("预测轨迹", &g_featurePredict);
            ImGui::Checkbox("显示棋盘透视", &g_featureESP);
            ImGui::Checkbox("秒退功能", &g_featureInstantQuit);

            ImGui::Separator();
            
            // 数据展示
            ImGui::Text("当前金币: %d", gold);
            ImGui::Text("当前等级: %d", level);

            // 棋盘微调 (仅当开启透视时显示)
            if (g_featureESP) {
                ImGui::Spacing();
                ImGui::Text("透视位置调整:");
                ImGui::SliderFloat("缩放", &g_chessboardScale, 0.5f, 2.0f);
                ImGui::DragFloat2("坐标", &g_chessboardPosX);
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 50);
            if (ImGui::Button("退出程序", ImVec2(-1, 35))) running = false;
        }
        ImGui::End();

        imgui.EndFrame();
    }

    // 6. 优雅清理
    running = false;
    if (inputThread.joinable()) inputThread.join();
    
    LOGI("=== Assistant Shutdown ===");
    return 0;
}
