#include "Global.h"
#include "AImGui.h"
#include <thread>
#include <cstdio>
#include <chrono>

// ========== 金铲铲助手数据 ==========
int gold = 100;
int level = 8;
int hp = 85;
bool autoBuy = true;
bool autoRefresh = true;
bool autoScaleEnabled = true;
float manualScale = 1.0f;
ImVec2 baseSize(400, 300);
bool firstTime = true;

void ReadGameData() {
    FILE* f = fopen("/data/local/tmp/game_data.txt", "r");
    if (f) {
        fscanf(f, "gold=%d\n", &gold);
        fscanf(f, "level=%d\n", &level);
        fscanf(f, "hp=%d\n", &hp);
        fclose(f);
    }
}

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

int main()
{
    printf("[1] Starting...\n");
    
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
        ReadGameData();

        imgui.BeginFrame();

        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // 金铲铲助手窗口
        {
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            ImVec2 currentSize = ImGui::GetWindowSize();
            
            if (firstTime) {
                baseSize = currentSize;
                firstTime = false;
            }
            
            float scaleX = currentSize.x / baseSize.x;
            float scaleY = currentSize.y / baseSize.y;
            float newScale = (scaleX > scaleY ? scaleX : scaleY);
            
            const float MIN_SCALE = 0.5f;
            const float MAX_SCALE = 5.0f;
            if (newScale < MIN_SCALE) newScale = MIN_SCALE;
            if (newScale > MAX_SCALE) newScale = MAX_SCALE;
            
            ImGui::Checkbox("自动缩放", &autoScaleEnabled);
            
            float effectiveScale;
            if (autoScaleEnabled) {
                effectiveScale = newScale;
                ImGui::Text("当前缩放: %.2f", effectiveScale);
            } else {
                ImGui::SliderFloat("缩放比例", &manualScale, MIN_SCALE, MAX_SCALE, "%.2f");
                effectiveScale = manualScale;
            }
            
            ImGui::GetIO().FontGlobalScale = effectiveScale;
            ImGui::Separator();

            ImGui::Text("金币: %d", gold);
            ImGui::Text("等级: %d", level);
            ImGui::Text("血量: %d", hp);
            
            float progressWidth = 200.0f * effectiveScale;
            float progressHeight = 20.0f * effectiveScale;
            ImGui::ProgressBar(hp/100.0f, ImVec2(progressWidth, progressHeight), "");
            
            ImGui::Checkbox("自动购买", &autoBuy);
            ImGui::Checkbox("自动刷新", &autoRefresh);
            
            if (ImGui::Button("刷新")) {
                printf("刷新按钮点击\n");
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

    if (inputThread.joinable())
        inputThread.join();

    return 0;
}
