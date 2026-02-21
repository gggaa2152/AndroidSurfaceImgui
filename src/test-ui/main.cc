#include "Global.h"
#include "AImGui.h"

#include <thread>
#include <iostream>
#include <cstdio>

// ========== 金铲铲助手数据 ==========
int gold = 100;
int level = 8;
int hp = 85;
bool autoBuy = true;
bool autoRefresh = true;
bool autoScaleEnabled = true;
float manualScale = 1.0f;
float currentScale = 1.0f;
ImVec2 baseSize(400, 300);
bool firstTime = true;

// ========== 读取游戏数据 ==========
void ReadGameData() {
    FILE* f = fopen("/data/local/tmp/game_data.txt", "r");
    if (f) {
        fscanf(f, "gold=%d\n", &gold);
        fscanf(f, "level=%d\n", &level);
        fscanf(f, "hp=%d\n", &hp);
        fclose(f);
    }
}

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

    std::thread processInputEventThread(
        [&]
        {
            while (state)
            {
                imgui.ProcessInputEvent();
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });

    while (state)
    {
        // 读取游戏数据
        ReadGameData();

        imgui.BeginFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // ========== 金铲铲助手窗口（替换原来的窗口）==========
        {
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            // 获取当前窗口大小用于自动缩放
            ImVec2 currentSize = ImGui::GetWindowSize();
            
            // 第一次运行时记录基准大小
            if (firstTime) {
                baseSize = currentSize;
                firstTime = false;
            }
            
            // 自动缩放计算
            float scaleX = currentSize.x / baseSize.x;
            float scaleY = currentSize.y / baseSize.y;
            float newScale = (scaleX > scaleY ? scaleX : scaleY);
            
            const float MIN_SCALE = 0.5f;
            const float MAX_SCALE = 5.0f;
            if (newScale < MIN_SCALE) newScale = MIN_SCALE;
            if (newScale > MAX_SCALE) newScale = MAX_SCALE;
            
            // 缩放控制
            ImGui::Checkbox("自动缩放", &autoScaleEnabled);
            
            float effectiveScale;
            if (autoScaleEnabled) {
                effectiveScale = newScale;
                ImGui::Text("当前缩放: %.2f", effectiveScale);
            } else {
                ImGui::SliderFloat("缩放比例", &manualScale, MIN_SCALE, MAX_SCALE, "%.2f");
                effectiveScale = manualScale;
            }
            
            // 应用字体缩放
            ImGui::GetIO().FontGlobalScale = effectiveScale;
            
            ImGui::Separator();

            // 游戏数据显示
            ImGui::Text("金币: %d", gold);
            ImGui::Text("等级: %d", level);
            ImGui::Text("血量: %d", hp);
            
            // 进度条
            float progressWidth = 200.0f * effectiveScale;
            float progressHeight = 20.0f * effectiveScale;
            ImGui::ProgressBar(hp/100.0f, ImVec2(progressWidth, progressHeight), "");
            
            // 功能开关
            ImGui::Checkbox("自动购买", &autoBuy);
            ImGui::Checkbox("自动刷新", &autoRefresh);
            
            // 按钮
            if (ImGui::Button("刷新")) {
                // 这里添加刷新功能
                printf("刷新按钮点击\n");
            }
            
            ImGui::End();
        }

        // 3. Show another simple window.
        if (showAnotherWindow)
        {
            ImGui::Begin("Another Window", &showAnotherWindow); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                showAnotherWindow = false;
            ImGui::End();
        }

        imgui.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (processInputEventThread.joinable())
        processInputEventThread.join();

    return 0;
}
