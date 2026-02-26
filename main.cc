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

bool g_predict_enemy = false, g_predict_hex = false, g_esp_board = true;
bool g_esp_bench = false, g_esp_shop = false, g_auto_buy = false, g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

// 缩放与位置控制
float g_scale = 1.0f;           
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_menuX = 100.0f, g_menuY = 100.0f;
float g_menuW = 400.0f, g_menuH = 600.0f; 

// 棋盘控制
float g_boardScale = 2.2f, g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    

// 英雄资源
GLuint g_heroTexture = 0;           
bool g_textureLoaded = false, g_resLoaded = false, g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 贴图与 Shader 渲染 (保留你的核心绘图代码)
// =================================================================
class HexShader {
public:
    GLuint program = 0; GLint resLoc = -1;
    void Init() {
        const char* vs = "#version 300 es\nlayout(location=0) in vec2 Position;\nlayout(location=1) in vec2 UV;\nout vec2 Frag_UV;\nuniform vec2 u_Res;\nvoid main() {\nFrag_UV = UV;\nvec2 ndc = (Position / u_Res) * 2.0 - 1.0;\ngl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n}";
        const char* fs = "#version 300 es\nprecision mediump float;\nuniform sampler2D Texture;\nin vec2 Frag_UV;\nout vec4 Out_Color;\nfloat sdHex(vec2 p, float r) { vec3 k = vec3(-0.866025, 0.5, 0.57735); p = abs(p); p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy; p -= vec2(clamp(p.x, -k.z * r, k.z * r), r); return length(p)*sign(p.y); }\nvoid main() { vec2 p = (Frag_UV - 0.5) * 2.0; vec2 rotated_p = vec2(p.y, p.x); float d = sdHex(rotated_p, 0.92); float w = fwidth(d); float m = 1.0 - smoothstep(-w, w, d); vec4 tex = texture(Texture, Frag_UV); if(m <= 0.0) discard; Out_Color = tex * m; }";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res"); glDeleteShader(v); glDeleteShader(f);
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
// 3. 棋盘绘制逻辑
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f; float yStep = sz * 1.5f;
    
    // 计算缩放手柄位置
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + sz * 0.6f, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 240));
    
    static bool isDraggingBoard = false, isScalingBoard = false;
    static ImVec2 dragOffset;

    if (ImGui::IsMouseClicked(0)) {
        if (ImRect(p_top, p_ext).Contains(io.MousePos)) isScalingBoard = true;
        else if (ImRect(ImVec2(g_startX-sz, g_startY-sz), ImVec2(lastCX+sz, lastCY+sz)).Contains(io.MousePos)) {
            isDraggingBoard = true; dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    if (isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - g_startX;
            g_boardManualScale = std::max(curW / ((6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale), 0.1f);
        } else isScalingBoard = false;
    }
    if (isDraggingBoard && !isScalingBoard) {
        if (ImGui::IsMouseDown(0)) { g_startX = io.MousePos.x - dragOffset.x; g_startY = io.MousePos.y - dragOffset.y; }
        else isDraggingBoard = false;
    }

    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz); 
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(0, 255, 255, 255), ImDrawFlags_Closed, 2.0f);
        }
    }
}

// =================================================================
// 4. 配置与字体
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n" << "espBoard=" << g_esp_board << "\n";
        out << "menuX=" << g_menuX << "\n" << "menuY=" << g_menuY << "\n";
        out << "menuW=" << g_menuW << "\n" << "menuH=" << g_menuH << "\n";
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
            if (k == "menuX") g_menuX = std::stof(v);
            else if (k == "menuY") g_menuY = std::stof(v);
            else if (k == "menuW") g_menuW = std::stof(v);
            else if (k == "menuH") g_menuH = std::stof(v);
            else if (k == "menuScale") g_scale = std::stof(v);
            else if (k == "espBoard") g_esp_board = (v == "1");
        }
        in.close(); g_needUpdateFontSafe = true;
    }
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(18.0f * g_autoScale * g_scale, 10.0f, 150.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 1.0f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF("/system/fonts/SysSans-Hans-Regular.ttf", targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 核心菜单 (物理缩放逻辑)
// =================================================================
bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(); float w = h * 2.0f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + 10.0f + label_size.x, h));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held; bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.15f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f,0.2f,0.2f,1), ImVec4(0,0.5f,1,1), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.0f, IM_COL32_WHITE);
    ImGui::RenderText(bb.Min + ImVec2(w + 10.0f, 0), label);
    return pressed;
}

void DrawMenu() {
    static bool isResizing = false;
    ImGuiIO& io = ImGui::GetIO();
    float winW = g_menuW, winH = g_menuCollapsed ? ImGui::GetFrameHeight() : g_menuH;

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuY + ImGui::GetFrameHeight())) {
            if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) g_menuCollapsed = !g_menuCollapsed;
        }
        if (!isResizing && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0)) {
            g_menuX += io.MouseDelta.x; g_menuY += io.MouseDelta.y;
        }

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            
            Toggle((const char*)u8"对手棋盘显示", &g_esp_board, 1);
            Toggle((const char*)u8"预测分布", &g_predict_enemy, 2);
            if (ImGui::Button((const char*)u8"保存", ImVec2(-1, 45*g_autoScale*g_scale))) SaveConfig();

            // --- 终极缩放手柄 ---
            ImVec2 br = ImGui::GetWindowPos() + ImGui::GetWindowSize();
            float hSz = 80.0f * g_autoScale; // 大感应区
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br-ImVec2(hSz*0.6f,0), br-ImVec2(0,hSz*0.6f), IM_COL32(0,120,255,255));

            if (ImGui::IsMouseClicked(0) && ImRect(br - ImVec2(hSz, hSz), br).Contains(io.MousePos)) isResizing = true;
            if (isResizing) {
                if (ImGui::IsMouseDown(0)) {
                    // 核心：直接改变物理宽度 = 手指坐标 - 起始坐标
                    g_menuW = std::max(200.0f, io.MousePos.x - g_menuX);
                    g_menuH = std::max(150.0f, io.MousePos.y - g_menuY);
                    // 反向推导 scale 用于内部缩放
                    g_scale = g_menuW / (320.0f * g_autoScale);
                } else { isResizing = false; g_needUpdateFontSafe = true; SaveConfig(); }
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 6. 入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    LoadConfig(); UpdateFontHD(true);
    while (true) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        imgui.BeginFrame();
        if (!g_resLoaded) { g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); g_textureLoaded = (g_heroTexture != 0); g_resLoaded = true; }
        DrawBoard(); 
        DrawMenu();
        imgui.EndFrame();
    }
    return 0;
}
