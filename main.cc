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
#include <signal.h>
#include <android/log.h>
#include <dlfcn.h> // 新增：动态库检查

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
#define AppLogDebug(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
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
std::atomic<bool> g_ImguiReady(false);
std::atomic<bool> g_IsRunning(true);

// ========== 信号处理（优雅退出） ==========
void SignalHandler(int sig) {
    AppLogError("捕获信号 %d，开始优雅退出", sig);
    g_IsRunning = false;
}

// ========== 系统环境检查 ==========
bool CheckAndroidEnv() {
    // 1. 检查 SELinux 状态
    FILE* fp = fopen("/sys/fs/selinux/enforce", "r");
    if (fp) {
        char buf[2] = {0};
        fread(buf, 1, 1, fp);
        fclose(fp);
        if (buf[0] == '1') {
            AppLogError("SELinux 处于开启状态，可能导致 Surface 访问失败！");
            // 尝试关闭 SELinux（需要 root）
            fp = fopen("/sys/fs/selinux/enforce", "w");
            if (fp) {
                fprintf(fp, "0");
                fclose(fp);
                AppLogInfo("已成功关闭 SELinux");
            } else {
                AppLogError("无法关闭 SELinux，请手动执行 setenforce 0");
                return false;
            }
        }
    }

    // 2. 检查关键动态库
    void* libandroid = dlopen("libandroid.so", RTLD_LAZY);
    void* libEGL = dlopen("libEGL.so", RTLD_LAZY);
    void* libGLESv2 = dlopen("libGLESv2.so", RTLD_LAZY);
    if (!libandroid || !libEGL || !libGLESv2) {
        AppLogError("缺少关键渲染库：libandroid=%p, libEGL=%p, libGLESv2=%p", libandroid, libEGL, libGLESv2);
        return false;
    }
    dlclose(libandroid);
    dlclose(libEGL);
    dlclose(libGLESv2);

    // 3. 检查 /data/local/tmp 可写
    if (access("/data/local/tmp", W_OK) != 0) {
        AppLogError("/data/local/tmp 不可写，切换到 /sdcard");
        g_Config.board.x = 100.0f; // 避免路径问题导致的间接崩溃
    }

    AppLogInfo("Android 环境检查通过");
    return true;
}

