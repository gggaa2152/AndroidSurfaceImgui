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
#include <cstdarg>
#include <signal.h> // 新增：信号处理
#include <android/log.h> // 新增：Android 日志

// 解决 C++20 下 char8_t* 无法隐式转换为 char* 的问题
#define U8(str) (const char*)u8##str

// 浮点精度比较宏
#define FLOAT_EQ(a, b, epsilon) (fabs((a) - (b)) < (epsilon))
#define FLOAT_EPSILON 0.01f

// Android 日志宏（兼容 Global.h）
#ifndef LOG_TAG
#define LOG_TAG "JCCAssistant"
#define AppLogError(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define AppLogInfo(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif

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

// ========== 全局原子变量（线程安全） ==========
std::atomic<bool> g_ImguiReady(false); // 标记 AImGui 是否完全初始化
std::atomic<bool> g_IsRunning(true);

// ========== 信号处理（优雅退出） ==========
void SignalHandler(int sig) {
    AppLogError("捕获信号 %d，开始优雅退出", sig);
    g_IsRunning = false;
}

// ========== 文件操作（增加权限检查） ==========
void SaveConfig() {
    // 检查目录可写性
    if (access("/data/local/tmp", W_OK) != 0) {
        AppLogError("目录 /data/local/tmp 不可写，无法保存配置");
        return;
    }

    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) {
        AppLogError("保存配置失败：无法打开文件 %s", CONFIG_PATH);
        return;
    }

    int writeCount = 0;
    writeCount += fprintf(f, "scale=%.2f\n", g_Config.globalScale);
    writeCount += fprintf(f, "predict=%d\n", g_Config.featurePredict);
    writeCount += fprintf(f, "esp=%d\n", g_Config.featureESP);
    writeCount += fprintf(f, "quit=%d\n", g_Config.featureInstantQuit);
    writeCount += fprintf(f, "win_x=%.0f\n", g_Config.windowPos.x);
    writeCount += fprintf(f, "win_y=%.0f\n", g_Config.windowPos.y);
    writeCount += fprintf(f, "win_w=%.0f\n", g_Config.windowSize.x);
    writeCount += fprintf(f, "win_h=%.0f\n", g_Config.windowSize.y);
    writeCount += fprintf(f, "board_scale=%.2f\n", g_Config.board.scale);
    writeCount += fprintf(f, "board_x=%.0f\n", g_Config.board.x);
    writeCount += fprintf(f, "board_y=%.0f\n", g_Config.board.y);
    writeCount += fprintf(f, "board_lock=%d\n", g_Config.board.lockPosition);

    if (writeCount <= 0) {
        AppLogError("保存配置失败：写入文件内容为空");
    }

    fclose(f);
}

void LoadConfig() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        AppLogInfo("配置文件 %s 不存在，使用默认配置", CONFIG_PATH);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        float fv; int iv;
        if (sscanf(line, "scale=%f", &fv) == 1 && fv >= 0.5f && fv <= 2.0f) {
            g_Config.globalScale = fv;
        } else if (sscanf(line, "predict=%d", &iv) == 1) {
            g_Config.featurePredict = (iv != 0);
        } else if (sscanf(line, "esp=%d", &iv) == 1) {
            g_Config.featureESP = (iv != 0);
        } else if (sscanf(line, "quit=%d", &iv) == 1) {
            g_Config.featureInstantQuit = (iv != 0);
        } else if (sscanf(line, "win_x=%f", &fv) == 1 && fv >= 0) {
            g_Config.windowPos.x = fv;
        } else if (sscanf(line, "win_y=%f", &fv) == 1 && fv >= 0) {
            g_Config.windowPos.y = fv;
        } else if (sscanf(line, "win_w=%f", &fv) == 1 && fv >= 100) {
            g_Config.windowSize.x = fv;
        } else if (sscanf(line, "win_h=%f", &fv) == 1 && fv >= 100) {
            g_Config.windowSize.y = fv;
        } else if (sscanf(line, "board_scale=%f", &fv) == 1 && fv >= 0.5f && fv <= 2.0f) {
            g_Config.board.scale = fv;
        } else if (sscanf(line, "board_x=%f", &fv) == 1 && fv >= 0) {
            g_Config.board.x = fv;
        } else if (sscanf(line, "board_y=%f", &fv) == 1 && fv >= 0) {
            g_Config.board.y = fv;
        } else if (sscanf(line, "board_lock=%d", &iv) == 1) {
            g_Config.board.lockPosition = (iv != 0);
        }
    }
    fclose(f);
    
    if(g_Config.globalScale < 0.5f) g_Config.globalScale = 0.5f;
    if(g_Config.globalScale > 2.0f) g_Config.globalScale = 2.0f;
    g_Config.windowInitialized = true;
}

