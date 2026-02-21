#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

// 系统头文件
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sstream>
#include <thread>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <cstring>

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
const int CHESSBOARD_ROWS = 4;       // 4行
const int CHESSBOARD_COLS = 7;       // 7列
float g_chessboardScale = 1.0f;
float g_chessboardPosX = 200;
float g_chessboardPosY = 200;
bool g_chessboardDragging = false;

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;
const float MIN_SCALE = 0.3f;
const float MAX_SCALE = 5.0f;

// ========== 窗口位置和大小 ==========
ImVec2 g_windowPos = ImVec2(50, 100);
ImVec2 g_windowSize = ImVec2(280, 400);
bool g_windowPosInitialized = false;

// ========== 共享内存结构 ==========
struct SharedGameData {
    int32_t gold;
    int32_t level;
    int32_t hp;
    int32_t autoBuy;
    int32_t autoRefresh;
    int32_t timestamp;
    int32_t version;
    char scriptName[64];
};

// ========== 共享内存变量 ==========
int g_shm_fd = -1;
SharedGameData* g_sharedData = nullptr;
int g_lastTimestamp = 0;
bool g_shmValid = false;

// ========== 安全读取共享内存 ==========
template<typename T>
T safe_read(volatile T* ptr, const T& default_val = T()) {
    if (!g_shmValid || !ptr) return default_val;
    
    T val;
    // 使用 memcpy 安全读取
    memcpy(&val, (void*)ptr, sizeof(T));
    return val;
}

// ========== 初始化共享内存 ==========
bool InitSharedMemory() {
    printf("[+] Initializing shared memory...\n");
    
    // 先检查文件是否存在
    struct stat st;
    if (stat("/data/local/tmp/jcc_shared_mem", &st) != 0) {
        printf("[-] Shared memory file not found\n");
        return false;
    }
    
    printf("[+] File size: %ld bytes (expected: %lu)\n", st.st_size, sizeof(SharedGameData));
    
    if (st.st_size < (off_t)sizeof(SharedGameData)) {
        printf("[-] File too small\n");
        return false;
    }
    
    // 以读写方式打开
    g_shm_fd = open("/data/local/tmp/jcc_shared_mem", O_RDWR);
    if (g_shm_fd < 0) {
        printf("[-] Failed to open: %s\n", strerror(errno));
        return false;
    }
    
    // 映射内存
    g_sharedData = (SharedGameData*)mmap(NULL, sizeof(SharedGameData),
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, g_shm_fd, 0);
    
    if (g_sharedData == MAP_FAILED) {
        printf("[-] Failed to map: %s\n", strerror(errno));
        close(g_shm_fd);
        g_sharedData = nullptr;
        return false;
    }
    
    g_shmValid = true;
    
    // 测试读取
    printf("[+] Testing read...\n");
    int test_gold = g_sharedData->gold;
    int test_level = g_sharedData->level;
    int test_hp = g_sharedData->hp;
    int test_ts = g_sharedData->timestamp;
    
    printf("[+] Read test: gold=%d, level=%d, hp=%d, ts=%d\n", 
           test_gold, test_level, test_hp, test_ts);
    
    if (test_gold >= 0 && test_level >= 0 && test_hp >= 0) {
        gold = test_gold;
        level = test_level;
        hp = test_hp;
        autoBuy = (g_sharedData->autoBuy != 0);
        autoRefresh = (g_sharedData->autoRefresh != 0);
        g_lastTimestamp = test_ts;
        
        printf("[+] Initial values loaded: gold=%d, level=%d, hp=%d\n", gold, level, hp);
    } else {
        printf("[-] Read test failed, using defaults\n");
    }
    
    return true;
}

// ========== 从共享内存读取数据 ==========
void ReadFromSharedMemory() {
    if (!g_shmValid || !g_sharedData) {
        static int count = 0;
        if (++count % 120 == 0) {
            printf("[DEBUG] SHM not valid: valid=%d, ptr=%p\n", 
                   g_shmValid, g_sharedData);
        }
        return;
    }
    
    int ts = g_sharedData->timestamp;
    if (ts < 0) return;
    
    static int last_print = 0;
    if (++last_print % 60 == 0) {  // 每秒打印一次
        printf("[DEBUG] SHM status: ts=%d, gold=%d, level=%d, hp=%d\n", 
               ts, g_sharedData->gold, g_sharedData->level, g_sharedData->hp);
    }
    
    if (ts != g_lastTimestamp && ts > 0) {
        int new_gold = g_sharedData->gold;
        int new_level = g_sharedData->level;
        int new_hp = g_sharedData->hp;
        
        if (new_gold != gold || new_level != level || new_hp != hp) {
            gold = new_gold;
            level = new_level;
            hp = new_hp;
            autoBuy = (g_sharedData->autoBuy != 0);
            autoRefresh = (g_sharedData->autoRefresh != 0);
            
            g_lastTimestamp = ts;
            
            printf("\033[32m[UPDATE] gold=%d, level=%d, hp=%d, ts=%d\033[0m\n", 
                   gold, level, hp, ts);
        }
    }
}

