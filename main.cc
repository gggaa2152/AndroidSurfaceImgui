#include "Global.h"
#include "AImGui.h"
#include <android/log.h>
#include <unistd.h>
#include <thread>
#include <chrono>

// 日志定义
#define LOG_TAG "JKChess"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// --- 严格保留原有的业务字段名 ---
int gold = 100;
int level = 8;
bool g_featurePredict = false;
bool g_featureESP = false;
bool g_featureInstantQuit = false;

// --- 棋盘绘制函数 ---
void DrawChessboardOverlay() {
    if (!g_featureESP) return;
    
    // 获取背景绘制列表，用于在 UI 窗口下方、游戏画面上方绘制
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    // 模拟金铲铲棋盘布局 (4x7)
    float startX = 200.0f;
    float startY = 500.0f;
    float spacing = 70.0f;
    
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 7; c++) {
            ImVec2 center(startX + c * spacing, startY + r * spacing);
            // 绘制半透明圆点作为棋盘格参考
            drawList->AddCircleFilled(center, 20.0f, IM_COL32(0, 255, 200, 100));
            drawList->AddCircle(center, 21.0f, IM_COL32(255, 255, 255, 150), 20, 1.0f);
        }
    }
}

int main(int argc, char** argv) {
    LOGI("=== JKChess Assistant Starting... ===");

    // 1. 初始化 AImGui 配置
    android::AImGui::Options options;
    options.renderType = android::AImGui::RenderType::RenderNative;
    options.autoUpdateOrientation = true; // 自动处理横竖屏转换
    
    android::AImGui imgui(options);

    if (!imgui) {
        LOGI("[-] Error: Could not initialize AImGui.");
        return -1;
    }

    // 2. 关键同步：将库内部创建的上下文同步到当前线程
    // 解决 0x50 崩溃的核心操作
    ImGui::SetCurrentContext(ImGui::GetCurrentContext());
    LOGI("[+] ImGui Context Bound: %p", ImGui::GetCurrentContext());

    // 3. 字体加载 (com.tencent.jkchess 建议大一点的字体方便触摸)
    ImGuiIO& io = ImGui::GetIO();
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, 24.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        LOGI("[+] Successfully loaded Chinese font.");
    }

    // 4. 输入线程：处理触摸和屏幕事件
    bool running = true;
    std::thread inputThread([&] {
        while (running) {
            imgui.ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    LOGI("[*] Main loop started.");

    // 5. 主渲染循环
    while (running) {
        imgui.BeginFrame();

        // 绘制棋盘透视参考点
        DrawChessboardOverlay();

        // 绘制助手控制窗口
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(450, 550), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("JKChess Assistant", &running, ImGuiWindowFlags_NoCollapse)) {
            
            // 状态显示
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "游戏状态 (JKChess)");
            ImGui::Separator();
            ImGui::Text("金币数量: %d", gold);
            ImGui::Text("小小英雄等级: %d", level);
            ImGui::Text("帧率: %.1f FPS", io.Framerate);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 功能控制区
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "功能列表");
            
            ImGui::Checkbox("预测轨迹 (Predict)", &g_featurePredict);
            ImGui::Checkbox("棋盘透视 (ESP Overlay)", &g_featureESP);
            ImGui::Checkbox("瞬时退赛 (InstantQuit)", &g_featureInstantQuit);

            // 底部操作
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 70);
            ImGui::Separator();
            if (ImGui::Button("完全退出助手", ImVec2(-1, 50))) {
                running = false;
            }
        }
        ImGui::End();

        imgui.EndFrame();
    }

    // 6. 清理退出
    running = false;
    if (inputThread.joinable()) {
        inputThread.join();
    }
    
    LOGI("=== JKChess Assistant Shutdown ===");
    return 0;
}
