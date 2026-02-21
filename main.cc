#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>

#include "GLOBAL.H"
#include "AImGui.h"
#include "ANativeWindowCreator.h"
#include "ATouchEvent.h"

using namespace android;
using namespace android::anative_window_creator;

// 全局运行控制，确保信号触发时能安全释放资源
std::atomic<bool> g_KeepRunning(true);

void GlobalSignalHandler(int sig) {
    LogInfo("Terminal signal [%d] received. Exiting gracefully...", sig);
    g_KeepRunning = false;
}

namespace com::tencent::jkchess {

class OverlayApp {
public:
    OverlayApp() : m_NativeWindow(nullptr) {}

    bool Initialize() {
        // 1. 初始化系统 API 环境（必须步骤，否则 Creator 内部指针偏移会错位）
        char sdk_ver[PROP_VALUE_MAX];
        __system_property_get("ro.build.version.sdk", sdk_ver);
        detail::compat::SystemVersion = atoi(sdk_ver);
        LogInfo("JKChess ELF - Android API: %zu", detail::compat::SystemVersion);

        // 2. 创建 SurfaceControl (NativeWindow 载体)
        // 默认全屏 1080x2400，可根据实际设备调整
        detail::compat::String8 surfaceName("JKChess_Overlay");
        m_SurfaceControl = ANativeWindowCreator::CreateSurface(
            surfaceName, 1080, 2400, 
            detail::types::PixelFormat::RGBA_8888, 
            detail::types::WindowFlags::eSkipScreenshot
        );

        if (!m_SurfaceControl.get()) {
            LogError("Critical: SurfaceControl creation failed. Root required.");
            return false;
        }

        // 置顶显示
        ANativeWindowCreator::SetLayer(m_SurfaceControl, 0x7FFFFFFF);
        ANativeWindowCreator::Show(m_SurfaceControl);

        // 3. 绑定 NativeWindow 与 AImGui
        m_NativeWindow = ANativeWindowCreator::GetNativeWindow(m_SurfaceControl);
        
        // 使用 Native 渲染模式，开启 Zstd 压缩（与你的 CMake 配置对应）
        AImGui::Options options;
        options.renderType = AImGui::RenderType::RenderNative;
        options.compressionFrameData = true;
        
        m_Gui = std::make_unique<AImGui>(options);
        if (!(*m_Gui)) {
            LogError("Critical: AImGui initialization failed.");
            return false;
        }

        // 4. 初始化触摸
        m_Touch = std::make_unique<ATouchEvent>();

        LogInfo("Overlay initialized successfully.");
        return true;
    }

    void Run() {
        // 输入处理线程：保持 1000Hz 左右的采样，确保 UI 响应丝滑
        std::thread inputThread([&] {
            while (g_KeepRunning && m_Gui) {
                m_Gui->ProcessInputEvent();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // 渲染主循环
        while (g_KeepRunning && m_Gui) {
            m_Gui->BeginFrame();

            // --- 绘制你的 UI ---
            RenderBusinessUI();

            m_Gui->EndFrame();
            
            // 限制帧率，减轻 CPU/GPU 压力
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (inputThread.joinable()) inputThread.join();
        Cleanup();
    }

private:
    detail::types::StrongPointer<void> m_SurfaceControl;
    ANativeWindow* m_NativeWindow;
    std::unique_ptr<AImGui> m_Gui;
    std::unique_ptr<ATouchEvent> m_Touch;

    void RenderBusinessUI() {
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("JKChess arm64 ELF", nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: System Active");
            ImGui::Separator();
            
            static float val = 0.5f;
            ImGui::SliderFloat("Sensitivity", &val, 0.0f, 1.0f);

            if (ImGui::Button("Close Overlay", ImVec2(-1, 50))) {
                g_KeepRunning = false;
            }
            
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();
    }

    void Cleanup() {
        LogInfo("Shutting down and cleaning up Surface...");
        m_Gui.reset();
        m_Touch.reset();
        if (m_SurfaceControl.get()) {
            ANativeWindowCreator::Hide(m_SurfaceControl);
            ANativeWindowCreator::Disconnect(m_SurfaceControl);
        }
    }
};

} // namespace com::tencent::jkchess

int main(int argc, char** argv) {
    // 信号监听
    signal(SIGINT, GlobalSignalHandler);
    signal(SIGTERM, GlobalSignalHandler);

    com::tencent::jkchess::OverlayApp app;
    if (app.Initialize()) {
        app.Run();
    }

    return 0;
}
