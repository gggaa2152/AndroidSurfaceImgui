#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

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
float g_targetScale = 1.0f;
const float MIN_SCALE = 0.5f;
const float MAX_SCALE = 3.0f;

// ========== 配置文件路径 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

// ========== 帧率计算 ==========
float g_currentFPS = 0.0f;
int g_frameCount = 0;
auto g_fpsTimer = std::chrono::high_resolution_clock::now();

// ========== 动画变量 ==========
float g_toggleAnimProgress[10] = {0};
int g_toggleAnimTarget[10] = {0};

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
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
                g_targetScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
            }
            else if (sscanf(line, "predict=%d", &ival) == 1) {
                g_featurePredict = (ival != 0);
                g_toggleAnimTarget[0] = g_featurePredict ? 1 : 0;
            }
            else if (sscanf(line, "esp=%d", &ival) == 1) {
                g_featureESP = (ival != 0);
                g_toggleAnimTarget[1] = g_featureESP ? 1 : 0;
            }
            else if (sscanf(line, "instantQuit=%d", &ival) == 1) {
                g_featureInstantQuit = (ival != 0);
                g_toggleAnimTarget[2] = g_featureInstantQuit ? 1 : 0;
            }
            else if (sscanf(line, "autoBuy=%d", &ival) == 1) {
                autoBuy = (ival != 0);
                g_toggleAnimTarget[3] = autoBuy ? 1 : 0;
            }
            else if (sscanf(line, "autoRefresh=%d", &ival) == 1) {
                autoRefresh = (ival != 0);
                g_toggleAnimTarget[4] = autoRefresh ? 1 : 0;
            }
        }
        fclose(f);
        
        ImGui::GetIO().FontGlobalScale = g_globalScale;
        printf("[+] Config loaded\n");
    } else {
        // 首次运行，创建默认配置
        SaveConfig();
    }
}

// ========== 精美滑动开关（带动画） ==========
bool ToggleSwitch(const char* label, bool* v, int animIdx) {
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
    
    float target = (*v ? 1.0f : 0.0f);
    g_toggleAnimTarget[animIdx] = target;
    
    float& progress = g_toggleAnimProgress[animIdx];
    float speed = 0.15f;
    progress += (target - progress) * speed;
    if (fabs(progress - target) < 0.01f) progress = target;
    
    ImVec4 bgColorOn(0.2f, 0.8f, 0.3f, 0.9f);
    ImVec4 bgColorOff(0.3f, 0.3f, 0.3f, 0.9f);
    
    ImVec4 currentBgColor(
        bgColorOff.x + (bgColorOn.x - bgColorOff.x) * progress,
        bgColorOff.y + (bgColorOn.y - bgColorOff.y) * progress,
        bgColorOff.z + (bgColorOn.z - bgColorOff.z) * progress,
        bgColorOff.w + (bgColorOn.w - bgColorOff.w) * progress
    );
    
    ImRect frame_bb(pos, ImVec2(pos.x + width, pos.y + height));
    
    window->DrawList->AddRectFilled(
        frame_bb.Min, frame_bb.Max,
        ImGui::GetColorU32(currentBgColor),
        height * 0.5f
    );
    
    float shift = progress * (width - 2 * radius - 4);
    ImVec2 thumbCenter(
        pos.x + radius + shift + (radius/2),
        pos.y + height/2
    );
    
    window->DrawList->AddCircleFilled(
        thumbCenter,
        radius - 1,
        IM_COL32(255, 255, 255, 255),
        32
    );
    
    window->DrawList->AddCircle(
        thumbCenter,
        radius - 3,
        IM_COL32(200, 200, 200, 100),
        32,
        1.0f
    );
    
    if (label_size.x > 0.0f) {
        ImGui::RenderText(
            ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, pos.y + (height - label_size.y) * 0.5f),
            label
        );
    }
    
    bool pressed = ImGui::ButtonBehavior(total_bb, id, NULL, NULL, ImGuiButtonFlags_PressedOnClick);
    if (pressed) {
        *v = !*v;
        g_toggleAnimTarget[animIdx] = *v ? 1 : 0;
    }
    
    return pressed;
}