// ========== 清理共享内存 ==========
void CleanupSharedMemory() {
    g_shmValid = false;
    if (g_sharedData) {
        munmap(g_sharedData, sizeof(SharedGameData));
        g_sharedData = nullptr;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
}

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    printf("[+] Loading Chinese font...\n");
    
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    
    ImFont* font = nullptr;
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    
    for (const char* path : fontPaths) {
        printf("[+] Trying font: %s\n", path);
        font = io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
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
    printf("[+] Font loaded successfully\n");
}

// ========== 配置文件路径 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

// ========== 帧率计算 ==========
float g_currentFPS = 0.0f;
int g_frameCount = 0;
auto g_fpsTimer = std::chrono::high_resolution_clock::now();

// ========== 动画变量 ==========
float g_toggleAnimProgress[10] = {0};
int g_toggleAnimTarget[10] = {0};

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
        fprintf(f, "windowPosX=%.0f\n", g_windowPos.x);
        fprintf(f, "windowPosY=%.0f\n", g_windowPos.y);
        fprintf(f, "windowWidth=%.0f\n", g_windowSize.x);
        fprintf(f, "windowHeight=%.0f\n", g_windowSize.y);
        fprintf(f, "chessboardScale=%.2f\n", g_chessboardScale);
        fprintf(f, "chessboardPosX=%.0f\n", g_chessboardPosX);
        fprintf(f, "chessboardPosY=%.0f\n", g_chessboardPosY);
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
            float fval; int ival;
            if (sscanf(line, "scale=%f", &fval) == 1) {
                g_globalScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
            }
            else if (sscanf(line, "predict=%d", &ival) == 1) g_featurePredict = (ival != 0);
            else if (sscanf(line, "esp=%d", &ival) == 1) g_featureESP = (ival != 0);
            else if (sscanf(line, "instantQuit=%d", &ival) == 1) g_featureInstantQuit = (ival != 0);
            else if (sscanf(line, "windowPosX=%f", &fval) == 1) g_windowPos.x = fval;
            else if (sscanf(line, "windowPosY=%f", &fval) == 1) g_windowPos.y = fval;
            else if (sscanf(line, "windowWidth=%f", &fval) == 1) g_windowSize.x = fval;
            else if (sscanf(line, "windowHeight=%f", &fval) == 1) g_windowSize.y = fval;
            else if (sscanf(line, "chessboardScale=%f", &fval) == 1) g_chessboardScale = fval;
            else if (sscanf(line, "chessboardPosX=%f", &fval) == 1) g_chessboardPosX = fval;
            else if (sscanf(line, "chessboardPosY=%f", &fval) == 1) g_chessboardPosY = fval;
        }
        fclose(f);
        ImGui::GetIO().FontGlobalScale = g_globalScale;
        g_windowPosInitialized = true;
    } else {
        SaveConfig();
    }
}

// ========== 精美滑动开关 ==========
bool ToggleSwitch(const char* label, bool* v, int animIdx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
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
    if (!ImGui::ItemAdd(total_bb, id)) return false;
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
    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(currentBgColor), height * 0.5f);
    float shift = progress * (width - 2 * radius - 4);
    ImVec2 thumbCenter(pos.x + radius + shift + (radius/2), pos.y + height/2);
    window->DrawList->AddCircleFilled(thumbCenter, radius - 1, IM_COL32(255,255,255,255), 32);
    window->DrawList->AddCircle(thumbCenter, radius - 3, IM_COL32(200,200,200,100), 32, 1.0f);
    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, pos.y + (height - label_size.y) * 0.5f), label);
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
    if (!g_featureESP) return;
    
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
        4.0f
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
            float radius = cellSize * 0.3f;
            
            ImU32 circleColor = ((row + col) % 2 == 0) ? 
                IM_COL32(255, 100, 100, 200) : IM_COL32(100, 100, 255, 200);
            
            drawList->AddCircleFilled(
                ImVec2(centerX, centerY),
                radius,
                circleColor,
                32
            );
            
            drawList->AddCircle(
                ImVec2(centerX, centerY),
                radius,
                IM_COL32(255, 255, 255, 150),
                32,
                1.0f
            );
        }
    }
}

