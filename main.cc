#include <stdarg.h>
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
// 1. 全局变量与功能状态
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

// 功能开关
bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

// 菜单状态
bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

// 物理缩放控制
float g_scale = 1.0f;           
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_menuX = 100.0f;
float g_menuY = 100.0f;
float g_menuW = 400.0f; // 初始物理宽度
float g_menuH = 600.0f; // 初始物理高度

// 资源相关
GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

// =================================================================
// 2. 配置存取
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n";
        out << "predictHex=" << g_predict_hex << "\n";
        out << "espBoard=" << g_esp_board << "\n";
        out << "espBench=" << g_esp_bench << "\n";
        out << "espShop=" << g_esp_shop << "\n";
        out << "autoBuy=" << g_auto_buy << "\n";
        out << "instant=" << g_instant << "\n";
        out << "menuX=" << g_menuX << "\n";
        out << "menuY=" << g_menuY << "\n";
        out << "menuW=" << g_menuW << "\n";
        out << "menuH=" << g_menuH << "\n";
        out << "menuScale=" << g_scale << "\n";
        out.close();
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find('='); if (pos == std::string::npos) continue; 
            std::string k = line.substr(0, pos), v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy") g_predict_enemy = (v == "1");
                else if (k == "predictHex") g_predict_hex = (v == "1");
                else if (k == "espBoard") g_esp_board = (v == "1");
                else if (k == "espBench") g_esp_bench = (v == "1");
                else if (k == "espShop") g_esp_shop = (v == "1");
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "instant") g_instant = (v == "1");
                else if (k == "menuX") g_menuX = std::stof(v);
                else if (k == "menuY") g_menuY = std::stof(v);
                else if (k == "menuW") g_menuW = std::stof(v);
                else if (k == "menuH") g_menuH = std::stof(v);
                else if (k == "menuScale") g_scale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

// =================================================================
// 3. 字体与界面样式适配
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    
    // 字体大小随 g_scale 实时计算
    float targetSize = std::clamp(18.0f * g_autoScale * g_scale, 10.0f, 150.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 1.0f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    io.Fonts->AddFontFromFileTTF(fontPath, targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->Build(); 
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// 自定义 Toggle 开关
bool CustomToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float height = ImGui::GetFrameHeight();
    float width = height * 2.0f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width + 10.0f + label_size.x, height));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }

    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.15f;
    float t = g_anim[idx];

    ImU32 col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.15f, 0.15f, 0.15f, 1.0f), ImVec4(0.0f, 0.5f, 1.0f, 1.0f), t));
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(width, height), col_bg, height * 0.5f);
    window->DrawList->AddCircleFilled(ImVec2(bb.Min.x + height * 0.5f + t * (width - height), bb.Min.y + height * 0.5f), height * 0.5f - 2.0f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + width + 10.0f, bb.Min.y + (height - label_size.y) * 0.5f), label);

    return pressed;
}

// =================================================================
// 4. 终极菜单绘制 (物理像素跟手缩放)
// =================================================================
void DrawMenu() {
    static bool isResizing = false;
    ImGuiIO& io = ImGui::GetIO();

    // 核心物理参数
    const float rawBaseW = 320.0f; // UI 设计基准宽度

    // 确定当前显示的物理长宽
    float currentWinW = g_menuW;
    float currentWinH = g_menuCollapsed ? ImGui::GetFrameHeight() : g_menuH;

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(currentWinW, currentWinH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    
    if (ImGui::Begin((const char*)u8"金铲铲全功能助手", NULL, flags)) {
        
        // 1. 标题栏交互 (移动/折叠)
        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuY + ImGui::GetFrameHeight())) {
            if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) {
                g_menuCollapsed = !g_menuCollapsed; SaveConfig();
            }
        }
        if (!isResizing && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0)) {
            g_menuX += io.MouseDelta.x; g_menuY += io.MouseDelta.y;
            if (ImGui::IsMouseReleased(0)) SaveConfig();
        }

        if (!g_menuCollapsed) {
            // 2. 动态内容缩放系数
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            
            ImGui::TextColored(ImVec4(0, 0.8f, 1, 1), "System FPS: %.1f", io.Framerate);
            ImGui::Separator();

            if (ImGui::CollapsingHeader((const char*)u8"预测相关")) {
                ImGui::Indent();
                CustomToggle((const char*)u8"预测对手分布", &g_predict_enemy, 1);
                CustomToggle((const char*)u8"海克斯强化预测", &g_predict_hex, 2);
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader((const char*)u8"透视相关")) {
                ImGui::Indent();
                CustomToggle((const char*)u8"对手棋盘", &g_esp_board, 3);
                CustomToggle((const char*)u8"对手备战席", &g_esp_bench, 4);
                CustomToggle((const char*)u8"对手商店", &g_esp_shop, 5);
                ImGui::Unindent();
            }

            ImGui::Separator();
            CustomToggle((const char*)u8"全自动拿牌", &g_auto_buy, 6);
            CustomToggle((const char*)u8"极速秒退助手", &g_instant, 7);

            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 50 * g_autoScale * g_scale))) {
                SaveConfig();
            }

            // --- 3. 【重点：物理像素同步缩放逻辑】 ---
            ImVec2 br = ImGui::GetWindowPos() + ImGui::GetWindowSize(); // 窗口右下角点
            float handleTouchArea = 90.0f * g_autoScale; // 手指感应区调大，方便操作
            
            // 绘制视觉手柄
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                br, 
                br - ImVec2(handleTouchArea * 0.6f, 0), 
                br - ImVec2(0, handleTouchArea * 0.6f), 
                IM_COL32(0, 120, 255, 230)
            );

            // 缩放判定
            if (ImGui::IsMouseClicked(0) && ImRect(br - ImVec2(handleTouchArea, handleTouchArea), br).Contains(io.MousePos)) {
                isResizing = true;
            }

            if (isResizing) {
                if (ImGui::IsMouseDown(0)) {
                    // 直接线性映射：手指到哪，窗口边缘就到哪
                    g_menuW = std::max(220.0f * g_autoScale, io.MousePos.x - g_menuX);
                    g_menuH = std::max(200.0f * g_autoScale, io.MousePos.y - g_menuY);

                    // 反向推导 scale，用于控制字体和按钮大小
                    g_scale = g_menuW / (rawBaseW * g_autoScale);
                } else {
                    isResizing = false;
                    g_needUpdateFontSafe = true; // 停止缩放后自动重绘高清字体
                    SaveConfig();
                }
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 5. 运行入口
// =================================================================
int main() {
    // 基础环境初始化
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    // 加载配置
    LoadConfig(); 
    UpdateFontHD(true); 
    
    // 主循环
    while (true) {
        if (g_needUpdateFontSafe) { 
            UpdateFontHD(true); 
            g_needUpdateFontSafe = false; 
        }
        
        imgui.BeginFrame();
        
        // 执行菜单绘制
        DrawMenu();
        
        imgui.EndFrame();
        std::this_thread::yield();
    }
    return 0;
}
