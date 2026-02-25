// =================================================================
// 极致精简且超跟手版 (补全所有功能)
// =================================================================
#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 
#include <thread>      
#include <fstream>      
#include <GLES3/gl3.h>
#include <unistd.h>

// 1. 全局变量与配置结构 (保持你原来的变量名)
bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false;
bool g_auto_buy = false, g_instant = false;
float g_scale = 1.0f, g_autoScale = 1.0f, g_current_rendered_size = 0;
ImVec2 g_menuPos = {100, 100}; 
bool g_menuCollapsed = false, g_needUpdateFont = false;
float g_anim[15] = {0.0f};

// 2. 核心功能：Toggle (你原有的漂亮开关)
bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight();
    float w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.25f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    return pressed;
}

// 3. 字体动态更新 (已优化卡顿)
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    g_autoScale = io.DisplaySize.y / 1080.0f;
    float target = 18.0f * g_autoScale * g_scale;
    if (!force && std::abs(target - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF("/system/fonts/SysSans-Hans-Regular.ttf", target, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = target;
}

// 4. 菜单逻辑 (已优化跟手度)
void DrawMenu() {
    static bool isScaling = false, isDragging = false;
    static ImVec2 sMP, sPos; static float sMS;
    ImGuiIO& io = ImGui::GetIO();
    float bW = 320 * g_autoScale, bH = 500 * g_autoScale;

    ImGui::SetNextWindowSize({bW * g_scale, g_menuCollapsed ? ImGui::GetFrameHeight() : bH * g_scale});
    ImGui::SetNextWindowPos(g_menuPos);

    if (ImGui::Begin((const char*)u8"金铲铲助手", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        // 自定义标题栏拖动
        if (ImGui::IsMouseClicked(0) && ImGui::GetCurrentWindow()->TitleBarRect().Contains(io.MousePos)) {
            isDragging = true; sMP = io.MousePos; sPos = g_menuPos;
        }
        if (isDragging) {
            if (ImGui::IsMouseDown(0)) g_menuPos = sPos + (io.MousePos - sMP);
            else isDragging = false;
        }

        if (!g_menuCollapsed) {
            // 文字清晰度自适应
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            
            ImGui::TextColored(ImVec4(0, 1, 0.5, 1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();

            // --- 补全所有功能选项 ---
            if (ImGui::CollapsingHeader((const char*)u8"预测功能")) {
                ImGui::Indent();
                Toggle((const char*)u8"预测对手分布", &g_predict_enemy, 1);
                Toggle((const char*)u8"海克斯强化预测", &g_predict_hex, 2);
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader((const char*)u8"透视功能")) {
                ImGui::Indent();
                Toggle((const char*)u8"对手棋盘透视", &g_esp_board, 3);
                Toggle((const char*)u8"对手备战席透视", &g_esp_bench, 4);
                Toggle((const char*)u8"对手商店透视", &g_esp_shop, 5);
                ImGui::Unindent();
            }

            ImGui::Separator();
            Toggle((const char*)u8"全自动拿牌", &g_auto_buy, 6);
            Toggle((const char*)u8"极速秒退助手", &g_instant, 7);

            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 45 * g_autoScale * g_scale))) { /* SaveConfig()... */ }

            // 右下角全方位缩放
            ImVec2 br = g_menuPos + ImGui::GetWindowSize();
            float hSz = 60.0f * g_autoScale * g_scale;
            if (ImGui::IsMouseClicked(0) && ImRect(br - ImVec2(hSz, hSz), br).Contains(io.MousePos)) {
                isScaling = true; sMS = g_scale; sMP = io.MousePos;
            }
            if (isScaling) {
                if (ImGui::IsMouseDown(0)) {
                    float dx = (io.MousePos.x - sMP.x) / bW;
                    float dy = (io.MousePos.y - sMP.y) / bH;
                    g_scale = std::clamp(sMS + std::max(dx, dy), 0.4f, 4.0f);
                } else { isScaling = false; g_needUpdateFont = true; }
            }
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br - ImVec2(hSz*0.4f, 0), br - ImVec2(0, hSz*0.4f), IM_COL32(0, 120, 215, 200));
        }
    }
    ImGui::End();
}

int main() {
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true; 
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    UpdateFontHD(true);
    static bool running = true;
    std::thread([&] { while(running) { imgui.ProcessInputEvent(); std::this_thread::yield(); } }).detach();

    while (running) {
        if (g_needUpdateFont) { UpdateFontHD(true); g_needUpdateFont = false; }
        imgui.BeginFrame();
        DrawMenu();
        // DrawBoard()...
        imgui.EndFrame();
        std::this_thread::yield(); 
    }
    return 0;
}
