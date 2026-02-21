#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <map>
#include <vector>

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

// ========== 棋盘设置 ==========
bool g_showChessboard = false;      // 是否显示棋盘
const int CHESSBOARD_ROWS = 8;       // 8行
const int CHESSBOARD_COLS = 7;       // 7列
float g_chessboardScale = 1.0f;      // 棋盘缩放
float g_chessboardPosX = 200;        // 棋盘位置X
float g_chessboardPosY = 200;        // 棋盘位置Y
bool g_chessboardDragging = false;   // 是否正在拖动棋盘

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;
const float MIN_SCALE = 0.5f;
const float MAX_SCALE = 5.0f;

// ========== 窗口位置和大小 ==========
ImVec2 g_windowPos = ImVec2(100, 100);
ImVec2 g_windowSize = ImVec2(400, 300);
bool g_windowPosInitialized = false;

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
        fprintf(f, "version=1.0\n");
        fprintf(f, "scale=%.2f\n", g_globalScale);
        fprintf(f, "predict=%d\n", g_featurePredict ? 1 : 0);
        fprintf(f, "esp=%d\n", g_featureESP ? 1 : 0);
        fprintf(f, "instantQuit=%d\n", g_featureInstantQuit ? 1 : 0);
        fprintf(f, "autoBuy=%d\n", autoBuy ? 1 : 0);
        fprintf(f, "autoRefresh=%d\n", autoRefresh ? 1 : 0);
        fprintf(f, "windowPosX=%.0f\n", g_windowPos.x);
        fprintf(f, "windowPosY=%.0f\n", g_windowPos.y);
        fprintf(f, "windowWidth=%.0f\n", g_windowSize.x);
        fprintf(f, "windowHeight=%.0f\n", g_windowSize.y);
        fprintf(f, "chessboardScale=%.2f\n", g_chessboardScale);
        fprintf(f, "chessboardPosX=%.0f\n", g_chessboardPosX);
        fprintf(f, "chessboardPosY=%.0f\n", g_chessboardPosY);
        fclose(f);
        printf("[+] Config saved (scale=%.2f)\n", g_globalScale);
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
            else if (sscanf(line, "windowPosX=%f", &fval) == 1) {
                g_windowPos.x = fval;
            }
            else if (sscanf(line, "windowPosY=%f", &fval) == 1) {
                g_windowPos.y = fval;
            }
            else if (sscanf(line, "windowWidth=%f", &fval) == 1) {
                g_windowSize.x = fval;
            }
            else if (sscanf(line, "windowHeight=%f", &fval) == 1) {
                g_windowSize.y = fval;
            }
            else if (sscanf(line, "chessboardScale=%f", &fval) == 1) {
                g_chessboardScale = fval;
            }
            else if (sscanf(line, "chessboardPosX=%f", &fval) == 1) {
                g_chessboardPosX = fval;
            }
            else if (sscanf(line, "chessboardPosY=%f", &fval) == 1) {
                g_chessboardPosY = fval;
            }
        }
        fclose(f);
        
        ImGui::GetIO().FontGlobalScale = g_globalScale;
        g_windowPosInitialized = true;
        printf("[+] Config loaded (scale=%.2f)\n", g_globalScale);
    } else {
        // 首次运行，创建默认配置
        printf("[-] No config file, using defaults\n");
        SaveConfig();
    }
}

// ========== 精美滑动开关（正常大小） ==========
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

// ========== 绘制棋盘 ==========
void DrawChessboard() {
    if (!g_featureESP && !g_showChessboard) return;
    
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    float cellSize = 40 * g_chessboardScale;
    float boardWidth = CHESSBOARD_COLS * cellSize;
    float boardHeight = CHESSBOARD_ROWS * cellSize;
    
    ImVec2 mousePos = io.MousePos;
    bool mouseInBoard = (mousePos.x >= g_chessboardPosX && mousePos.x <= g_chessboardPosX + boardWidth &&
                         mousePos.y >= g_chessboardPosY && mousePos.y <= g_chessboardPosY + boardHeight);
    
    if (io.MouseDown[0]) {
        if (!g_chessboardDragging && mouseInBoard) {
            g_chessboardDragging = true;
        }
        if (g_chessboardDragging) {
            g_chessboardPosX = mousePos.x - boardWidth/2;
            g_chessboardPosY = mousePos.y - boardHeight/2;
        }
    } else {
        if (g_chessboardDragging) {
            g_chessboardDragging = false;
            SaveConfig();
        }
    }
    
    drawList->AddRectFilled(
        ImVec2(g_chessboardPosX, g_chessboardPosY),
        ImVec2(g_chessboardPosX + boardWidth, g_chessboardPosY + boardHeight),
        IM_COL32(30, 30, 30, 100),
        10.0f
    );
    
    for (int row = 0; row <= CHESSBOARD_ROWS; row++) {
        float y = g_chessboardPosY + row * cellSize;
        drawList->AddLine(
            ImVec2(g_chessboardPosX, y),
            ImVec2(g_chessboardPosX + boardWidth, y),
            IM_COL32(100, 100, 100, 200),
            1.0f
        );
    }
    
    for (int col = 0; col <= CHESSBOARD_COLS; col++) {
        float x = g_chessboardPosX + col * cellSize;
        drawList->AddLine(
            ImVec2(x, g_chessboardPosY),
            ImVec2(x, g_chessboardPosY + boardHeight),
            IM_COL32(100, 100, 100, 200),
            1.0f
        );
    }
    
    for (int row = 0; row < CHESSBOARD_ROWS; row++) {
        for (int col = 0; col < CHESSBOARD_COLS; col++) {
            float centerX = g_chessboardPosX + col * cellSize + cellSize/2;
            float centerY = g_chessboardPosY + row * cellSize + cellSize/2;
            
            ImU32 circleColor = ((row + col) % 2 == 0) ? 
                IM_COL32(255, 100, 100, 200) : IM_COL32(100, 255, 100, 200);
            
            drawList->AddCircleFilled(
                ImVec2(centerX, centerY),
                cellSize * 0.3f,
                circleColor,
                16
            );
            
            drawList->AddCircle(
                ImVec2(centerX, centerY),
                cellSize * 0.3f,
                IM_COL32(255, 255, 255, 200),
                16,
                1.0f
            );
        }
    }
}

