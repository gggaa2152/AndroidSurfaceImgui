#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"  // 必须添加这个头文件！

#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <cstdio>
#include <cmath>

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

// ========== 帧率计算 ==========
float g_currentFPS = 0.0f;
int g_frameCount = 0;
auto g_fpsTimer = std::chrono::high_resolution_clock::now();

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 一加/OPPO 系统字体
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",  // 一加/OPPO
        "/system/fonts/NotoSansCJK-Regular.ttc",   // Google
        "/system/fonts/DroidSansFallback.ttf",      // 备用
    };
    
    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        printf("[+] Trying font: %s\n", path);
        font = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
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
        printf("[+] Config saved\n");
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
        printf("[+] Config loaded\n");
    } else {
        printf("[-] No config file, using defaults\n");
    }
}

// ========== 自定义滑动开关（修复版） ==========
bool ToggleSwitch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    
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
    if (!ImGui::ItemAdd(total_bb, id))
        return false;
    
    // 背景
    float t = *v ? 1.0f : 0.0f;
    
    // 【修复】移除对 g.LastActiveIdTimer 的依赖，简化动画
    ImU32 col_bg = *v ? ImGui::GetColorU32(ImVec4(0.26f, 0.98f, 0.26f, 0.94f)) : ImGui::GetColorU32(ImVec4(0.76f, 0.76f, 0.76f, 0.94f));
    
    ImRect frame_bb(pos, ImVec2(pos.x + width, pos.y + height));
    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, col_bg, height * 0.5f);
    
    // 滑块（根据状态计算位置）
    float shift = t * (width - 2 * radius - 4);
    window->DrawList->AddCircleFilled(
        ImVec2(pos.x + radius + shift + (radius/2), pos.y + height/2), 
        radius-2, 
        IM_COL32(255, 255, 255, 255), 
        32
    );
    
    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, pos.y + (height - label_size.y) * 0.5f), label);
    }
    
    // 点击处理
    bool pressed = ImGui::ButtonBehavior(total_bb, id, NULL, NULL, ImGuiButtonFlags_PressedOnClick);
    if (pressed) {
        *v = !*v;
    }
    
    return pressed;
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    // 先创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    // 加载中文字体
    LoadChineseFont();
    
    android::AImGui imgui(android::AImGui::Options{.renderType = android::AImGui::RenderType::RenderNative, .autoUpdateOrientation = true});
    bool state = true, showDemoWindow = false, showAnotherWindow = false;
    ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);

    if (!imgui)
    {
        printf("[-] ImGui initialization failed\n");
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
    auto frameTimer = std::chrono::high_resolution_clock::now();
    g_fpsTimer = std::chrono::high_resolution_clock::now();
    
    printf("[2] Entering main loop\n");
    
    while (state)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        // 读取游戏数据
        ReadGameData();

        imgui.BeginFrame();

        // 计算帧率
        g_frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsedMs = std::chrono::duration<float, std::milli>(now - g_fpsTimer).count();
        if (elapsedMs >= 1000.0f) {
            g_currentFPS = g_frameCount * 1000.0f / elapsedMs;
            g_frameCount = 0;
            g_fpsTimer = now;
        }

        // 1. Show the big demo window
        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // ========== 金铲铲助手主窗口 ==========
        {
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            // ===== 帧率显示 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "📊 帧率: %.1f FPS", g_currentFPS);
            
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
            
            // ===== 功能开关（滑动开关） =====
            ImGui::TextColored(ImVec4(1,1,0,1), "🔧 功能设置");
            
            bool prevPredict = g_featurePredict;
            bool prevESP = g_featureESP;
            bool prevInstantQuit = g_featureInstantQuit;
            bool prevAutoBuy = autoBuy;
            bool prevAutoRefresh = autoRefresh;
            
            // 使用滑动开关（修复版）
            ToggleSwitch("预测", &g_featurePredict);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后预测敌方下一步行动");
            }
            
            ToggleSwitch("透视", &g_featureESP);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后显示敌方位置");
            }
            
            ToggleSwitch("秒退", &g_featureInstantQuit);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后快速退出对局");
            }
            
            ImGui::Separator();
            
            // ===== 游戏功能 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "🎮 游戏功能");
            
            ToggleSwitch("自动购买", &autoBuy);
            ToggleSwitch("自动刷新", &autoRefresh);
            
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
                printf("[+] Refresh button clicked\n");
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

    printf("[3] JCC Assistant exited\n");
    return 0;
}
