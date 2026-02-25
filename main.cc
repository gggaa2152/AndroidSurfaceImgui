// =================================================================
// 1. 头文件 (必须置顶)
// =================================================================
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
#include <algorithm>
#include <unistd.h>

// =================================================================
// 2. 全局状态变量 (严格保留字段名)
// =================================================================
bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false;  
bool g_auto_buy = false, g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f, g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    

ImVec2 g_menuPos = {100.0f, 100.0f}; 
bool g_needUpdateFont = false;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 3. 核心工具函数
// =================================================================

// 字体更新 (修复崩溃且精简)
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;

    // 确保 targetSize 始终有效，彻底解决 Error 134
    float targetSize = std::max(10.0f, 18.0f * g_autoScale * g_scale); 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tid;
}

bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(), w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + ImGui::GetStyle().ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, window->GetID(label))) return false;
    bool hovered, held, pressed = ImGui::ButtonBehavior(bb, window->GetID(label), &hovered, &held);
    if (pressed) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.25f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + ImGui::GetStyle().ItemInnerSpacing.x, bb.Min.y + ImGui::GetStyle().FramePadding.y), label);
    return pressed;
}

// =================================================================
// 4. 业务绘图 (棋盘与菜单)
// =================================================================

void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f, yStep = sz * 1.5f;

    static bool isDraggingB = false, isScalingB = false;
    static ImVec2 dragOff;
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    ImVec2 p_handle = ImVec2(lastCX + sz * 0.9f, lastCY); 

    if (ImGui::IsMouseClicked(0)) {
        if (ImGui::IsMouseHoveringRect(p_handle - ImVec2(30,30), p_handle + ImVec2(30,30))) isScalingB = true;
        else if (ImGui::IsMouseHoveringRect(ImVec2(g_startX-sz, g_startY-sz), ImVec2(lastCX+sz, lastCY+sz))) {
            isDraggingB = true; dragOff = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    if (isScalingB && ImGui::IsMouseDown(0)) {
        g_boardManualScale = std::max((io.MousePos.x - g_startX) / ((6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale), 0.1f);
    } else { isScalingB = false; }

    if (isDraggingB && !isScalingB && ImGui::IsMouseDown(0)) {
        g_startX = io.MousePos.x - dragOff.x; g_startY = io.MousePos.y - dragOff.y;
    } else { isDraggingB = false; }

    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            if(g_enemyBoard[r][c] && g_textureLoaded) d->AddImage((ImTextureID)(intptr_t)g_heroTexture, ImVec2(cx-sz, cy-sz), ImVec2(cx+sz, cy+sz));
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(fmodf((float)ImGui::GetTime() * 0.5f + (cx+cy)*0.001f, 1.0f), 0.8f, 1.0f, rf, gf, bf);
            d->AddCircle(ImVec2(cx, cy), sz, IM_COL32(rf*255, gf*255, bf*255, 200), 6, 3.0f * g_autoScale);
        }
    }
}

void DrawMenu() {
    static bool isScalingM = false, isDraggingM = false;
    static ImVec2 sMP, sPos; static float sMS;
    ImGuiIO& io = ImGui::GetIO();
    float bW = 320 * g_autoScale, bH = 500 * g_autoScale;

    ImGui::SetNextWindowSize({bW * g_scale, g_menuCollapsed ? ImGui::GetFrameHeight() : bH * g_scale});
    ImGui::SetNextWindowPos(g_menuPos);

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        // 自定义标题栏拖动
        if (ImGui::IsMouseClicked(0) && ImGui::GetCurrentWindow()->TitleBarRect().Contains(io.MousePos)) {
            isDraggingM = true; sMP = io.MousePos; sPos = g_menuPos;
        }
        if (isDraggingM && ImGui::IsMouseDown(0)) g_menuPos = sPos + (io.MousePos - sMP);
        else isDraggingM = false;

        // 折叠逻辑
        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuPos.y + ImGui::GetFrameHeight()) && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) 
            g_menuCollapsed = !g_menuCollapsed;

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            ImGui::TextColored(ImVec4(0,1,0.5,1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8"预测功能")) {
                ImGui::Indent(); Toggle((const char*)u8"预测对手分布", &g_predict_enemy, 1);
                Toggle((const char*)u8"海克斯强化预测", &g_predict_hex, 2); ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8"透视功能")) {
                ImGui::Indent(); Toggle((const char*)u8"对手棋盘透视", &g_esp_board, 3);
                Toggle((const char*)u8"对手备战席透视", &g_esp_bench, 4);
                Toggle((const char*)u8"对手商店透视", &g_esp_shop, 5); ImGui::Unindent();
            }
            ImGui::Separator();
            Toggle((const char*)u8"全自动拿牌", &g_auto_buy, 6);
            Toggle((const char*)u8"极速秒退助手", &g_instant, 7);

            // 右下角缩放
            ImVec2 br = g_menuPos + ImGui::GetWindowSize();
            float hSz = 60.0f * g_autoScale * g_scale;
            if (ImGui::IsMouseClicked(0) && ImRect(br-ImVec2(hSz,hSz), br).Contains(io.MousePos)) {
                isScalingM = true; sMS = g_scale; sMP = io.MousePos;
            }
            if (isScalingM && ImGui::IsMouseDown(0)) {
                g_scale = std::clamp(sMS + std::max((io.MousePos.x-sMP.x)/bW, (io.MousePos.y-sMP.y)/bH), 0.4f, 4.0f);
            } else if (isScalingM) { isScalingM = false; g_needUpdateFont = true; }
            
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br-ImVec2(hSz*0.4f,0), br-ImVec2(0,hSz*0.4f), IM_COL32(0,120,215,200));
        }
    }
    ImGui::End();
}

// =================================================================
// 5. 主程序
// =================================================================
int main() {
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true; 
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    UpdateFontHD(true); 
    static bool running = true;
    std::thread([&]{ while(running){ imgui.ProcessInputEvent(); std::this_thread::yield(); } }).detach();

    while (running) {
        if (g_needUpdateFont) { UpdateFontHD(true); g_needUpdateFont = false; }
        
        imgui.BeginFrame();
        if(!g_textureLoaded) {
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png");
            g_textureLoaded = (g_heroTexture != 0);
        }
        DrawBoard(); 
        DrawMenu();
        imgui.EndFrame();
        
        std::this_thread::yield();
    }
    return 0;
}