// ========== 自定义窗口缩放回调 ==========
void ScaleWindow(ImGuiSizeCallbackData* data) {
    float newWidth = data->DesiredSize.x;
    float scaleDelta = newWidth / 400.0f;
    if (scaleDelta < MIN_SCALE) scaleDelta = MIN_SCALE;
    if (scaleDelta > MAX_SCALE) scaleDelta = MAX_SCALE;
    
    g_globalScale = scaleDelta;
    ImGui::GetIO().FontGlobalScale = g_globalScale;
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    
    // 禁止穿透点击
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    
    ImGuiStyle& style = ImGui::GetStyle();
    
    // ===== 超大右下角缩放区域 =====
    style.GrabMinSize = 40.0f;           // 增大到40px，更容易点到
    style.FramePadding = ImVec2(10, 8);  // 增大内边距
    style.WindowPadding = ImVec2(12, 12);
    
    // 让窗口底部区域更大，更容易点到缩放柄
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    
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
    g_fpsTimer = std::chrono::high_resolution_clock::now();
    
    printf("[2] Entering main loop\n");
    
    auto lastSaveTime = std::chrono::high_resolution_clock::now();
    
    while (state)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        ReadGameData();

        imgui.BeginFrame();

        DrawChessboard();

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

        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * g_globalScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * g_globalScale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.2f, 0.6f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.6f, 1.0f));
            
            // 设置窗口大小回调，右下角三角更容易点到
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(200, 150),                      // 最小尺寸
                ImVec2(FLT_MAX, FLT_MAX),              // 无最大限制
                ScaleWindow,                            // 回调函数
                nullptr
            );
            
            if (g_windowPosInitialized) {
                ImGui::SetNextWindowPos(g_windowPos, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(g_windowSize, ImGuiCond_FirstUseEver);
            }
            
            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);
            
            ImVec2 currentPos = ImGui::GetWindowPos();
            ImVec2 currentSize = ImGui::GetWindowSize();
            
            bool posChanged = (currentPos.x != g_windowPos.x || currentPos.y != g_windowPos.y);
            bool sizeChanged = (currentSize.x != g_windowSize.x || currentSize.y != g_windowSize.y);
            
            if (posChanged || sizeChanged) {
                g_windowPos = currentPos;
                g_windowSize = currentSize;
            }
            
            ImGui::Separator();
            
            // 信息栏
            ImGui::Columns(2, "info", false);
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "FPS: %.0f", g_currentFPS);
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "缩放: %.2fx", g_globalScale);
            ImGui::Columns(1);
            
            ImGui::Separator();
            
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
                ImGui::SetTooltip("开启后显示棋盘");
            }
            
            ToggleSwitch("秒退", &g_featureInstantQuit, 2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后快速退出对局");
            }
            
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "游戏功能");
            
            ToggleSwitch("自动购买", &autoBuy, 3);
            ToggleSwitch("自动刷新", &autoRefresh, 4);
            
            if (g_featureESP) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "棋盘设置");
                if (ImGui::SliderFloat("棋盘缩放", &g_chessboardScale, 0.5f, 2.0f, "%.1f")) {}
                ImGui::Text("拖动棋盘可移动位置");
            }
            
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "当前状态");
            ImGui::Text("预测: %s", g_featurePredict ? "开启" : "关闭");
            ImGui::Text("透视: %s", g_featureESP ? "开启" : "关闭");
            ImGui::Text("秒退: %s", g_featureInstantQuit ? "开启" : "关闭");
            ImGui::Text("自动购买: %s", autoBuy ? "开启" : "关闭");
            ImGui::Text("自动刷新: %s", autoRefresh ? "开启" : "关闭");
            
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
            
            auto currentTime = std::chrono::high_resolution_clock::now();
            float timeSinceLastSave = std::chrono::duration<float>(currentTime - lastSaveTime).count();
            
            bool switchesChanged = (prevPredict != g_featurePredict || 
                                   prevESP != g_featureESP || 
                                   prevInstantQuit != g_featureInstantQuit ||
                                   prevAutoBuy != autoBuy ||
                                   prevAutoRefresh != autoRefresh);
            bool windowMoved = posChanged || sizeChanged;
            
            if ((switchesChanged || windowMoved) && timeSinceLastSave > 2.0f) {
                SaveConfig();
                lastSaveTime = currentTime;
            }
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

    SaveConfig();
    printf("[3] JCC Assistant exited\n");
    return 0;
}
