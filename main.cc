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
#include <vector>
#include <atomic>

// ========== 全局配置 ==========
struct AppConfig {
    int gold = 100;
    int level = 8;
    int hp = 85;

    bool featurePredict = false;
    bool featureESP = true;
    bool featureInstantQuit = false;

    float globalScale = 1.0f;
    bool showMenu = true;

    struct {
        float scale = 1.0f;
        float x = 200.0f;
        float y = 300.0f;
        bool isDragging = false;
        bool lockPosition = false;
    } board;

    ImVec2 windowPos = ImVec2(50, 100);
    ImVec2 windowSize = ImVec2(320, 450);
    bool windowInitialized = false;
};

AppConfig g_Config;
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

// ========== 文件操作 ==========
void SaveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "scale=%.2f\n", g_Config.globalScale);
        fprintf(f, "predict=%d\n", g_Config.featurePredict);
        fprintf(f, "esp=%d\n", g_Config.featureESP);
        fprintf(f, "quit=%d\n", g_Config.featureInstantQuit);
        fprintf(f, "win_x=%.0f\n", g_Config.windowPos.x);
        fprintf(f, "win_y=%.0f\n", g_Config.windowPos.y);
        fprintf(f, "win_w=%.0f\n", g_Config.windowSize.x);
        fprintf(f, "win_h=%.0f\n", g_Config.windowSize.y);
        fprintf(f, "board_scale=%.2f\n", g_Config.board.scale);
        fprintf(f, "board_x=%.0f\n", g_Config.board.x);
        fprintf(f, "board_y=%.0f\n", g_Config.board.y);
        fprintf(f, "board_lock=%d\n", g_Config.board.lockPosition);
        fclose(f);
    }
}

void LoadConfig() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        float fv; int iv;
        if (sscanf(line, "scale=%f", &fv) == 1) g_Config.globalScale = fv;
        else if (sscanf(line, "predict=%d", &iv) == 1) g_Config.featurePredict = iv;
        else if (sscanf(line, "esp=%d", &iv) == 1) g_Config.featureESP = iv;
        else if (sscanf(line, "quit=%d", &iv) == 1) g_Config.featureInstantQuit = iv;
        else if (sscanf(line, "win_x=%f", &fv) == 1) g_Config.windowPos.x = fv;
        else if (sscanf(line, "win_y=%f", &fv) == 1) g_Config.windowPos.y = fv;
        else if (sscanf(line, "win_w=%f", &fv) == 1) g_Config.windowSize.x = fv;
        else if (sscanf(line, "win_h=%f", &fv) == 1) g_Config.windowSize.y = fv;
        else if (sscanf(line, "board_scale=%f", &fv) == 1) g_Config.board.scale = fv;
        else if (sscanf(line, "board_x=%f", &fv) == 1) g_Config.board.x = fv;
        else if (sscanf(line, "board_y=%f", &fv) == 1) g_Config.board.y = fv;
        else if (sscanf(line, "board_lock=%d", &iv) == 1) g_Config.board.lockPosition = iv;
    }
    fclose(f);
    if(g_Config.globalScale < 0.5f) g_Config.globalScale = 0.5f;
    g_Config.windowInitialized = true;
}

// ========== 字体加载 ==========
void LoadFonts(ImGuiIO& io) {
    const char* fontPaths[] = {
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/SysSans-Hans-Regular.ttf"
    };

    ImFontConfig config;
    config.SizePixels = 18.0f;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        if (access(path, R_OK) == 0) {
            font = io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
            if (font) break;
        }
    }
    if (!font) io.Fonts->AddFontDefault();
    io.Fonts->Build();
}

// ========== 开关组件 ==========
bool ToggleSwitch(const char* label, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight();
    float width = height * 1.55f;
    float radius = height * 0.50f;

    ImGui::InvisibleButton(label, ImVec2(width, height));
    if (ImGui::IsItemClicked()) *v = !*v;
    
    ImU32 col_bg;
    if (ImGui::IsItemHovered())
        col_bg = *v ? IM_COL32(100, 200, 80, 255) : IM_COL32(180, 180, 180, 255);
    else
        col_bg = *v ? IM_COL32(80, 180, 60, 255) : IM_COL32(160, 160, 160, 255);

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(*v ? (p.x + width - radius) : (p.x + radius), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));
    ImGui::SameLine();
    ImGui::Text("%s", label);
    return *v;
}

// ========== 棋盘组件 ==========
void DrawChessboard() {
    if (!g_Config.featureESP) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    const int ROWS = 4;
    const int COLS = 7;
    float cellSize = 45.0f * g_Config.board.scale;
    float w = COLS * cellSize;
    float h = ROWS * cellSize;
    
    ImVec2 mouse = io.MousePos;
    bool hovered = (mouse.x >= g_Config.board.x && mouse.x <= g_Config.board.x + w &&
                    mouse.y >= g_Config.board.y && mouse.y <= g_Config.board.y + h);
    
    if (!g_Config.board.lockPosition) {
        if (hovered && io.MouseDown[0] && !io.WantCaptureMouse) g_Config.board.isDragging = true;
        if (g_Config.board.isDragging) {
            if (io.MouseDown[0]) {
                g_Config.board.x += io.MouseDelta.x;
                g_Config.board.y += io.MouseDelta.y;
            } else {
                g_Config.board.isDragging = false;
            }
        }
    }

    drawList->AddRectFilled(
        ImVec2(g_Config.board.x, g_Config.board.y),
        ImVec2(g_Config.board.x + w, g_Config.board.y + h),
        IM_COL32(30, 30, 35, 140), 10.0f
    );

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            float cx = g_Config.board.x + c * cellSize + cellSize * 0.5f;
            float cy = g_Config.board.y + r * cellSize + cellSize * 0.5f;
            bool isOdd = (r + c) % 2 != 0;
            ImU32 color = isOdd ? IM_COL32(100, 100, 255, 160) : IM_COL32(255, 100, 100, 160);
            drawList->AddCircleFilled(ImVec2(cx, cy), cellSize * 0.35f, color, 16);
        }
    }
    
    if (!g_Config.board.lockPosition) {
        drawList->AddRect(
            ImVec2(g_Config.board.x, g_Config.board.y),
            ImVec2(g_Config.board.x + w, g_Config.board.y + h),
            IM_COL32(255, 255, 0, 150), 10.0f, 0, 2.0f
        );
    }
}

