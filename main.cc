#include <cmath>
#include "imgui.h"
#include "AImGui.h"

static float g_toggleAnimProgress[32] = {};
static float g_toggleAnimTarget[32] = {};

bool ToggleSwitch(const char* label, bool* v, int animIdx)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.8f;
    const float radius = height * 0.5f;

    ImVec2 pos = window->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    if (pressed)
        *v = !*v;

    float target = *v ? 1.0f : 0.0f;
    float& progress = g_toggleAnimProgress[animIdx];

    float speed = 0.25f; // 更跟手
    progress += (target - progress) * speed;

    if (fabs(progress - target) < 0.01f)
        progress = target;

    ImU32 bg = ImGui::GetColorU32(ImLerp(
        ImVec4(0.3f,0.3f,0.3f,1.0f),
        ImVec4(0.2f,0.8f,0.3f,1.0f),
        progress
    ));

    window->DrawList->AddRectFilled(
        bb.Min, bb.Max, bg, height * 0.5f
    );

    float circleX = bb.Min.x + radius + progress * (width - height);

    window->DrawList->AddCircleFilled(
        ImVec2(circleX, bb.Min.y + radius),
        radius - 2,
        IM_COL32(255,255,255,255),
        32
    );

    return pressed;
}

int main()
{
    android::AImGui app;

    android::AImGui::Config config = {
        .title = "Test UI",
        .width = 800,
        .height = 600,
        .renderType = android::AImGui::RenderType::RenderNative,
    };

    app.Init(config);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // 不保存布局
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    bool toggle1 = false;
    bool toggle2 = true;

    app.Run([&]() {

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Once);
        ImGui::Begin("Android Surface ImGui");

        ImGui::Text("FPS: %.1f", io.Framerate);

        ToggleSwitch("Switch 1", &toggle1, 0);
        ToggleSwitch("Switch 2", &toggle2, 1);

        ImGui::End();
    });

    return 0;
}
