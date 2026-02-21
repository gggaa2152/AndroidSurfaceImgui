#include "Global.h"
#include "AImGui.h"
#include <android/log.h>
#include <unistd.h>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "JKChess", __VA_ARGS__)

int main(int argc, char** argv) {
    LOGI("=== Diagnostic Start ===");

    // 1. 初始化 AImGui
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) {
        LOGI("[-] AImGui Init Failed");
        return -1;
    }

    // 2. 强行绑定并立即测试上下文
    ImGui::SetCurrentContext(ImGui::GetCurrentContext());
    LOGI("[+] Context bound: %p", ImGui::GetCurrentContext());

    // 3. 暂时【注释掉】字体加载，防止路径或内存溢出崩溃
    // LOGI("[*] Skipping font loading for test...");

    bool running = true;
    
    // 4. 输入处理线程
    std::thread inputThread([&] {
        while (running) {
            imgui.ProcessInputEvent();
            usleep(5000); 
        }
    });

    LOGI("[*] Entering Render Loop...");
    
    while (frameCount < 1000) { // 运行一段时间自动退出，方便看日志
        imgui.BeginFrame();

        // 使用最原始的窗口，不加任何样式
        ImGui::Begin("Debug Window");
        ImGui::Text("System Ready.");
        ImGui::Text("If you see this, offset is OK!");
        if (ImGui::Button("Exit Test")) break;
        ImGui::End();

        imgui.EndFrame();
        usleep(16000);
    }

    running = false;
    if (inputThread.joinable()) inputThread.join();
    LOGI("=== Diagnostic End ===");
    return 0;
}
