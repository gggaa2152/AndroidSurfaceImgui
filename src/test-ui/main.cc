#include "Global.h"
#include "AImGui.h"
#include <thread>
#include <cstdio>
#include <chrono>
#include <string>
#include <unistd.h>

// ========== 金铲铲助手数据 ==========
int gold = 100;
int level = 8;
int hp = 85;
bool autoBuy = true;
bool autoRefresh = true;

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;           // 全局缩放比例
const float MIN_SCALE = 0.5f;
const float MAX_SCALE = 3.0f;
bool g_showScaleSlider = true;        // 是否显示缩放滑块

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

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 一加/OPPO 系统字体
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    
    printf("[+] Loading font: %s\n", fontPath);
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    
    if (font) {
        printf("[+] Font loaded successfully\n");
        io.FontDefault = font;
    } else {
        printf("[-] Font loading failed, using default\n");
        io.Fonts->AddFontDefault();
    }
    
    io.Fonts->Build();
}

// ========== 保存配置 ==========
void SaveConfig() {
    printf("[+] Saving config to %s\n", CONFIG_PATH);
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "# JCC Assistant Config\n");
        fprintf(f, "scale=%.2f\n", g_globalScale);
        fprintf(f, "autoBuy=%d\n", autoBuy ? 1 : 0);
        fprintf(f, "autoRefresh=%d\n", autoRefresh ? 1 : 0);
        fprintf(f, "showScaleSlider=%d\n", g_showScaleSlider ? 1 : 0);
        fclose(f);
        printf("[+] Config saved\n");
    } else {
        printf("[-] Failed to save config\n");
    }
}

// ========== 加载配置 ==========
void LoadConfig() {
    printf("[+] Loading config from %s\n", CONFIG_PATH);
    FILE* f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            // 跳过注释
            if (line[0] == '#' || line[0] == '\n') continue;
            
            float fval;
            int ival;
            if (sscanf(line, "scale=%f", &fval) == 1) {
                g_globalScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
                printf("[+] Loaded scale: %.2f\n", g_globalScale);
            }
            else if (sscanf(line, "autoBuy=%d", &ival) == 1) {
                autoBuy = (ival != 0);
                printf("[+] Loaded autoBuy: %d\n", autoBuy);
            }
            else if (sscanf(line, "autoRefresh=%d", &ival) == 1) {
                autoRefresh = (ival != 0);
                printf("[+] Loaded autoRefresh: %d\n", autoRefresh);
            }
            else if (sscanf(line, "showScaleSlider=%d", &ival) == 1) {
                g_showScaleSlider = (ival != 0);
                printf("[+] Loaded showScaleSlider: %d\n", g_showScaleSlider);
            }
        }
        fclose(f);
        
        // 应用加载的缩放
        ImGui::GetIO().FontGlobalScale = g_globalScale;
        printf("[+] Config loaded successfully\n");
    } else {
        printf("[-] No config file found, using defaults\n");
        // 创建默认配置
        SaveConfig();
    }
}

