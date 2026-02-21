#include "Global.h"
#include "AImGui.h"

#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>

// 简单计时器
class SimpleTimer {
    std::chrono::high_resolution_clock::time_point start;
public:
    SimpleTimer() { reset(); }
    void reset() { start = std::chrono::high_resolution_clock::now(); }
    float elapsedMs() { 
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float, std::milli>(now - start).count();
    }
};

int main()
{
    android::AImGui imgui(android::AImGui::Options{.renderType = android::AImGui::RenderType::RenderNative, .autoUpdateOrientation = true});
    bool state = true, showDemoWindow = false, showAnotherWindow = false;
    ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);

    if (!imgui)
    {
        LogInfo("[-] ImGui initialization failed");
        return 0;
    }

    // 输入线程 - 无优先级设置
    std::thread processInputEventThread(
        [&]
        {
            while (state)
            {
                imgui.ProcessInputEvent();
                // 短暂让出CPU
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });

    const float TARGET_FPS = 120.0f;
    const float TARGET_FRAME_TIME_MS = 1000.0f / TARGET_FPS;
    SimpleTimer frameTimer;

    LogInfo("[+] Entering main loop");
    
    while (state)
    {
        frameTimer.reset();
        
        imgui.BeginFrame();

        // 1. Show the big demo window
        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // 2. Show a simple window
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!", &state);

            ImGui::Text("This is some useful text.");
            ImGui::Checkbox("Demo Window", &showDemoWindow);
            ImGui::Checkbox("Another Window", &showAnotherWindow);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
            ImGui::ColorEdit3("clear color", (float *)&clearColor);

            if (ImGui::Button("Button"))
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("FPS: %.1f", 1000.0f / frameTimer.elapsedMs());
            ImGui::End();
        }

        // 3. Show another simple window
        if (showAnotherWindow)
        {
            ImGui::Begin("Another Window", &showAnotherWindow);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                showAnotherWindow = false;
            ImGui::End();
        }

        imgui.EndFrame();
        
        // 动态休眠
        float frameTime = frameTimer.elapsedMs();
        if (frameTime < TARGET_FRAME_TIME_MS)
        {
            int sleepUs = (int)((TARGET_FRAME_TIME_MS - frameTime) * 1000);
            if (sleepUs > 0) {
                usleep(sleepUs);
            }
        }
    }

    if (processInputEventThread.joinable())
        processInputEventThread.join();

    return 0;
}
