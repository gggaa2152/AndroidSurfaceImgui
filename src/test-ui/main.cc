#include "Global.h"
#include "AImGui.h"

#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <cstdio>

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

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;
const float MIN_SCALE = 0.5f;
const float MAX_SCALE = 3.0f;

// ========== 配置文件路径 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

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

// ========== 保存配置 ==========
void SaveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "# JCC Assistant Config\n");
        fprintf(f, "scale=%.2f\n", g_globalScale);
        fprintf(f, "predict=%d\n", g_featurePredict ? 1 : 0);
        fprintf(f, "esp=%d\n", g_featureESP ? 1 : 0);
        fprintf(f, "instantQuit=%d\n", g_featureInstantQuit ? 1 : 0);
        fprintf(f, "autoBuy=%d\n", autoBuy ? 1 : 0);
        fprintf(f, "autoRefresh=%d\n", autoRefresh ? 1 : 0);
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
            
            float fval;
            int ival;
            if (sscanf(line, "scale=%f", &fval) == 1) {
                g_globalScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
            }
            else if (sscanf(line, "predict=%d", &ival) == 1) {
                g_featurePredict = (ival != 0);
            }
            else if (sscanf(line, "esp=%d", &ival) == 1) {
                g_featureESP = (ival != 0);
            }
            else if (sscanf(line, "instantQuit=%d", &ival) == 1) {
                g_featureInstantQuit = (ival != 0);
            }
            else if (sscanf(line, "autoBuy=%d", &ival) == 1) {
                autoBuy = (ival != 0);
            }
            else if (sscanf(line, "autoRefresh=%d", &ival) == 1) {
                autoRefresh = (ival != 0);
            }
        }
        fclose(f);
        
        // 应用加载的缩放
        ImGui::GetIO().FontGlobalScale = g_globalScale;
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

    // 加载配置
    LoadConfig();

    std::thread processInputEventThread(
        [&]
        {
            while (state)
            {
                imgui.ProcessInputEvent();
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });

    const float TARGET_FPS = 120.0f;
    const float TARGET_FRAME_TIME_MS = 1000.0f / TARGET_FPS;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();
    
    while (state)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        // 读取游戏数据
        ReadGameData();

        imgui.BeginFrame();

        // 1. Show the big demo window
        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // ========== 金铲铲助手主窗口 ==========
        {
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            // ===== 全局缩放控制 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "⚙️ 全局缩放");
            
            float prevScale = g_globalScale;
            if (ImGui::SliderFloat("缩放", &g_globalScale, MIN_SCALE, MAX_SCALE, "%.2f")) {
                ImGui::GetIO().FontGlobalScale = g_globalScale;
            }
            ImGui::SameLine();
            ImGui::Text("(%.0f%%)", g_globalScale * 100);
            
            if (prevScale != g_globalScale) {
                SaveConfig();
            }
            
            ImGui::Separator();
            
            // ===== 功能开关 =====
            ImGui::TextColored(ImVec4(1,1,0,1), "🔧 功能设置");
            
            bool prevPredict = g_featurePredict;
            bool prevESP = g_featureESP;
            bool prevInstantQuit = g_featureInstantQuit;
            bool prevAutoBuy = autoBuy;
            bool prevAutoRefresh = autoRefresh;
            
            // 1. 预测开关
            ImGui::Checkbox("1. 预测", &g_featurePredict);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后预测敌方下一步行动");
            }
            
            // 2. 透视开关
            ImGui::Checkbox("2. 透视", &g_featureESP);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后显示敌方位置");
            }
            
            // 3. 秒退开关
            ImGui::Checkbox("3. 秒退", &g_featureInstantQuit);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后快速退出对局");
            }
            
            ImGui::Separator();
            
            // ===== 游戏功能 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "🎮 游戏功能");
            
            ImGui::Checkbox("自动购买", &autoBuy);
            ImGui::Checkbox("自动刷新", &autoRefresh);
            
            // ===== 游戏数据 =====
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1,1,0,1), "💰 金币: %d", gold);
            ImGui::TextColored(ImVec4(0,1,0,1), "📊 等级: %d", level);
            ImGui::TextColored(ImVec4(1,0,0,1), "❤️ 血量: %d", hp);
            
            // 进度条
            float progressWidth = 200.0f * g_globalScale;
            float progressHeight = 20.0f * g_globalScale;
            ImGui::ProgressBar(hp/100.0f, ImVec2(progressWidth, progressHeight), "");
            
            // 按钮
            if (ImGui::Button("刷新", ImVec2(100 * g_globalScale, 0))) {
                LogInfo("[+] Refresh button clicked");
            }
            
            // ===== 当前功能状态 =====
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0,1,1,1), "📋 当前状态");
            ImGui::Text("预测: %s", g_featurePredict ? "✅开启" : "❌关闭");
            ImGui::Text("透视: %s", g_featureESP ? "✅开启" : "❌关闭");
            ImGui::Text("秒退: %s", g_featureInstantQuit ? "✅开启" : "❌关闭");
            ImGui::Text("自动购买: %s", autoBuy ? "✅开启" : "❌关闭");
            ImGui::Text("自动刷新: %s", autoRefresh ? "✅开启" : "❌关闭");
            
            // 如果有变化就保存
            if (prevPredict != g_featurePredict || 
                prevESP != g_featureESP || 
                prevInstantQuit != g_featureInstantQuit ||
                prevAutoBuy != autoBuy ||
                prevAutoRefresh != autoRefresh) {
                SaveConfig();
            }
            
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
        
        // 帧率控制
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        
        if (frameTime < TARGET_FRAME_TIME_MS) {
            int sleepUs = (int)((TARGET_FRAME_TIME_MS - frameTime) * 1000);
            if (sleepUs > 0) {
                usleep(sleepUs);
            }
        }
    }

    if (processInputEventThread.joinable())
        processInputEventThread.join();

    // 退出前保存配置
    SaveConfig();

    return 0;
}