// ========== 自定义窗口缩放回调 ==========
void ScaleWindow(ImGuiSizeCallbackData* data) {
    float newWidth = data->DesiredSize.x;
    float scaleDelta = newWidth / 280.0f;
    if (scaleDelta < MIN_SCALE) scaleDelta = MIN_SCALE;
    if (scaleDelta > MAX_SCALE) scaleDelta = MAX_SCALE;
    
    g_globalScale = scaleDelta;
    ImGui::GetIO().FontGlobalScale = g_globalScale;
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    // 初始化共享内存
    InitSharedMemory();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // 样式设置
    ImGuiStyle& style = ImGui::GetStyle();
    style.GrabMinSize = 24.0f;
    style.FramePadding = ImVec2(6, 4);
    style.WindowPadding = ImVec2(8, 8);
    style.ItemSpacing = ImVec2(6, 4);
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;

    LoadChineseFont();

    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    bool state = true, showDemoWindow = false, showAnotherWindow = false;
    ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);

    if (!imgui) {
        printf("[-] ImGui initialization failed\n");
        return 0;
    }

    LoadConfig();

    // ========== 输入线程 ==========
    std::thread inputThread([&] {
        struct sched_param param;
        param.sched_priority = 99;
        pthread_setschedparam(pthread_self(), SCHED_RR, &param);
        while (state) {
            imgui.ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    const float TARGET_FPS = 120.0f;
    const float TARGET_FRAME_TIME_MS = 1000.0f / TARGET_FPS;
    g_fpsTimer = std::chrono::high_resolution_clock::now();
    auto lastSaveTime = std::chrono::high_resolution_clock::now();

    printf("[2] Entering main loop (120fps)\n");

    while (state) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        bool prevPredict = g_featurePredict;
        bool prevESP = g_featureESP;
        bool prevInstantQuit = g_featureInstantQuit;

        // 读取共享内存
        ReadFromSharedMemory();

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

        if (showDemoWindow) ImGui::ShowDemoWindow(&showDemoWindow);

        bool posChanged = false, sizeChanged = false;
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * g_globalScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * g_globalScale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.2f, 0.6f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.6f, 1.0f));

            ImGui::SetNextWindowSizeConstraints(ImVec2(150, 200), ImVec2(FLT_MAX, FLT_MAX), ScaleWindow, nullptr);
            
            if (g_windowPosInitialized) {
                ImGui::SetNextWindowPos(g_windowPos, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(g_windowSize, ImGuiCond_FirstUseEver);
            }

            ImGui::Begin("金铲铲助手", &state, ImGuiWindowFlags_NoSavedSettings);

            ImVec2 currentPos = ImGui::GetWindowPos();
            ImVec2 currentSize = ImGui::GetWindowSize();
            posChanged = (currentPos.x != g_windowPos.x || currentPos.y != g_windowPos.y);
            sizeChanged = (currentSize.x != g_windowSize.x || currentSize.y != g_windowSize.y);
            if (posChanged || sizeChanged) {
                g_windowPos = currentPos;
                g_windowSize = currentSize;
            }

            ImGui::Separator();

            // 显示共享内存状态
            if (g_shmValid && g_sharedData) {
                ImGui::TextColored(ImVec4(0,1,0,1), "✓ 共享内存已连接");
                // 显示脚本名
                char scriptName[65] = {0};
                memcpy(scriptName, (void*)&g_sharedData->scriptName, 64);
                ImGui::Text("脚本: %s", scriptName);
                ImGui::Text("时间戳: %d", g_sharedData->timestamp);
            } else {
                ImGui::TextColored(ImVec4(1,0,0,1), "✗ 共享内存未连接");
            }

            ImGui::Separator();
            ImGui::Text("FPS: %.0f", g_currentFPS);
            ImGui::Text("缩放: %.1fx", g_globalScale);
            ImGui::Separator();

            ImGui::Text("功能设置");
            ToggleSwitch("预测", &g_featurePredict, 0);
            ToggleSwitch("透视", &g_featureESP, 1);
            ToggleSwitch("秒退", &g_featureInstantQuit, 2);

            ImGui::Separator();
            ImGui::Text("游戏数据");
            ImGui::Text("金币: %d", gold);
            ImGui::Text("等级: %d", level);
            ImGui::Text("血量: %d", hp);

            if (g_featureESP) {
                ImGui::Separator();
                ImGui::Text("棋盘设置");
                ImGui::SliderFloat("棋盘缩放", &g_chessboardScale, 0.3f, 3.0f, "%.1f");
            }

            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        }

        if (showAnotherWindow) {
            ImGui::Begin("另一个窗口", &showAnotherWindow);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("关闭")) showAnotherWindow = false;
            ImGui::End();
        }

        imgui.EndFrame();

        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        if (frameTime < TARGET_FRAME_TIME_MS) {
            int sleepUs = (int)((TARGET_FRAME_TIME_MS - frameTime) * 1000);
            if (sleepUs > 0) usleep(sleepUs);
        }

        auto saveTime = std::chrono::high_resolution_clock::now();
        float timeSinceLastSave = std::chrono::duration<float>(saveTime - lastSaveTime).count();
        bool switchesChanged = (prevPredict != g_featurePredict || prevESP != g_featureESP || prevInstantQuit != g_featureInstantQuit);
        bool windowMoved = posChanged || sizeChanged;
        if ((switchesChanged || windowMoved) && timeSinceLastSave > 2.0f) {
            SaveConfig();
            lastSaveTime = saveTime;
        }
    }

    CleanupSharedMemory();
    SaveConfig();
    if (inputThread.joinable()) inputThread.join();
    printf("[3] JCC Assistant exited\n");
    return 0;
}