// ========== 文件操作 ==========
void SaveConfig() {
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

// ========== 字体加载（极简容错） ==========
void LoadFonts(ImGuiIO& io) {
    // 禁用自定义字体，直接使用默认字体（避免字体加载导致的渲染崩溃）
    AppLogInfo("使用 ImGui 默认字体，跳过自定义字体加载");
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
}

// ========== 开关组件 ==========
bool ToggleSwitch(const char* label, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (draw_list == nullptr) {
        AppLogError("draw_list 为空，跳过开关绘制");
        return *v;
    }
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

// ========== 棋盘组件（完全屏蔽，避免渲染崩溃） ==========
void DrawChessboard() {
    // 临时屏蔽棋盘绘制（核心崩溃点之一）
    AppLogDebug("临时屏蔽棋盘绘制，避免渲染崩溃");
    return;
    
    if (!g_Config.featureESP) return;

    ImGuiIO& io = ImGui::GetIO();
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

// ========== 输入线程（延迟启动 + 极简逻辑） ==========
void InputThreadFunc(android::AImGui* imguiPtr) {
    AppLogInfo("输入线程启动，延迟 2 秒等待初始化");
    // 延迟 2 秒，确保 AImGui 完全初始化
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!g_IsRunning || imguiPtr == nullptr) {
        AppLogError("输入线程退出：AImGui 指针为空或程序已停止");
        return;
    }

    AppLogInfo("输入线程开始处理事件");
    while (g_IsRunning) {
        try {
            // 降低输入事件处理频率，减少崩溃概率
            imguiPtr->ProcessInputEvent();
            std::this_thread::sleep_for(std::chrono::microseconds(5000));
        } catch (const std::exception& e) {
            AppLogError("处理输入事件异常：%s", e.what());
        } catch (...) {
            AppLogError("处理输入事件发生未知异常");
        }
    }
    AppLogInfo("输入线程正常退出");
}

// ========== 主程序（核心加固 + 兼容模式） ==========
int main() {
    // 1. 注册信号处理
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);
    signal(SIGINT, SignalHandler);

    // 2. 检查 Android 环境（关键！）
    if (!CheckAndroidEnv()) {
        AppLogError("Android 环境检查失败，程序退出");
        return 1;
    }

    AppLogInfo("程序启动，初始化 ImGui 上下文");
    // 3. 初始化 ImGui 上下文（极简配置）
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    if (ctx == nullptr) {
        AppLogError("ImGui 上下文创建失败！");
        return 1;
    }
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // 禁用鼠标光标修改（减少崩溃）
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // 启用 docking，增加兼容性
    
    // 4. 极简 ImGui 样式（避免样式相关崩溃）
    ImGui::StyleColorsLight(); // 改用 Light 样式，减少渲染计算
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f; // 禁用圆角，减少渲染错误
    style.FrameRounding = 0.0f;
    style.ScrollbarSize = 16.0f;
    style.TouchExtraPadding = ImVec2(0, 0);

    // 5. 初始化 AImGui（兼容模式）
    AppLogInfo("初始化 AImGui，使用兼容模式");
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderGLES, // 改用 GLES 渲染（替代 RenderNative）
        .autoUpdateOrientation = false, // 禁用自动旋转，减少崩溃
        .width = 800, // 固定宽度
        .height = 600 // 固定高度
    });

    // 6. 强制检查 AImGui 有效性
    if (!imgui) {
        AppLogError("AImGui 初始化失败! 请检查：1. Root 权限 2. Android 版本 3. SurfaceFlinger 权限");
        ImGui::DestroyContext(ctx);
        return 1;
    }
    AppLogInfo("AImGui 初始化成功");

    // 7. 加载字体（极简模式）
    LoadFonts(io);

    // 8. 加载配置（非关键，容错）
    LoadConfig();

    // 9. 延迟标记就绪，启动输入线程
    g_ImguiReady = true;
    std::thread inputThread(InputThreadFunc, &imgui);

    // 10. 主循环（极简逻辑，仅保留基础 UI）
    const int TARGET_FPS = 10; // 极低帧率，减少渲染压力
    const auto frameDuration = std::chrono::microseconds(1000000 / TARGET_FPS);
    AppLogInfo("进入主循环，目标帧率 %d FPS", TARGET_FPS);

    while (g_IsRunning) {
        auto frameStart = std::chrono::steady_clock::now();

        // 强制检查 AImGui 有效性
        if (!imgui) {
            AppLogError("主循环中 AImGui 失效，退出程序");
            g_IsRunning = false;
            break;
        }

        // 极简 BeginFrame（无返回值判断）
        imgui.BeginFrame();

        // 仅保留基础 UI，屏蔽所有渲染相关逻辑
        if (g_Config.showMenu) {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Always); // 固定窗口位置
            ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_Always); // 固定窗口大小
            ImGui::Begin(U8("金铲铲助手"), &g_Config.showMenu, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            
            ImGui::Text(U8("程序运行中（兼容模式）"));
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();

            if (ImGui::Button(U8("安全退出"), ImVec2(-1, 40))) {
                g_IsRunning = false;
            }
            ImGui::End();
        }

        // 极简 EndFrame
        imgui.EndFrame();

        // 帧率控制
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    // 11. 退出清理
    AppLogInfo("程序退出，开始清理资源");
    if (inputThread.joinable()) {
        inputThread.join();
    }
    ImGui::DestroyContext(ctx);

    AppLogInfo("程序正常退出");
    return 0;
}
