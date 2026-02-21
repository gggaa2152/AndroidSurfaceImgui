#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <sys/system_properties.h>

// 严格对齐文件名大小写，解决 Linux 编译报错
#include "Global.h"
#include "AImGui.h"
#include "ANativeWindowCreator.h"
#include "ATouchEvent.h"

using namespace android;
using namespace android::anative_window_creator;

// 全局运行控制
std::atomic<bool> g_KeepRunning{true};

// 信号处理：确保 Root 进程被 Ctrl+C 或 kill 时能正常释放 Surface 句柄
void SignalHandler(int sig) {
    LogInfo("Signal [%d] received, shutting down...", sig);
    g_KeepRunning = false;
}

namespace com::tencent::jkchess {

class OverlayApp {
public:
    OverlayApp() : m_NativeWindow(nullptr) {}

    bool Initialize(AImGui::RenderType type) {
        // 1. 初始化系统环境（由 ANativeWindowCreator 依赖）
        char sdk_ver[PROP_VALUE_MAX];
        __system_property_get("ro.build.version.sdk", sdk_ver);
        detail::compat::SystemVersion = atoi(sdk_ver);
        LogInfo("JKChess Launcher - API: %zu", detail::compat::SystemVersion);

        // 2. 根据模式决定是否创建 Surface
        // 只有 Server 或 Native 模式需要创建底层窗口
        if (type != AImGui::RenderType::RenderClient) {
            detail::compat::String8 name("JKChess_Overlay");
            m_SurfaceControl = ANativeWindowCreator::CreateSurface(name, 1080, 2400);

            if (!m_SurfaceControl.get()) {
                LogError("[-] Critical: Failed to create SurfaceControl. Needs Root.");
                return false;
            }

            ANativeWindowCreator::SetLayer(m_SurfaceControl, 0x7FFFFFFF); // 置顶
            ANativeWindowCreator::Show(m_SurfaceControl);
            m_NativeWindow = ANativeWindowCreator::GetNativeWindow(m_SurfaceControl);
        }

        // 3. 初始化 AImGui
        AImGui::Options options;
        options.renderType = type;
        options.exchangeFontData = true;
        options.compressionFrameData = true; // 开启 Zstd 压缩

        m_Gui = std::make_unique<AImGui>(options);
        if (!(*m_Gui)) {
            LogError("[-] Critical: AImGui context failed.");
            return false;
        }

        // 4. 初始化触摸监听
        m_Touch = std::make_unique<ATouchEvent>();

        return true;
    }

    void Run() {
        // 输入处理线程：ProcessInputEvent 内部会处理触摸和事件转发
        std::thread inputThread([&] {
            while (g_KeepRunning && m_Gui) {
                m_Gui->ProcessInputEvent();
                // 100微秒睡眠，平衡响应速度与能耗
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // 渲染/逻辑主循环
        while (g_KeepRunning && m_Gui) {
            m_Gui->BeginFrame();

            // 业务 UI 绘制
            DrawBusinessUI();

            m_Gui->EndFrame();
            
            // 限制帧率在 120FPS 左右，降低手机发热
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        if (inputThread.joinable()) inputThread.join();
        Cleanup();
    }

private:
    detail::types::StrongPointer<void> m_SurfaceControl;
    ANativeWindow* m_NativeWindow;
    std::unique_ptr<AImGui> m_Gui;
    std::unique_ptr<ATouchEvent> m_Touch;

    void DrawBusinessUI() {
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("JKChess arm64 ELF", &g_KeepRunning.ref(), ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: System Active");
            ImGui::Separator();
            
            static float val = 0.5f;
            ImGui::SliderFloat("Sensitivity", &val, 0.0f, 1.0f);

            if (ImGui::Button("Exit Application", ImVec2(-1, 40))) {
                g_KeepRunning = false;
            }
            
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();
    }

    void Cleanup() {
        LogInfo("Cleaning up resources...");
        m_Gui.reset();
        m_Touch.reset();
        if (m_SurfaceControl.get()) {
            ANativeWindowCreator::Hide(m_SurfaceControl);
            ANativeWindowCreator::Disconnect(m_SurfaceControl);
        }
        LogInfo("JKChess safe exit.");
    }
};

} // namespace com::tencent::jkchess

int main(int argc, char** argv) {
    // 捕获系统退出信号
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    com::tencent::jkchess::OverlayApp app;

    // 默认作为服务端运行，如果参数带 client 则作为客户端
    AImGui::RenderType mode = AImGui::RenderType::RenderServer;
    if (argc > 1 && std::string(argv[1]) == "client") {
        mode = AImGui::RenderType::RenderClient;
    }

    if (app.Initialize(mode)) {
        app.Run();
    }

    return 0;
}
