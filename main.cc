#include "imgui.h"
#include "AImGui.h"
#include <cmath>

static float g_anim[32] = {};

bool ToggleSwitch(const char* label, bool* v, int idx)
{
    float height = ImGui::GetFrameHeight();
    float width = height * 1.8f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(width, height));

    if (ImGui::IsItemClicked())
        *v = !*v;

    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.25f;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 bg = ImGui::GetColorU32(
        ImLerp(ImVec4(0.3f,0.3f,0.3f,1.0f),
               ImVec4(0.2f,0.8f,0.3f,1.0f),
               g_anim[idx])
    );

    draw->AddRectFilled(p,
                        ImVec2(p.x + width, p.y + height),
                        bg,
                        height * 0.5f);

    float r = height * 0.5f;
    float cx = p.x + r + g_anim[idx] * (width - height);

    draw->AddCircleFilled(
        ImVec2(cx, p.y + r),
        r - 2,
        IM_COL32(255,255,255,255)
    );

    return true;
}

int main()
{
    android::AImGui app;

    app.run([](){

        static bool s1 = false;
        static bool s2 = true;

        ImGui::Begin("Android Surface ImGui");

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        ToggleSwitch("##s1", &s1, 0);
        ImGui::Text("Switch 1");

        ToggleSwitch("##s2", &s2, 1);
        ImGui::Text("Switch 2");

        ImGui::End();
    });

    return 0;
}
