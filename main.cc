#include "AImGui.h"
#include "imgui.h"
#include "Global.h"

// 动画状态
static float g_anim[32] = {0.0f};

// 自定义开关
bool ToggleSwitch(const char* id, bool* v, int idx) {
    float h = ImGui::GetFrameHeight();
    float w = h * 1.8f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    if (ImGui::IsItemClicked()) *v = !*v;
    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.25f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 bg = ImGui::GetColorU32(ImVec4(0.3f, 0.3f + 0.5f * g_anim[idx], 0.3f, 1.0f));
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    float r = h * 0.5f;
    float cx = p.x + r + g_anim[idx] * (w - h);
    draw->AddCircleFilled(ImVec2(cx, p.y + r), r - 2, IM_COL32(255, 255, 255, 255));
    return true;
}

int main(int argc, char** argv) {
    LogInfo("Initializing AImGui for com.tencent.jkchess");

    // 1. 创建配置选项
    android::AImGui::Options options;
    options.renderType = android::AImGui::RenderType::RenderNative;
    options.autoUpdateOrientation = true;

    // 2. 实例化 AImGui 对象 (这会调用构造函数进行环境初始化)
    android::AImGui gui(options);

    // 检查初始化是否成功 (对应头文件里的 constexpr operator bool)
    if (!gui) {
        LogError("Failed to initialize AImGui environment!");
        return -1;
    }

    // 3. 运行主循环
    // 注意：既然你的类没提供 run，我们需要手动写循环，或者调用你在 .cc 里实现的逻辑
    // 假设你的 AImGui 实例控制了整个生命周期：
    while (true) {
        // 处理输入事件（读取触控）
        gui.ProcessInputEvent();

        // 开始 ImGui 帧
        gui.BeginFrame();

        static bool s1 = false;
        static bool s2 = true;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Android Surface ImGui")) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Separator();

            ToggleSwitch("##s1", &s1, 0);
            ImGui::SameLine(); ImGui::Text("Master Hack");

            ToggleSwitch("##s2", &s2, 1);
            ImGui::SameLine(); ImGui::Text("ESP Overlay");

            if (ImGui::Button("Exit", ImVec2(100, 40))) break;
        }
        ImGui::End();

        // 结束 ImGui 帧并渲染（交换缓冲区）
        gui.EndFrame();
    }

    return 0;
}