// ========== 主程序 ==========
int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarSize = 22.0f;
    style.TouchExtraPadding = ImVec2(5, 5);

    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) {
        std::cerr << "AImGui 初始化失败! 请检查是否在 Root 权限下运行。" << std::endl;
        ImGui::DestroyContext();
        return 1;
    }

    LoadFonts(io);
    LoadConfig();

    std::atomic<bool> isRunning(true);
    
    // 多线程处理输入事件 (如果有随机崩溃，请尝试将此处删掉，把 imgui.ProcessInputEvent() 放到下面的 main loop 里)
    std::thread inputThread([&] {
        while (isRunning) {
            imgui.ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
    });

    const int TARGET_FPS = 60; // 建议60帧足够，降低手机发热
    const auto frameDuration = std::chrono::microseconds(1000000 / TARGET_FPS);
    auto lastSaveTime = std::chrono::steady_clock::now();
    bool configDirty = false;

    while (isRunning) {
        auto frameStart = std::chrono::steady_clock::now();
        imgui.BeginFrame();
        io.FontGlobalScale = g_Config.globalScale;

        DrawChessboard();

        if (g_Config.showMenu) {
            if (g_Config.windowInitialized) {
                ImGui::SetNextWindowPos(g_Config.windowPos, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(g_Config.windowSize, ImGuiCond_FirstUseEver);
            }

            // 添加 u8 前缀防止中文编译乱码
            ImGui::Begin(u8"金铲铲助手", &g_Config.showMenu, ImGuiWindowFlags_NoSavedSettings);
            
            ImVec2 curPos = ImGui::GetWindowPos();
            ImVec2 curSize = ImGui::GetWindowSize();
            if (curPos.x != g_Config.windowPos.x || curPos.y != g_Config.windowPos.y ||
                curSize.x != g_Config.windowSize.x || curSize.y != g_Config.windowSize.y) {
                g_Config.windowPos = curPos;
                g_Config.windowSize = curSize;
                configDirty = true;
            }

            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();

            if (ImGui::CollapsingHeader(u8"功能", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ToggleSwitch(u8" 预测", &g_Config.featurePredict)) configDirty = true;
                if (ToggleSwitch(u8" 透视", &g_Config.featureESP)) configDirty = true;
                if (ToggleSwitch(u8" 秒退", &g_Config.featureInstantQuit)) configDirty = true;
            }

            if (ImGui::CollapsingHeader(u8"状态", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text(u8"金币: %d", g_Config.gold);
                ImGui::SameLine();
                ImGui::Text(u8"等级: %d", g_Config.level);
                char hpBuf[32]; sprintf(hpBuf, "%d", g_Config.hp);
                ImGui::ProgressBar((float)g_Config.hp / 100.0f, ImVec2(-1, 0), hpBuf);
            }

            if (ImGui::CollapsingHeader(u8"设置")) {
                if (ImGui::SliderFloat(u8"界面", &g_Config.globalScale, 0.5f, 2.0f, "%.1f")) configDirty = true;
                if (ImGui::SliderFloat(u8"棋盘", &g_Config.board.scale, 0.5f, 2.0f, "%.1f")) configDirty = true;
                if (ImGui::Checkbox(u8"锁定棋盘", &g_Config.board.lockPosition)) configDirty = true;
                if (ImGui::Button(u8"保存配置", ImVec2(-1, 40))) { SaveConfig(); configDirty = false; }
                
                ImGui::Spacing();
                // 增加安全退出按钮，防止直接 Kill 导致进程挂起或资源未释放
                if (ImGui::Button(u8"安全退出", ImVec2(-1, 40))) {
                    isRunning = false;
                }
            }
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(0, 200), ImGuiCond_FirstUseEver);
            ImGui::Begin("##Open", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
            if (ImGui::Button("OPEN", ImVec2(80, 40))) g_Config.showMenu = true;
            ImGui::End();
        }

        imgui.EndFrame();

        auto now = std::chrono::steady_clock::now();
        if (configDirty && (now - lastSaveTime > std::chrono::seconds(5))) {
            SaveConfig();
            configDirty = false;
            lastSaveTime = now;
        }

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration) std::this_thread::sleep_for(frameDuration - elapsed);
    }

    // ========== 修复退出崩溃 (Use-After-Free) 的核心部分 ==========
    if (configDirty) SaveConfig();

    isRunning = false; // 确保通知子线程停止
    
    // 绝对不能用 inputThread.detach(); 必须等待它结束再销毁上下文
    if (inputThread.joinable()) {
        inputThread.join(); 
    }
    
    ImGui::DestroyContext(); // 清理 ImGui 防止内存泄漏和下次启动卡死
    return 0;
}
