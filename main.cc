#include "AImGui.h"
#include "imgui.h"
#include "Global.h"

// 动画平滑过渡数组
static float g_anim[32] = {0.0f};

/**
 * 自定义 ToggleSwitch 组件
 * 保留你原始代码中的绘制逻辑
 */
bool ToggleSwitch(const char* id, bool* v, int idx)
{
    float h = ImGui::GetFrameHeight();
    float w = h * 1.8f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));

    if (ImGui::IsItemClicked())
        *v = !*v;

    // 平滑动画插值
    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.25f;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 动态背景颜色
    ImU32 bg = ImGui::GetColorU32(ImVec4(
        0.3f + 0.0f * g_anim[idx],
        0.3f + 0.5f * g_anim[idx],
        0.3f - 0.1f * g_anim[idx],
        1.0f
    ));

    // 绘制背景矩形
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);

    // 绘制滑块圆点
    float r = h * 0.5f;
    float cx = p.x + r + g_anim[idx] * (w - h);

    draw->AddCircleFilled(
        ImVec2(cx, p.y + r),
        r - 2,
        IM_COL32(255, 255, 255, 255)
    );

    return true;
}

int main(int argc, char** argv)
{
    // 这里的 run 函数定义在 src/common/AImGui.cc 中
    // 它会自动调用 ANativeWindowCreator 并启动绘制循环
    android::AImGui::run([]() {
        
        static bool s1 = false;
        static bool s2 = true;

        // 设置 ImGui 窗口初始参数
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);

        // 绘制主窗口
        if (ImGui::Begin("Android Surface ImGui - Master Branch")) 
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Separator();

            // 开关 1
            ImGui::PushID("sw1");
            ToggleSwitch("##s1", &s1, 0);
            ImGui::SameLine();
            ImGui::Text("Enable Logic");
            ImGui::PopID();

            // 开关 2
            ImGui::PushID("sw2");
            ToggleSwitch("##s2", &s2, 1);
            ImGui::SameLine();
            ImGui::Text("Overlay Bypass");
            ImGui::PopID();

            ImGui::Spacing();
            ImGui::Separator();
            
            if (ImGui::Button("Exit Application", ImVec2(140, 45))) {
                exit(0);
            }
        }
        ImGui::End();
    });

    return 0;
}
