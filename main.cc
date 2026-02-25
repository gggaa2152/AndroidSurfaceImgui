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
// 1. 全局变量 (com.tencent.jkchess)
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f;    
float g_startY = 400.0f;    

float g_menuX = 100.0f;
float g_menuY = 100.0f;
const float g_baseW = 320.0f; 
const float g_baseH = 500.0f; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置管理
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
        out << "startX=" << g_startX << "\n";
        out << "startY=" << g_startY << "\n";
        out << "manualScale=" << g_boardManualScale << "\n";
        out << "menuX=" << g_menuX << "\n";
        out << "menuY=" << g_menuY << "\n";
        out << "menuScale=" << g_scale << "\n";
        out << "menuCollapsed=" << (g_menuCollapsed ? 1 : 0) << "\n";
        out.close();
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue; 
            std::string k = line.substr(0, pos), v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy") g_predict_enemy = (v == "1");
                else if (k == "predictHex") g_predict_hex = (v == "1");
                else if (k == "espBoard") g_esp_board = (v == "1");
                else if (k == "espBench") g_esp_bench = (v == "1");
                else if (k == "espShop") g_esp_shop = (v == "1");
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "instant") g_instant = (v == "1");
                else if (k == "startX") g_startX = std::stof(v);
                else if (k == "startY") g_startY = std::stof(v);
                else if (k == "manualScale") g_boardManualScale = std::stof(v);
                else if (k == "menuX") g_menuX = std::stof(v);
                else if (k == "menuY") g_menuY = std::stof(v);
                else if (k == "menuScale") g_scale = std::stof(v);
                else if (k == "menuCollapsed") g_menuCollapsed = (v == "1");
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

// =================================================================
// 3. 渲染系统 (Shader & Texture)
// =================================================================
class HexShader {
public:
    GLuint program = 0; GLint resLoc = -1;
    void Init() {
        const char* vs = "#version 300 es\nlayout(location=0) in vec2 Position;\nlayout(location=1) in vec2 UV;\nout vec2 Frag_UV;\nuniform vec2 u_Res;\nvoid main() {\n    Frag_UV = UV;\n    vec2 ndc = (Position / u_Res) * 2.0 - 1.0;\n    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n}";
        const char* fs = "#version 300 es\nprecision mediump float;\nuniform sampler2D Texture;\nin vec2 Frag_UV;\nout vec4 Out_Color;\nfloat sdHex(vec2 p, float r) {\n    vec3 k = vec3(-0.866025, 0.5, 0.57735);\n    p = abs(p);\n    p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;\n    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);\n    return length(p)*sign(p.y);\n}\nvoid main() {\n    vec2 p = (Frag_UV - 0.5) * 2.0;\n    vec2 rotated_p = vec2(p.y, p.x);\n    float d = sdHex(rotated_p, 0.92);\n    float w = fwidth(d);\n    float m = 1.0 - smoothstep(-w, w, d);\n    vec4 tex = texture(Texture, Frag_UV);\n    if(m <= 0.0) discard;\n    Out_Color = tex * m;\n}";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexShader;
bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c; unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, (void*)(intptr_t)g_heroTexture);
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

// =================================================================
// 4. 棋盘绘制逻辑
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f; float yStep = sz * 1.5f;
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;

    float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + sz * 0.6f, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 200));

    static bool isDraggingBoard = false, isScalingBoard = false; static ImVec2 dragOffset;
    if (ImGui::IsMouseClicked(0)) {
        if (ImRect(p_top, p_ext).Contains(io.MousePos)) isScalingBoard = true;
        else if (ImRect(ImVec2(g_startX - sz, g_startY - sz), ImVec2(lastCX + sz, lastCY + sz)).Contains(io.MousePos)) {
            isDraggingBoard = true; dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    if (isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - g_startX;
            g_boardManualScale = std::max(curW / ((6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale), 0.1f);
        } else { isScalingBoard = false; SaveConfig(); }
    }
    if (isDraggingBoard && !isScalingBoard) {
        if (ImGui::IsMouseDown(0)) { g_startX = io.MousePos.x - dragOffset.x; g_startY = io.MousePos.y - dragOffset.y; }
        else { isDraggingBoard = false; SaveConfig(); }
    }

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0), cy = g_startY + r * yStep;
            if (g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz);
            ImVec2 pts[6]; for (int i = 0; i < 6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(255, 255, 255, 150), ImDrawFlags_Closed, 2.0f * g_autoScale);
        }
    }
}