// ========== 自定义窗口缩放回调（右下角三角控制全局缩放） ==========
void ScaleWindow(ImGuiSizeCallbackData* data) {
    // 当用户拖动右下角三角时，更新全局缩放
    float newWidth = data->DesiredSize.x;
    
    // 根据宽度变化计算缩放比例
    float scaleDelta = newWidth / 400.0f; // 基准宽度400px
    if (scaleDelta < MIN_SCALE) scaleDelta = MIN_SCALE;
    if (scaleDelta > MAX_SCALE) scaleDelta = MAX_SCALE;
    
    g_targetScale = scaleDelta;
    g_globalScale = scaleDelta; // 直接更新，让滑块同步
    
    // 立即应用缩放
    ImGui::GetIO().FontGlobalScale = g_globalScale;
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    LoadChineseFont();
    
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });
    
    bool state = true, showDemoWindow = false, showAnotherWindow = false;
    ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);

    if (!imgui)
    {
        printf("[-] ImGui initialization failed\n");
        return 0;
    }

    // 加载配置
    LoadConfig();
    g_targetScale = g_globalScale;

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

        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // ========== 金铲铲助手主窗口 ==========
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.2f, 0.6f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.6f, 1.0f));
            
            // 【修改】移除窗口大小限制，让用户可以自由调整
            // 但仍然保留回调函数，使右下角三角控制全局缩放
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(0, 0),                          // 无最小限制
                ImVec2(FLT_MAX, FLT_MAX),              // 无最大限制
                ScaleWindow,                            // 回调函数
                nullptr
            );
            
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            // 【修复】直接使用g_targetScale更新，不用平滑过渡，让滑块立即响应
            if (g_targetScale != g_globalScale) {
                g_globalScale = g_targetScale;
                ImGui::GetIO().FontGlobalScale = g_globalScale;
            }
            
            ImGui::Separator();
            
            // 信息栏
            ImGui::Columns(2, "info", false);
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "FPS: %.0f", g_currentFPS);
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "缩放: %.2fx", g_globalScale);
            ImGui::Columns(1);
            
            ImGui::Separator();
            
            // 【修复】滑块控制 - 现在可以正常工作了
            float prevScale = g_globalScale;
            if (ImGui::SliderFloat("全局缩放", &g_targetScale, MIN_SCALE, MAX_SCALE, "%.2f")) {
                // 滑块改变目标缩放
                g_globalScale = g_targetScale;
                ImGui::GetIO().FontGlobalScale = g_globalScale;
            }
            
            // 缩放变化时保存配置
            if (fabs(prevScale - g_globalScale) > 0.01f) {
                SaveConfig();
            }
            
            ImGui::Separator();
            
            // 功能设置
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "功能设置");
            
            bool prevPredict = g_featurePredict;
            bool prevESP = g_featureESP;
            bool prevInstantQuit = g_featureInstantQuit;
            bool prevAutoBuy = autoBuy;
            bool prevAutoRefresh = autoRefresh;
            
            ToggleSwitch("预测", &g_featurePredict, 0);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后预测敌方下一步行动");
            }
            
            ToggleSwitch("透视", &g_featureESP, 1);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后显示敌方位置");
            }
            
            ToggleSwitch("秒退", &g_featureInstantQuit, 2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后快速退出对局");
            }
            
            ImGui::Separator();
            
            // 游戏功能
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "游戏功能");
            
            ToggleSwitch("自动购买", &autoBuy, 3);
            ToggleSwitch("自动刷新", &autoRefresh, 4);
            
            ImGui::Separator();
            
            // 当前状态显示
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "当前状态");
            ImGui::Text("预测: %s", g_featurePredict ? "开启" : "关闭");
            ImGui::Text("透视: %s", g_featureESP ? "开启" : "关闭");
            ImGui::Text("秒退: %s", g_featureInstantQuit ? "开启" : "关闭");
            ImGui::Text("自动购买: %s", autoBuy ? "开启" : "关闭");
            ImGui::Text("自动刷新: %s", autoRefresh ? "开启" : "关闭");
            
            // 开关状态变化时保存配置
            if (prevPredict != g_featurePredict || 
                prevESP != g_featureESP || 
                prevInstantQuit != g_featureInstantQuit ||
                prevAutoBuy != autoBuy ||
                prevAutoRefresh != autoRefresh) {
                SaveConfig();
            }
            
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        }

        if (showAnotherWindow)
        {
            ImGui::Begin("另一个窗口", &showAnotherWindow);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("关闭"))
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
