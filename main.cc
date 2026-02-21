#include "AImGui.h"
#include "imgui.h"

static float g_anim[32] = {};

bool ToggleSwitch(const char* id, bool* v, int idx)
{
    float h = ImGui::GetFrameHeight();
    float w = h * 1.8f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));

    if (ImGui::IsItemClicked())
        *v = !*v;

    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.25f;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 bg = ImGui::GetColorU32(ImVec4(
        0.3f + 0.0f * g_anim[idx],
        0.3f + 0.5f * g_anim[idx],
        0.3f - 0.1f * g_anim[idx],
        1.0f
    ));

    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);

    float r = h * 0.5f;
    float cx = p.x + r + g_anim[idx] * (w - h);

    draw->AddCircleFilled(
        ImVec2(cx, p.y + r),
        r - 2,
        IM_COL32(255,255,255,255)
    );

    return true;
}

int main()
{
    android::AImGui::run([](){

        static bool s1 = false;
        static bool s2 = true;

        ImGui::Begin("Android Surface ImGui");

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        ToggleSwitch("##s1", &s1, 0);
        ImGui::SameLine();
        ImGui::Text("Switch 1");

        ToggleSwitch("##s2", &s2, 1);
        ImGui::SameLine();
        ImGui::Text("Switch 2");

        ImGui::End();
    });

    return 0;
}
