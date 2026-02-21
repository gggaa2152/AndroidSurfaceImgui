#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <sys/system_properties.h>

#include "Global.h"
#include "AImGui.h"
#include "ANativeWindowCreator.h"
#include "ATouchEvent.h"

// 仅引入 android，不全局引入 anative_window_creator 以避免 detail 冲突
using namespace android;

std::atomic<bool> g_KeepRunning{true};

void SignalHandler(int sig) {
    LogInfo("Signal [%d] received, shutting down...", sig);
    g_KeepRunning = false;
}

namespace com::tencent::jkchess {

class OverlayApp {
public:
    OverlayApp() : m_NativeWindow(nullptr) {}

    bool Initialize(AImGui::RenderType type) {
        char sdk_ver[PROP_VALUE_MAX];
        __system_property_get("ro.build.version.sdk", sdk_ver);
        
        // 使用完整的绝对路径，消除 detail 歧义
        android::anative_window_creator::detail::compat::SystemVersion = atoi(sdk_ver);
        LogInfo("JKChess Launcher - API: %zu", android::anative_window_creator::detail::compat::SystemVersion);

        if (type != AImGui::RenderType::RenderClient) {
            android::anative_window_creator::detail::compat::String8 name("JKChess_Overlay");
            
            // 注意：anative_window_creator 是命名空间，直接调用其内部方法
            m_SurfaceControl = android::anative_window_creator::CreateSurface(name, 1080, 2400);

            if (!m_SurfaceControl.get()) {
                LogError("[-] Critical: Failed to create SurfaceControl. Needs Root.");
                return false;
            }

            android::anative_window_creator::SetLayer(m_SurfaceControl, 0x7FFFFFFF); // 置顶
            android::anative_window_creator::Show(m_SurfaceControl);
            m_NativeWindow = android::anative_window_creator::GetNativeWindow(m_SurfaceControl);
        }

        AImGui::Options options;
        options.renderType = type;
        options.exchangeFontData = true;
        options.compressionFrameData = true;

        m_Gui = std::make_unique<AImGui>(options);
        if (!(*m_Gui)) {
            LogError("[-] Critical: AImGui context failed.");
            return false;
        }

        m_Touch = std::make_unique<ATouchEvent>();

        return true;
    }

    void Run() {
        std::thread inputThread([&] {
            while (g_KeepRunning && m_Gui) {
                m_Gui->ProcessInputEvent();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        while (g_KeepRunning && m_Gui) {
            m_Gui->BeginFrame();
            DrawBusinessUI();
            m_Gui->EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        if (inputThread.joinable()) inputThread.join();
        Cleanup();
    }

private:
    android::anative_window_creator::detail::types::StrongPointer<void> m_SurfaceControl;
    ANativeWindow* m_NativeWindow;
    std::unique_ptr<AImGui> m_Gui;
    std::unique_ptr<ATouchEvent> m_Touch;

    void DrawBusinessUI() {
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        
        // 修复 atomic bool 无法直接传给 ImGui 的问题
        bool is_open = g_KeepRunning.load();
        
        if (ImGui::Begin("JKChess arm64 ELF", &is_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: System Active");
            ImGui::Separator();
            
            static float val = 0.5f;
            ImGui::SliderFloat("Sensitivity", &val, 0.0f, 1.0f);

            if (ImGui::Button("Exit Application", ImVec2(-1, 40))) {
                is_open = false;
            }
            
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();

        // 同步窗口关闭状态回原子变量
        if (!is_open) {
            g_KeepRunning = false;
        }
    }

    void Cleanup() {
        LogInfo("Cleaning up resources...");
        m_Gui.reset();
        m_Touch.reset();
        if (m_SurfaceControl.get()) {
            android::anative_window_creator::Hide(m_SurfaceControl);
            android::anative_window_creator::Disconnect(m_SurfaceControl);
        }
        LogInfo("JKChess safe exit.");
    }
};

} // namespace com::tencent::jkchess

int main(int argc, char** argv) {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    com::tencent::jkchess::OverlayApp app;

    AImGui::RenderType mode = AImGui::RenderType::RenderServer;
    if (argc > 1 && std::string(argv[1]) == "client") {
        mode = AImGui::RenderType::RenderClient;
    }

    if (app.Initialize(mode)) {
        app.Run();
    }

    return 0;
}
