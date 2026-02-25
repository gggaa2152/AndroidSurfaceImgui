#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#include <thread>      
#include <cmath>       
#include <fstream>      
#include <string>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局变量 (保留原始字段名)
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false;  
bool g_auto_buy = false, g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

ImVec2 g_menuPos = {100.0f, 100.0f}; // 菜单位置
bool g_needUpdateFont = false;

// =================================================================
// 2. 核心功能：字体更新 (修复 SizePixels > 0 崩溃)
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    // 自动适配比例
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;

    // 计算目标大小并增加安全限值
    float targetSize = 18.0f * g_autoScale * g_scale;
    if (targetSize <= 1.0f) targetSize = 18.0f; // [关键修复] 防止 SizePixels <= 0 导致的崩溃

    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 1;
    config.PixelSnapH = true;
    
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 3. UI 交互组件 (补全 Toggle)
// =================================================================
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

// =================================================================
// 4. 菜单逻辑 (极致跟手 + 补全所有功能)
// =================================================================
void DrawMenu() {
    static bool isScalingMenu = false, isDraggingMenu = false;
    static float startMS = 1.0f; 
    static ImVec2 startMP, startPos;
    
    ImGuiIO& io = ImGui::GetIO(); 
    float baseW = 320.0f * g_autoScale;
    float baseH = 500.0f * g_autoScale;

    ImGui::SetNextWindowSize(ImVec2(baseW * g_scale, g_menuCollapsed ? ImGui::GetFrameHeight() : (baseH * g_scale)));
    ImGui::SetNextWindowPos(g_menuPos);

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        // 自定义拖动逻辑 (比 ImGui 原生拖动更贴合手指)
        ImRect titleRect = ImGui::GetCurrentWindow()->TitleBarRect();
        if (ImGui::IsMouseClicked(0) && titleRect.Contains(io.MousePos)) {
            isDraggingMenu = true; startMP = io.MousePos; startPos = g_menuPos;
        }
        if (isDraggingMenu) {
            if (ImGui::IsMouseDown(0)) g_menuPos = startPos + (io.MousePos - startMP);
            else isDraggingMenu = false;
        }

        // 标题栏点击收缩
        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuPos.y + ImGui::GetFrameHeight())) {
            if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) g_menuCollapsed = !g_menuCollapsed;
        }

        if (!g_menuCollapsed) {
            // 字体缩放适应
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
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 40 * g_autoScale * g_scale))) {
                // SaveConfig();
            }

            // --- 右下角全方位缩放 (绝对位移计算) ---
            ImVec2 br = g_menuPos + ImGui::GetWindowSize();
            float hSz = 60.0f * g_autoScale * g_scale; 
            ImRect handleRect(br - ImVec2(hSz, hSz), br);

            if (ImGui::IsMouseClicked(0) && handleRect.Contains(io.MousePos)) { 
                isScalingMenu = true; startMS = g_scale; startMP = io.MousePos; 
            }
            if (isScalingMenu) { 
                if (ImGui::IsMouseDown(0)) {
                    float dx = (io.MousePos.x - startMP.x) / baseW;
                    float dy = (io.MousePos.y - startMP.y) / baseH;
                    g_scale = std::clamp(startMS + std::max(dx, dy), 0.4f, 4.0f);
                } else { 
                    isScalingMenu = false; 
                    g_needUpdateFont = true; // 动作结束再更新字体
                } 
            }
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br - ImVec2(hSz*0.4f, 0), br - ImVec2(0, hSz*0.4f), IM_COL32(0, 120, 215, 200));
        }
    }
    ImGui::End();
}

// =================================================================
// 5. 程序入口
// =================================================================
int main() {
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true; 

    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    UpdateFontHD(true);  // 初始高清加载
    
    static bool running = true; 
    std::thread it([&] { while(running) { imgui.ProcessInputEvent(); std::this_thread::yield(); } });

    while (running) {
        if (g_needUpdateFont) {
            UpdateFontHD(true);
            g_needUpdateFont = false;
        }

        imgui.BeginFrame();
        DrawMenu();
        imgui.EndFrame(); 
        
        std::this_thread::yield();
    }
    
    running = false; 
    if (it.joinable()) it.join(); 
    return 0;
}