// ========== 重置为默认值 ==========
void ResetToDefault() {
    g_globalScale = 1.0f;
    autoBuy = true;
    autoRefresh = true;
    g_showScaleSlider = true;
    
    ImGui::GetIO().FontGlobalScale = g_globalScale;
    SaveConfig();
    printf("[+] Reset to default values\n");
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    // 先创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    // 加载字体
    LoadChineseFont();
    
    // 创建窗口
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui)
    {
        printf("[-] ImGui initialization failed\n");
        return 0;
    }
    
    printf("[2] AImGui created\n");

    // 加载配置
    LoadConfig();

    bool state = true, showDemoWindow = false, showAnotherWindow = false;
    ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);

    std::thread inputThread([&] {
        while (state) {
            imgui.ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    });

    printf("[3] Entering main loop\n");
    
    while (state)
    {
        // 读取游戏数据
        ReadGameData();

        imgui.BeginFrame();

        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // ========== 主窗口 ==========
        {
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            // ===== 全局缩放控制滑块 =====
            if (g_showScaleSlider) {
                ImGui::TextColored(ImVec4(0,1,1,1), "⚙️ 全局缩放控制");
                
                float prevScale = g_globalScale;
                if (ImGui::SliderFloat("缩放比例", &g_globalScale, MIN_SCALE, MAX_SCALE, "%.2f")) {
                    // 当滑块变化时，应用全局缩放
                    ImGui::GetIO().FontGlobalScale = g_globalScale;
                }
                ImGui::SameLine();
                ImGui::Text("(%.0f%%)", g_globalScale * 100);
                
                // 快捷缩放按钮
                if (ImGui::Button("0.5x")) { g_globalScale = 0.5f; ImGui::GetIO().FontGlobalScale = g_globalScale; }
                ImGui::SameLine();
                if (ImGui::Button("1.0x")) { g_globalScale = 1.0f; ImGui::GetIO().FontGlobalScale = g_globalScale; }
                ImGui::SameLine();
                if (ImGui::Button("1.5x")) { g_globalScale = 1.5f; ImGui::GetIO().FontGlobalScale = g_globalScale; }
                ImGui::SameLine();
                if (ImGui::Button("2.0x")) { g_globalScale = 2.0f; ImGui::GetIO().FontGlobalScale = g_globalScale; }
                
                // 如果缩放有变化，保存配置
                if (prevScale != g_globalScale) {
                    SaveConfig();
                }
                
                ImGui::Separator();
            }
            
            // ===== 游戏数据显示 =====
            ImGui::TextColored(ImVec4(1,1,0,1), "💰 金币: %d", gold);
            ImGui::TextColored(ImVec4(0,1,0,1), "📊 等级: %d", level);
            ImGui::TextColored(ImVec4(1,0,0,1), "❤️ 血量: %d", hp);
            
            // 进度条（受全局缩放影响）
            float progressWidth = 200.0f * g_globalScale;
            float progressHeight = 20.0f * g_globalScale;
            ImGui::ProgressBar(hp/100.0f, ImVec2(progressWidth, progressHeight), "");
            
            ImGui::Separator();
            
            // ===== 功能开关 =====
            bool prevAutoBuy = autoBuy;
            bool prevAutoRefresh = autoRefresh;
            
            ImGui::Checkbox("🛒 自动购买", &autoBuy);
            ImGui::Checkbox("🔄 自动刷新", &autoRefresh);
            
            // 如果开关状态有变化，保存配置
            if (prevAutoBuy != autoBuy || prevAutoRefresh != autoRefresh) {
                SaveConfig();
            }
            
            // ===== 按钮 =====
            if (ImGui::Button("🔄 刷新", ImVec2(120 * g_globalScale, 0))) {
                printf("刷新按钮点击\n");
            }
            
            ImGui::SameLine();
            if (ImGui::Button("⚙️ 配置", ImVec2(120 * g_globalScale, 0))) {
                // 打开配置菜单
                ImGui::OpenPopup("配置菜单");
            }
            
            // ===== 配置菜单弹出窗口 =====
            if (ImGui::BeginPopup("配置菜单")) {
                ImGui::Text("配置选项");
                ImGui::Separator();
                
                ImGui::Checkbox("显示缩放滑块", &g_showScaleSlider);
                
                if (ImGui::Button("保存配置")) {
                    SaveConfig();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("重置默认")) {
                    ResetToDefault();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("取消")) {
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::End();
        }

        if (showAnotherWindow)
        {
            ImGui::Begin("Another Window", &showAnotherWindow);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                showAnotherWindow = false;
            ImGui::End();
        }

        imgui.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 退出前保存配置
    SaveConfig();

    if (inputThread.joinable())
        inputThread.join();

    printf("[4] JCC Assistant exited\n");
    return 0;
}
