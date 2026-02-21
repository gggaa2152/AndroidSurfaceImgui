#include "Global.h"
#include "AImGui.h"

#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <signal.h>

// 安全退出控制
std::atomic<bool> g_Running(true);

void SignalHandler(int sig)
{
    LogInfo("[*] Signal received, safely exiting...");
    g_Running = false;
}

int main(int argc, char** argv)
{
    // 注册信号处理，以便在使用 Ctrl+C 时能安全退出
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 默认配置
    android::AImGui::Options options;
    options.exchangeFontData = true;

    // 通过命令行参数切换模式
    if (argc > 1 && std::string(argv[1]) == "client") 
    {
        options.renderType = android::AImGui::RenderType::RenderClient;
        LogInfo("[+] Starting com.tencent.jkchess - Client Mode");
    } 
    else 
    {
        options.renderType = android::AImGui::RenderType::RenderServer;
        LogInfo("[+] Starting com.tencent.jkchess - Server Mode");
    }

    // 初始化 AImGui，底层框架会自动处理 Surface 逻辑
    android::AImGui imgui(options);

    if (!imgui)
    {
        LogInfo("[-] ImGui initialization failed");
        return 0;
    }

    std::thread processInputEventThread(
        [&]
        {
            while (g_Running && imgui)
            {
                imgui.ProcessInputEvent();
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });

    while (g_Running && imgui)
    {
        imgui.BeginFrame();
        
        // --- 你的业务 UI (ImGui) 逻辑可以写在这里 ---
        
        imgui.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (processInputEventThread.joinable())
        processInputEventThread.join();

    LogInfo("[+] Exit successful");
    return 0;
}