// =================================================================
// 5. 菜单 UI 组件
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(18.0f * g_autoScale * g_scale, 12.0f, 120.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) io.Fonts->AddFontFromFileTTF(fontPath, targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label); const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(); float w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, style.FramePadding.y); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held; bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h * 0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h * 0.5f + g_anim[idx] * (w - h), h * 0.5f), h * 0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    return pressed;
}

// =================================================================
// 6. 完整菜单 (修复展开失效问题)
// =================================================================
void DrawMenu() {
    static bool isScaling = false; 
    static bool isDraggingHeader = false;
    ImGuiIO& io = ImGui::GetIO();

    // 关键修正：SetNextWindowCollapsed 使用变量 g_menuCollapsed 控制
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), isScaling ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(g_baseW * g_autoScale * g_scale, g_baseH * g_autoScale * g_scale), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(g_menuCollapsed); // 每一帧同步我们的变量

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        // --- 标题栏逻辑：移动与折叠 ---
        if (ImGui::IsWindowHovered() && io.MousePos.y < (window->Pos.y + ImGui::GetFrameHeight())) {
            if (ImGui::IsMouseClicked(0)) isDraggingHeader = true;
        }
        if (isDraggingHeader) {
            g_menuX = window->Pos.x; g_menuY = window->Pos.y; 
            if (ImGui::IsMouseReleased(0)) {
                // 如果位移极小，视为点击标题栏，物理翻转折叠变量
                if (io.MouseDragMaxDistanceSqr[0] < 15.0f && !isScaling) {
                    g_menuCollapsed = !g_menuCollapsed; 
                    SaveConfig();
                }
                isDraggingHeader = false;
            }
        }

        // --- 内容展示 ---
        if (!g_menuCollapsed) {
            float expectedSize = 18.0f * g_autoScale * g_scale;
            ImGui::SetWindowFontScale(expectedSize / g_current_rendered_size);

            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
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
            
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 45 * g_autoScale * g_scale))) SaveConfig();

            // --- 缩放逻辑 ---
            ImVec2 br = window->Pos + window->Size; 
            float hSz = 80.0f * g_autoScale;
            if (!isDraggingHeader && ImGui::IsMouseClicked(0)) {
                if (ImRect(br - ImVec2(hSz, hSz), br).Contains(io.MousePos)) isScaling = true;
            }
            if (isScaling) {
                if (ImGui::IsMouseDown(0)) {
                    float ns = (io.MousePos.x - g_menuX) / (g_baseW * g_autoScale);
                    g_scale = std::clamp(ns, 0.4f, 4.0f);
                } else { isScaling = false; g_needUpdateFontSafe = true; SaveConfig(); }
            }
            window->DrawList->AddTriangleFilled(br, br - ImVec2(hSz*0.35f, 0), br - ImVec2(0, hSz*0.35f), IM_COL32(0, 150, 255, 200));
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 主函数入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    LoadConfig(); UpdateFontHD(true);  
    static bool running = true; 
    std::thread it([&] { while(running) { imgui.ProcessInputEvent(); std::this_thread::yield(); } });

    while (running) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        imgui.BeginFrame(); 
        if (!g_resLoaded) { 
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); g_resLoaded = true; 
        }
        DrawBoard(); 
        DrawMenu();
        imgui.EndFrame(); 
        std::this_thread::yield();
    }
    running = false; if (it.joinable()) it.join(); 
    return 0;
}