// ========== 字体加载（增加容错） ==========
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
            if (font) {
                AppLogInfo("成功加载字体：%s", path);
                break;
            } else {
                AppLogError("加载字体失败：%s", path);
            }
        }
    }
    if (!font) {
        AppLogInfo("使用 ImGui 默认字体");
        io.Fonts->AddFontDefault();
    }
    // 关键：字体加载后必须重建纹理
    io.Fonts->Build();
    if (io.Fonts->TexID == nullptr) {
        AppLogError("字体纹理创建失败！这会导致渲染崩溃");
    }
}

// ========== 开关组件 ==========
bool ToggleSwitch(const char* label, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight();
    float width = height * 1.55f;
    float radius = height * 0.50f;

    ImGui::InvisibleButton(label, ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        *v = !*v;
    }
    
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

// ========== 棋盘组件（增加空指针检查） ==========
void DrawChessboard() {
    if (!g_Config.featureESP) return;

    ImGuiIO& io = ImGui::GetIO();
    // 空指针检查
    if (io.BackendRendererUserData == nullptr) {
        AppLogError("ImGui 渲染后端未初始化，跳过棋盘绘制");
        return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (drawList == nullptr) {
        AppLogError("背景绘制列表为空，跳过棋盘绘制");
        return;
    }
    
    const int ROWS = 4;
    const int COLS = 7;
    float cellSize = 45.0f * g_Config.board.scale;
    float w = COLS * cellSize;
    float h = ROWS * cellSize;
    
    ImVec2 mouse = io.MousePos;
    bool hovered = (mouse.x >= g_Config.board.x && mouse.x <= g_Config.board.x + w &&
                    mouse.y >= g_Config.board.y && mouse.y <= g_Config.board.y + h);
    
    if (!g_Config.board.lockPosition) {
        if (hovered && io.MouseDown[0] && !io.WantCaptureMouse) {
            g_Config.board.isDragging = true;
        }
        if (g_Config.board.isDragging) {
            if (io.MouseDown[0]) {
                g_Config.board.x += io.MouseDelta.x;
                g_Config.board.y += io.MouseDelta.y;
                
                g_Config.board.x = fmax(0.0f, g_Config.board.x);
                g_Config.board.y = fmax(0.0f, g_Config.board.y);
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
            draw_list->AddCircleFilled(ImVec2(cx, cy), cellSize * 0.35f, color, 16);
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

// ========== 输入线程（增加就绪检查） ==========
void InputThreadFunc(android::AImGui* imguiPtr) {
    AppLogInfo("输入线程启动，等待 AImGui 就绪");
    // 等待 AImGui 初始化完成
    while (g_IsRunning && !g_ImguiReady) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    if (!g_IsRunning || imguiPtr == nullptr) {
        AppLogError("输入线程退出：AImGui 指针为空或程序已停止");
        return;
    }

    AppLogInfo("输入线程开始处理事件");
    while (g_IsRunning) {
        try {
            imguiPtr->ProcessInputEvent();
        } catch (const std::exception& e) {
            AppLogError("处理输入事件异常：%s", e.what());
        } catch (...) {
            AppLogError("处理输入事件发生未知异常");
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
    AppLogInfo("输入线程正常退出");
}

// ========== 主程序 ==========
int main() {
    // 注册信号处理（避免崩溃）
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGINT, SignalHandler);

    AppLogInfo("程序启动，初始化 ImGui 上下文");
    // 1. 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    if (ctx == nullptr) {
        AppLogError("ImGui 上下文创建失败！");
        return 1;
    }
    ImGui::SetCurrentContext(ctx); // 关键：设置当前上下文
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // 启用键盘导航
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // 启用手柄导航
    
    // 2. 设置 ImGui 样式
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarSize = 22.0f;
    style.TouchExtraPadding = ImVec2(5, 5);

    // 3. 初始化 AImGui（关键：增加详细日志）
    AppLogInfo("初始化 AImGui，使用 Native 渲染");
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui) {
        AppLogError("AImGui 初始化失败! 请检查：1. Root 权限 2. Android 版本 3. SurfaceFlinger 权限");
        ImGui::DestroyContext(ctx);
        return 1;
    }
    AppLogInfo("AImGui 初始化成功");

    // 4. 加载字体（必须在 AImGui 初始化后）
    LoadFonts(io);

    // 5. 加载配置
    LoadConfig();

    // 6. 标记 AImGui 就绪，启动输入线程
    g_ImguiReady = true;
    std::thread inputThread(InputThreadFunc, &imgui);

    // 7. 主循环（固定帧率）
    const int TARGET_FPS = 30; // 降低帧率，减少 Android 资源占用
    const auto frameDuration = std::chrono::microseconds(1000000 / TARGET_FPS);
    auto lastSaveTime = std::chrono::steady_clock::now();
    bool configDirty = false;

    AppLogInfo("进入主循环，目标帧率 %d FPS", TARGET_FPS);
    while (g_IsRunning) {
        auto frameStart = std::chrono::steady_clock::now();

        // 检查 AImGui 是否有效
        if (!imgui) {
            AppLogError("主循环中 AImGui 失效，退出程序");
            g_IsRunning = false;
            break;
        }

        // 开始 ImGui 帧
        if (!imgui.BeginFrame()) {
            AppLogError("BeginFrame 失败，跳过本次帧");
            std::this_thread::sleep_for(frameDuration);
            continue;
        }

        io.FontGlobalScale = g_Config.globalScale;

        // 绘制棋盘
        DrawChessboard();

        // 绘制主菜单
        if (g_Config.showMenu) {
            if (g_Config.windowInitialized) {
                ImGui::SetNextWindowPos(g_Config.windowPos, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(g_Config.windowSize, ImGuiCond_FirstUseEver);
            }

            ImGui::Begin(U8("金铲铲助手"), &g_Config.showMenu, ImGuiWindowFlags_NoSavedSettings);
            
            ImVec2 curPos = ImGui::GetWindowPos();
            ImVec2 curSize = ImGui::GetWindowSize();
            
            bool posChanged = !FLOAT_EQ(curPos.x, g_Config.windowPos.x, FLOAT_EPSILON) || 
                              !FLOAT_EQ(curPos.y, g_Config.windowPos.y, FLOAT_EPSILON);
            bool sizeChanged = !FLOAT_EQ(curSize.x, g_Config.windowSize.x, FLOAT_EPSILON) || 
                              !FLOAT_EQ(curSize.y, g_Config.windowSize.y, FLOAT_EPSILON);
            
            if (posChanged || sizeChanged) {
                g_Config.windowPos = curPos;
                g_Config.windowSize = curSize;
                configDirty = true;
            }

            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();

            if (ImGui::CollapsingHeader(U8("功能"), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ToggleSwitch(U8(" 预测"), &g_Config.featurePredict)) configDirty = true;
                if (ToggleSwitch(U8(" 透视"), &g_Config.featureESP)) configDirty = true;
                if (ToggleSwitch(U8(" 秒退"), &g_Config.featureInstantQuit)) configDirty = true;
            }

            if (ImGui::CollapsingHeader(U8("状态"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text(U8("金币: %d"), g_Config.gold);
                ImGui::SameLine();
                ImGui::Text(U8("等级: %d"), g_Config.level);
                char hpBuf[32]; sprintf(hpBuf, "%d", g_Config.hp);
                ImGui::ProgressBar((float)g_Config.hp / 100.0f, ImVec2(-1, 0), hpBuf);
            }

            if (ImGui::CollapsingHeader(U8("设置"))) {
                if (ImGui::SliderFloat(U8("界面"), &g_Config.globalScale, 0.5f, 2.0f, "%.1f")) configDirty = true;
                if (ImGui::SliderFloat(U8("棋盘"), &g_Config.board.scale, 0.5f, 2.0f, "%.1f")) configDirty = true;
                if (ImGui::Checkbox(U8("锁定棋盘"), &g_Config.board.lockPosition)) configDirty = true;
                if (ImGui::Button(U8("保存配置"), ImVec2(-1, 40))) { SaveConfig(); configDirty = false; }
                
                ImGui::Spacing();
                if (ImGui::Button(U8("安全退出"), ImVec2(-1, 40))) {
                    g_IsRunning = false;
                }
            }
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(0, 200), ImGuiCond_FirstUseEver);
            ImGui::Begin("##Open", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
            if (ImGui::Button("OPEN", ImVec2(80, 40))) g_Config.showMenu = true;
            ImGui::End();
        }

        // 结束帧并渲染
        imgui.EndFrame();

        // 自动保存配置
        auto now = std::chrono::steady_clock::now();
        if (configDirty && (now - lastSaveTime > std::chrono::seconds(5))) {
            SaveConfig();
            configDirty = false;
            lastSaveTime = now;
        }

        // 帧率控制
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    // 8. 退出清理（关键：确保资源释放）
    AppLogInfo("程序退出，开始清理资源");
    if (configDirty) {
        SaveConfig();
    }

    // 等待输入线程退出
    if (inputThread.joinable()) {
        inputThread.join();
    }

    // 销毁 AImGui 和 ImGui 上下文
    imgui.Destroy(); // 显式销毁 AImGui
    ImGui::DestroyContext(ctx);

    AppLogInfo("程序正常退出");
    return 0;
}
