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
#include <vector>
#include <map>
#include <chrono>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_esp_level = false; // 新增：金币等级投食
bool g_auto_buy = false;
bool g_instant = false;
bool g_boardLocked = false; 

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

// 各大模块的坐标与缩放比例
float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
float g_menuX = 100.0f, g_menuY = 100.0f;
float g_menuW = 350.0f, g_menuH = 550.0f; 

float g_benchX = 200.0f, g_benchY = 700.0f, g_benchScale = 1.0f;
float g_shopX = 200.0f, g_shopY = 850.0f, g_shopScale = 1.0f;

// 预测窗口的坐标与缩放
float g_enemyW_X = 100.0f, g_enemyW_Y = 100.0f;
float g_enemyW_W = 220.0f, g_enemyW_H = 120.0f, g_enemy_scale = 1.0f;

float g_hexW_X = 100.0f, g_hexW_Y = 220.0f;
float g_hexW_W = 280.0f, g_hexW_H = 120.0f, g_hex_scale = 1.0f;

float g_autoW_X = 300.0f, g_autoW_Y = 1000.0f, g_autoW_Scale = 1.0f;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置管理 (全面支持高度与各种缩放)
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n";
        out << "predictHex=" << g_predict_hex << "\n";
        out << "espBoard=" << g_esp_board << "\n";
        out << "espBench=" << g_esp_bench << "\n";
        out << "espShop=" << g_esp_shop << "\n";
        out << "espLevel=" << g_esp_level << "\n"; // 保存金币等级投食状态
        out << "autoBuy=" << g_auto_buy << "\n";
        out << "instant=" << g_instant << "\n";
        out << "boardLocked=" << g_boardLocked << "\n";
        
        out << "menuX=" << g_menuX << "\n";
        out << "menuY=" << g_menuY << "\n";
        out << "menuW=" << g_menuW << "\n";
        out << "menuH=" << g_menuH << "\n";
        out << "menuScale=" << g_scale << "\n";
        out << "menuCollapsed=" << g_menuCollapsed << "\n";
        
        out << "startX=" << g_startX << "\n";
        out << "startY=" << g_startY << "\n";
        out << "manualScale=" << g_boardManualScale << "\n";
        
        out << "benchX=" << g_benchX << "\n";
        out << "benchY=" << g_benchY << "\n";
        out << "benchScale=" << g_benchScale << "\n";
        
        out << "shopX=" << g_shopX << "\n";
        out << "shopY=" << g_shopY << "\n";
        out << "shopScale=" << g_shopScale << "\n";
        
        out << "enemyWX=" << g_enemyW_X << "\n";
        out << "enemyWY=" << g_enemyW_Y << "\n";
        out << "enemyWW=" << g_enemyW_W << "\n";
        out << "enemyWH=" << g_enemyW_H << "\n";
        out << "enemyScale=" << g_enemy_scale << "\n";
        
        out << "hexWX=" << g_hexW_X << "\n";
        out << "hexWY=" << g_hexW_Y << "\n";
        out << "hexWW=" << g_hexW_W << "\n";
        out << "hexWH=" << g_hexW_H << "\n";
        out << "hexScale=" << g_hex_scale << "\n";
        
        out << "autoWX=" << g_autoW_X << "\n";
        out << "autoWY=" << g_autoW_Y << "\n";
        out << "autoWScale=" << g_autoW_Scale << "\n";
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
                else if (k == "espLevel") g_esp_level = (v == "1");
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "instant") g_instant = (v == "1");
                else if (k == "boardLocked") g_boardLocked = (v == "1");
                
                else if (k == "menuX") g_menuX = std::stof(v);
                else if (k == "menuY") g_menuY = std::stof(v);
                else if (k == "menuW") g_menuW = std::stof(v);
                else if (k == "menuH") g_menuH = std::stof(v);
                else if (k == "menuScale") g_scale = std::stof(v);
                else if (k == "menuCollapsed") g_menuCollapsed = (v == "1");
                
                else if (k == "startX") g_startX = std::stof(v);
                else if (k == "startY") g_startY = std::stof(v);
                else if (k == "manualScale") g_boardManualScale = std::stof(v);
                
                else if (k == "benchX") g_benchX = std::stof(v);
                else if (k == "benchY") g_benchY = std::stof(v);
                else if (k == "benchScale") g_benchScale = std::stof(v);
                
                else if (k == "shopX") g_shopX = std::stof(v);
                else if (k == "shopY") g_shopY = std::stof(v);
                else if (k == "shopScale") g_shopScale = std::stof(v);
                
                else if (k == "enemyWX") g_enemyW_X = std::stof(v);
                else if (k == "enemyWY") g_enemyW_Y = std::stof(v);
                else if (k == "enemyWW") g_enemyW_W = std::stof(v);
                else if (k == "enemyWH") g_enemyW_H = std::stof(v);
                else if (k == "enemyScale") g_enemy_scale = std::stof(v);
                
                else if (k == "hexWX") g_hexW_X = std::stof(v);
                else if (k == "hexWY") g_hexW_Y = std::stof(v);
                else if (k == "hexWW") g_hexW_W = std::stof(v);
                else if (k == "hexWH") g_hexW_H = std::stof(v);
                else if (k == "hexScale") g_hex_scale = std::stof(v);
                
                else if (k == "autoWX") g_autoW_X = std::stof(v);
                else if (k == "autoWY") g_autoW_Y = std::stof(v);
                else if (k == "autoWScale") g_autoW_Scale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

// =================================================================
// 3. 基础资源 (Hex Shader / Texture)
// =================================================================
class HexShader {
public:
    GLuint program = 0;
    GLint resLoc = -1;
    void Init() {
        const char* vs = "#version 300 es\n"
                         "layout(location=0) in vec2 Position;\n"
                         "layout(location=1) in vec2 UV;\n"
                         "out vec2 Frag_UV;\n"
                         "uniform vec2 u_Res;\n"
                         "void main() {\n"
                         "    Frag_UV = UV;\n"
                         "    vec2 ndc = (Position / u_Res) * 2.0 - 1.0;\n"
                         "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
                         "}";
        const char* fs = "#version 300 es\n"
                         "precision mediump float;\n"
                         "uniform sampler2D Texture;\n"
                         "in vec2 Frag_UV;\n"
                         "out vec4 Out_Color;\n"
                         "float sdHex(vec2 p, float r) {\n"
                         "    vec3 k = vec3(-0.866025, 0.5, 0.57735);\n"
                         "    p = abs(p);\n"
                         "    p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;\n"
                         "    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);\n"
                         "    return length(p)*sign(p.y);\n"
                         "}\n"
                         "void main() {\n"
                         "    vec2 p = (Frag_UV - 0.5) * 2.0;\n"
                         "    float d = sdHex(vec2(p.y, p.x), 0.92);\n"
                         "    float alpha = 1.0 - smoothstep(-0.02, 0.02, d);\n"
                         "    if(alpha <= 0.0) discard;\n"
                         "    Out_Color = texture(Texture, Frag_UV) * alpha;\n"
                         "}";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
    void Cleanup() {
        if (program) { glDeleteProgram(program); program = 0; }
    }
} g_HexShader;
bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(18.0f * g_autoScale * g_scale, 12.0f, 100.0f);
    
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 2; config.OversampleV = 2; config.PixelSnapH = true;
    
    const char* fonts[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf"
    };
    
    bool loaded = false;
    for(const char* path : fonts) {
        if (access(path, R_OK) == 0) {
            io.Fonts->AddFontFromFileTTF(path, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            loaded = true; break;
        }
    }
    if(!loaded) io.Fonts->AddFontDefault();

    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 4. 核心物理交互引擎 (多实例复用，防跳变 + 极度丝滑)
// =================================================================
void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, 
                           float& t_x, float& t_y, float& t_scale,
                           bool& isDragging, bool& isScaling, 
                           ImVec2& dragOffset, ImVec2& scaleDragOffset,
                           float h_dx_unscaled, float h_dy_unscaled, 
                           float c_dx_unscaled, float c_dy_unscaled,
                           float hitMinX_unscaled, float hitMinY_unscaled, 
                           float hitMaxX_unscaled, float hitMaxY_unscaled, 
                           bool locked, bool* isOpen) 
{
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        float scaleHandleX = out_x + h_dx_unscaled * out_scale;
        float scaleHandleY = out_y + h_dy_unscaled * out_scale;
        ImVec2 p_scale(scaleHandleX, scaleHandleY);
        
        float closeHandleX = out_x + c_dx_unscaled * out_scale;
        float closeHandleY = out_y + c_dy_unscaled * out_scale;
        ImVec2 p_close(closeHandleX, closeHandleY);

        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            // Check Close Button Hit
            if (isOpen && ImLengthSqr(io.MousePos - p_close) < (4900.0f * g_autoScale * g_autoScale)) {
                *isOpen = false;
                return; 
            }
            // Check Scale Handle Hit
            else if (ImLengthSqr(io.MousePos - p_scale) < (4900.0f * g_autoScale * g_autoScale)) { 
                isScaling = true;
                ImVec2 targetHandleCenter(t_x + h_dx_unscaled * t_scale, t_y + h_dy_unscaled * t_scale);
                scaleDragOffset = io.MousePos - targetHandleCenter;
            } 
            // Check Drag Area Hit
            else {
                ImRect area(
                    ImVec2(out_x + hitMinX_unscaled * out_scale, out_y + hitMinY_unscaled * out_scale), 
                    ImVec2(out_x + hitMaxX_unscaled * out_scale, out_y + hitMaxY_unscaled * out_scale)
                );
                if (area.Contains(io.MousePos)) {
                    isDragging = true; 
                    dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y);
                }
            }
        }
        
        if (isScaling) {
            if (ImGui::IsMouseDown(0)) {
                ImVec2 targetHandleCenter = io.MousePos - scaleDragOffset;
                float targetDist = sqrtf(powf(targetHandleCenter.x - t_x, 2) + powf(targetHandleCenter.y - t_y, 2));
                float baseHandleDist = sqrtf(h_dx_unscaled * h_dx_unscaled + h_dy_unscaled * h_dy_unscaled);
                t_scale = targetDist / baseHandleDist;
                t_scale = std::clamp(t_scale, 0.2f, 5.0f);
            } else { 
                isScaling = false; 
            }
        }
        
        if (isDragging && !isScaling) {
            if (ImGui::IsMouseDown(0)) {
                t_x = io.MousePos.x + dragOffset.x;
                t_y = io.MousePos.y + dragOffset.y;
            } else { 
                isDragging = false; 
            }
        }
    }

    // 丝滑指数衰减阻尼
    float smoothness = 1.0f - expf(-20.0f * io.DeltaTime);
    out_x = ImLerp(out_x, t_x, smoothness);
    out_y = ImLerp(out_y, t_y, smoothness);
    out_scale = ImLerp(out_scale, t_scale, smoothness);
}

// 静态的黄金缩放手柄
void DrawScaleHandle(ImDrawList* d, ImVec2 p_handle, bool isScaling) {
    ImU32 coreColor = isScaling ? IM_COL32(0, 255, 180, 255) : IM_COL32(255, 255, 255, 255);
    d->AddCircleFilled(p_handle, 16.0f * g_autoScale, IM_COL32(255, 215, 0, 240));
    d->AddCircleFilled(p_handle, 6.0f * g_autoScale, coreColor);
    d->AddCircle(p_handle, 20.0f * g_autoScale, IM_COL32(255, 215, 0, 150), 32, 2.5f * g_autoScale);
}

// 渲染红色贴片关闭按钮
void DrawCloseHandle(ImDrawList* d, ImVec2 p_handle, bool* isOpen) {
    if (!isOpen) return;
    ImGuiIO& io = ImGui::GetIO();
    float cr = 13.0f * g_autoScale;
    
    static std::map<void*, float> hover_map;
    bool cHov = ImLengthSqr(io.MousePos - p_handle) < (cr*cr * 2.5f);
    hover_map[isOpen] = ImLerp(hover_map[isOpen], cHov ? 1.0f : 0.0f, 1.0f - expf(-15.0f * io.DeltaTime));
    float cha = hover_map[isOpen];
    
    d->AddCircleFilled(p_handle, cr, IM_COL32(200 + 55*cha, 50, 50, 200 + 55*cha));
    d->AddLine(p_handle - ImVec2(cr*0.35f, cr*0.35f), p_handle + ImVec2(cr*0.35f, cr*0.35f), IM_COL32_WHITE, 2.5f * g_autoScale);
    d->AddLine(p_handle + ImVec2(cr*0.35f, -cr*0.35f), p_handle - ImVec2(cr*0.35f, -cr*0.35f), IM_COL32_WHITE, 2.5f * g_autoScale);
    
    if (cha > 0.01f) {
        d->AddCircle(p_handle, cr + 2.0f + 3.0f*cha, IM_COL32(255, 50, 50, 150*cha), 16, 2.0f*g_autoScale);
    }
}

// 完美缩放字体的霓虹按钮
bool AnimatedNeonButton(ImDrawList* d, const char* label, ImVec2 pos, ImVec2 size, int id, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImRect bb(pos, pos + size);
    bool hovered = bb.Contains(io.MousePos);
    bool clicked = false;
    
    if (hovered && ImGui::IsMouseClicked(0)) clicked = true;
    bool held = hovered && ImGui::IsMouseDown(0);

    static std::map<int, float> anims;
    float target = held ? 1.0f : (hovered ? 0.6f : 0.0f);
    anims[id] = ImLerp(anims[id], target, 1.0f - expf(-20.0f * io.DeltaTime));
    float a = anims[id];

    ImU32 bg = IM_COL32(20 + 20*a, 30 + 40*a, 40 + 40*a, 200 + 55*a);
    ImU32 border = IM_COL32(0, 150 + 105*a, 255, 100 + 155*a);
    
    d->AddRectFilled(bb.Min, bb.Max, bg, size.y * 0.5f);
    d->AddRect(bb.Min, bb.Max, border, size.y * 0.5f, 0, 1.5f * scale * g_autoScale); 
    
    if (a > 0.01f) {
        d->AddRect(bb.Min, bb.Max, IM_COL32(0, 200, 255, 80 * a), size.y * 0.5f, 0, 4.0f * scale * g_autoScale);
    }

    ImFont* font = ImGui::GetFont();
    float scaledFontSize = ImGui::GetFontSize() * scale;
    ImVec2 textSize = font->CalcTextSizeA(scaledFontSize, FLT_MAX, 0.0f, label);
    
    ImVec2 textPos = pos + ImVec2((size.x - textSize.x)*0.5f, (size.y - textSize.y)*0.5f);
    d->AddText(font, scaledFontSize, textPos, IM_COL32_WHITE, label, NULL);

    return clicked; 
}


// =================================================================
// 5. 棋盘、备战席、商店渲染 (彻底去掉了所有灰色背景)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();

    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool firstFrame = true;
    if (firstFrame) { t_x = g_startX; t_y = g_startY; t_scale = g_boardManualScale; firstFrame = false; }

    static bool isDragging = false, isScaling = false;
    static ImVec2 dragOffset, scaleDragOffset;   

    float baseSz = 38.0f * g_boardScale * g_autoScale;
    float baseXStep = baseSz * 1.73205f;
    float baseYStep = baseSz * 1.5f;

    float h_dx = 6 * baseXStep + (3 % 2 == 1 ? baseXStep * 0.5f : 0) + baseSz;
    float h_dy = 3 * baseYStep + baseSz * 0.5f;
    
    float c_dx = -baseXStep * 0.5f;
    float c_dy = -baseYStep * 0.5f;

    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale,
                          isDragging, isScaling, dragOffset, scaleDragOffset,
                          h_dx, h_dy, c_dx, c_dy, -baseSz*2, -baseSz*2, 6.5f*baseXStep + baseSz*2, 3.0f*baseYStep + baseSz*2, 
                          g_boardLocked, &g_esp_board);

    if (!g_esp_board) return;

    float curSz = baseSz * g_boardManualScale;
    float curXStep = baseXStep * g_boardManualScale;
    float curYStep = baseYStep * g_boardManualScale;
    float time = (float)ImGui::GetTime();

    if (!g_boardLocked) {
        DrawScaleHandle(d, ImVec2(g_startX + h_dx * g_boardManualScale, g_startY + h_dy * g_boardManualScale), isScaling);
        DrawCloseHandle(d, ImVec2(g_startX + c_dx * g_boardManualScale, g_startY + c_dy * g_boardManualScale), &g_esp_board);
    }

    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0);
            float cy = g_startY + r * curYStep;
            
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), curSz * 0.95f); 
            
            float hue = fmodf(time * 0.3f + (cx + cy) * 0.0008f, 1.0f);
            float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + curSz * cosf(a), cy + curSz * sinf(a));
            }
            // 完全去除了背景填充，仅渲染彩色发光边框
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 220), ImDrawFlags_Closed, 2.5f * g_autoScale);
        }
    }
}

void DrawBench() {
    if (!g_esp_bench) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    
    static float t_x = g_benchX, t_y = g_benchY, t_scale = g_benchScale;
    static bool first = true;
    if (first) { t_x = g_benchX; t_y = g_benchY; t_scale = g_benchScale; first = false; }
    static bool isDragging = false, isScaling = false;
    static ImVec2 dragOffset, scaleDragOffset;

    float baseSz = 40.0f * g_autoScale;
    float spacing = baseSz; 
    float h_dx = 9 * spacing + baseSz * 0.3f; 
    float h_dy = baseSz * 0.5f;
    float c_dx = -baseSz * 0.3f;              
    float c_dy = baseSz * 0.5f;

    HandleGridInteraction(g_benchX, g_benchY, g_benchScale, t_x, t_y, t_scale,
                          isDragging, isScaling, dragOffset, scaleDragOffset,
                          h_dx, h_dy, c_dx, c_dy, 0, 0, 9*spacing, baseSz, g_boardLocked, &g_esp_bench);

    if (!g_esp_bench) return;

    float curSz = baseSz * g_benchScale;
    float curSpacing = spacing * g_benchScale;
    float time = (float)ImGui::GetTime();

    if (!g_boardLocked) {
        DrawScaleHandle(d, ImVec2(g_benchX + h_dx * g_benchScale, g_benchY + h_dy * g_benchScale), isScaling);
        DrawCloseHandle(d, ImVec2(g_benchX + c_dx * g_benchScale, g_benchY + c_dy * g_benchScale), &g_esp_bench);
    }

    // 纯彩色薄边框网格，完全去除底色
    for (int i=0; i<9; i++) {
        float x = g_benchX + i * curSpacing;
        float y = g_benchY;
        
        float hue = fmodf(time * 0.3f + i * 0.05f, 1.0f);
        float r, g, b; ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
        
        d->AddRect(ImVec2(x, y), ImVec2(x+curSz, y+curSz), IM_COL32(r*255, g*255, b*255, 255), 0, 0, 1.5f * g_autoScale * g_benchScale);
    }
}

void DrawShop() {
    if (!g_esp_shop) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    
    static float t_x = g_shopX, t_y = g_shopY, t_scale = g_shopScale;
    static bool first = true;
    if (first) { t_x = g_shopX; t_y = g_shopY; t_scale = g_shopScale; first = false; }
    static bool isDragging = false, isScaling = false;
    static ImVec2 dragOffset, scaleDragOffset;

    float baseSz = 55.0f * g_autoScale;
    float spacing = baseSz; 
    float h_dx = 5 * spacing + baseSz * 0.3f;
    float h_dy = baseSz * 0.5f;
    float c_dx = -baseSz * 0.3f;
    float c_dy = baseSz * 0.5f;

    HandleGridInteraction(g_shopX, g_shopY, g_shopScale, t_x, t_y, t_scale,
                          isDragging, isScaling, dragOffset, scaleDragOffset,
                          h_dx, h_dy, c_dx, c_dy, 0, 0, 5*spacing, baseSz, g_boardLocked, &g_esp_shop);

    if (!g_esp_shop) return;

    float curSz = baseSz * g_shopScale;
    float curSpacing = spacing * g_shopScale;
    float time = (float)ImGui::GetTime();

    if (!g_boardLocked) {
        DrawScaleHandle(d, ImVec2(g_shopX + h_dx * g_shopScale, g_shopY + h_dy * g_shopScale), isScaling);
        DrawCloseHandle(d, ImVec2(g_shopX + c_dx * g_shopScale, g_shopY + c_dy * g_shopScale), &g_esp_shop);
    }

    // 纯彩色薄边框网格，完全去除底色
    for (int i=0; i<5; i++) {
        float x = g_shopX + i * curSpacing;
        float y = g_shopY;
        
        float hue = fmodf(time * 0.3f + i * 0.08f, 1.0f);
        float r, g, b; ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
        
        d->AddRect(ImVec2(x, y), ImVec2(x+curSz, y+curSz), IM_COL32(r*255, g*255, b*255, 255), 0, 0, 1.5f * g_autoScale * g_shopScale);
        
        if (g_textureLoaded) {
            float imgPad = 4.0f * g_autoScale * g_shopScale;
            d->AddImage((ImTextureID)(intptr_t)g_heroTexture, ImVec2(x+imgPad, y+imgPad), ImVec2(x+curSz-imgPad, y+curSz-imgPad));
        }
    }
}

// =================================================================
// 6. 悬浮窗面板 (预测窗口全局缩放与动画胶囊菜单)
// =================================================================
void DrawExtraWindows() {
    float time = (float)ImGui::GetTime();

    if (g_predict_enemy) {
        float r, g, b; ImGui::ColorConvertHSVtoRGB(fmodf(time * 0.2f, 1.0f), 0.8f, 1.0f, r, g, b);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(r, g, b, 0.8f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * g_autoScale);
        
        ImGui::SetNextWindowPos(ImVec2(g_enemyW_X, g_enemyW_Y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(g_enemyW_W, g_enemyW_H), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin((const char*)u8"预测对手", &g_predict_enemy, ImGuiWindowFlags_NoSavedSettings)) {
            
            // 全局缩放检测 (跟随拖动框体大小自适应)
            if (ImGui::IsMouseReleased(0)) {
                float curW = ImGui::GetWindowSize().x;
                float curH = ImGui::GetWindowSize().y;
                if (std::abs(curW - g_enemyW_W) > 5.0f || std::abs(curH - g_enemyW_H) > 5.0f) {
                    g_enemyW_W = curW; g_enemyW_H = curH;
                    g_enemy_scale = curW / (220.0f * g_autoScale); 
                }
            }
            ImVec2 pos = ImGui::GetWindowPos();
            if (pos.x != g_enemyW_X || pos.y != g_enemyW_Y) { g_enemyW_X = pos.x; g_enemyW_Y = pos.y; }
            
            float fontScaleVal = (18.0f * g_autoScale * g_enemy_scale) / g_current_rendered_size;
            ImGui::SetWindowFontScale(fontScaleVal);
            
            ImGui::Text((const char*)u8"极高概率遇到:");
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), (const char*)u8"> 玩家 3 (连胜中)");
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), (const char*)u8"> 玩家 5 (血量见底)");
        }
        ImGui::End();
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    
    if (g_predict_hex) {
        float r, g, b; ImGui::ColorConvertHSVtoRGB(fmodf(time * 0.2f + 0.5f, 1.0f), 0.8f, 1.0f, r, g, b);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(r, g, b, 0.8f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * g_autoScale);
        
        ImGui::SetNextWindowPos(ImVec2(g_hexW_X, g_hexW_Y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(g_hexW_W, g_hexW_H), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin((const char*)u8"海克斯质量评估", &g_predict_hex, ImGuiWindowFlags_NoSavedSettings)) {
            
            if (ImGui::IsMouseReleased(0)) {
                float curW = ImGui::GetWindowSize().x;
                float curH = ImGui::GetWindowSize().y;
                if (std::abs(curW - g_hexW_W) > 5.0f || std::abs(curH - g_hexW_H) > 5.0f) {
                    g_hexW_W = curW; g_hexW_H = curH;
                    g_hex_scale = curW / (280.0f * g_autoScale); 
                }
            }
            ImVec2 pos = ImGui::GetWindowPos();
            if (pos.x != g_hexW_X || pos.y != g_hexW_Y) { g_hexW_X = pos.x; g_hexW_Y = pos.y; }

            float fontScaleVal = (18.0f * g_autoScale * g_hex_scale) / g_current_rendered_size;
            ImGui::SetWindowFontScale(fontScaleVal);
            
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), (const char*)u8"[左] 银色: T2 (普通)");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), (const char*)u8"[中] 金色: T0 (神级)");
            ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), (const char*)u8"[右] 彩色: T1 (强势)");
        }
        ImGui::End();
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    
    // 自动拿牌 - 纯手工霓虹动画胶囊窗
    if (g_auto_buy) {
        ImDrawList* d = ImGui::GetForegroundDrawList();
        
        static float t_x = g_autoW_X, t_y = g_autoW_Y, t_scale = g_autoW_Scale;
        static bool first = true;
        if (first) { t_x = g_autoW_X; t_y = g_autoW_Y; t_scale = g_autoW_Scale; first = false; }
        static bool isDragging = false, isScaling = false;
        static ImVec2 dragOffset, scaleDragOffset;

        float baseW = 300.0f * g_autoScale; 
        float baseH = 65.0f * g_autoScale;
        float h_dx = baseW + 20.0f * g_autoScale; 
        float h_dy = baseH * 0.5f;

        HandleGridInteraction(g_autoW_X, g_autoW_Y, g_autoW_Scale, t_x, t_y, t_scale,
                              isDragging, isScaling, dragOffset, scaleDragOffset,
                              h_dx, h_dy, 0, 0, 0, 0, baseW, baseH, g_boardLocked, nullptr);

        float curW = baseW * g_autoW_Scale;
        float curH = baseH * g_autoW_Scale;

        ImVec2 p_min(g_autoW_X, g_autoW_Y);
        ImVec2 p_max(g_autoW_X + curW, g_autoW_Y + curH);
        float rounding = curH * 0.5f;
        
        d->AddRectFilled(p_min, p_max, IM_COL32(15, 20, 25, 240), rounding);
        d->AddRect(p_min, p_max, IM_COL32(0, 255, 150, 200), rounding, 0, 2.0f * g_autoScale * g_autoW_Scale);

        if (!g_boardLocked) {
            DrawScaleHandle(d, ImVec2(g_autoW_X + h_dx * g_autoW_Scale, g_autoW_Y + h_dy * g_autoW_Scale), isScaling);
        }

        float btnW = (baseW - 40.0f * g_autoScale) * 0.5f * g_autoW_Scale;
        float btnH = (baseH - 20.0f * g_autoScale) * g_autoW_Scale;
        float gap = 10.0f * g_autoScale * g_autoW_Scale;
        
        ImVec2 b1_pos = p_min + ImVec2(15.0f * g_autoScale * g_autoW_Scale, 10.0f * g_autoScale * g_autoW_Scale);
        ImVec2 b2_pos = b1_pos + ImVec2(btnW + gap, 0);
        
        if (AnimatedNeonButton(d, (const char*)u8"自动刷新", b1_pos, ImVec2(btnW, btnH), 101, g_autoW_Scale)) {
            // 点击触发逻辑
        }
        if (AnimatedNeonButton(d, (const char*)u8"自动拿天选", b2_pos, ImVec2(btnW, btnH), 102, g_autoW_Scale)) {
            // 点击触发逻辑
        }
    }
}

// =================================================================
// 7. 主菜单 UI
// =================================================================
bool ModernToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    
    float h = ImGui::GetFrameHeight() * 0.85f;
    float w = h * 2.1f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); } // 无操作不自动保存

    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f; 
    
    ImVec4 col_bg_off = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    ImVec4 col_bg_on  = ImVec4(0.00f, 0.85f, 0.55f, 1.0f); 
    ImVec4 col_bg = ImLerp(col_bg_off, col_bg_on, g_anim[idx]);
    
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(col_bg), h*0.5f);
    window->DrawList->AddRect(bb.Min, bb.Min + ImVec2(w, h), IM_COL32(0, 0, 0, 80), h*0.5f, 0, 1.0f);
    
    float handle_radius = h * 0.5f - 2.5f;
    ImVec2 handle_center = bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f);
    window->DrawList->AddCircleFilled(handle_center + ImVec2(0, 1.5f), handle_radius, IM_COL32(0, 0, 0, 90));
    window->DrawList->AddCircleFilled(handle_center, handle_radius, IM_COL32_WHITE);
    
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y*0.5f), label);
    return pressed;
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); 
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding = 16.0f * g_autoScale;
    style.FrameRounding = 8.0f * g_autoScale;
    style.PopupRounding = 8.0f * g_autoScale;
    style.GrabRounding = 8.0f * g_autoScale;
    style.ItemSpacing = ImVec2(12 * g_autoScale, 16 * g_autoScale);
    style.WindowPadding = ImVec2(16 * g_autoScale, 16 * g_autoScale);
    style.WindowBorderSize = 1.0f;

    // 修改菜单背景透明度为 0.85f
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.85f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.90f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.13f, 0.15f, 0.90f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.20f, 0.25f, 0.8f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.27f, 0.32f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.35f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.17f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.25f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.32f, 0.38f, 1.0f);

    static bool firstMenuOpen = true;
    if (firstMenuOpen) {
        ImGui::SetNextWindowCollapsed(g_menuCollapsed);
        firstMenuOpen = false;
    }

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v2.5", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        
        g_menuX = ImGui::GetWindowPos().x;
        g_menuY = ImGui::GetWindowPos().y;
        
        // 动态监听尺寸拖动，保存并实时计算缩放
        if (ImGui::IsMouseReleased(0)) {
            float curW = ImGui::GetWindowSize().x;
            float curH = ImGui::GetWindowSize().y;
            if (std::abs(curW - g_menuW) > 5.0f || std::abs(curH - g_menuH) > 5.0f) {
                g_menuW = curW;
                g_menuH = curH;
                g_scale = curW / (350.0f * g_autoScale);
                g_needUpdateFontSafe = true; 
            }
        }
        
        g_menuCollapsed = ImGui::IsWindowCollapsed();

        if (!g_menuCollapsed) {
            float fontScaleVal = (18.0f * g_autoScale * g_scale) / g_current_rendered_size;
            ImGui::SetWindowFontScale(fontScaleVal);
            
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] VSYNC 模式已开启 | FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8" 预测功能 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"预测对手", &g_predict_enemy, 1); 
                ModernToggle((const char*)u8"预测海克斯", &g_predict_hex, 2); 
                ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8" 投食功能 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); 
                ModernToggle((const char*)u8"备战席投食", &g_esp_bench, 4); 
                ModernToggle((const char*)u8"商店投食", &g_esp_shop, 5);
                ModernToggle((const char*)u8"金币等级投食", &g_esp_level, 9); // 新增金币等级投食
                ImGui::Unindent();
            }
            ImGui::Separator();
            ModernToggle((const char*)u8"锁定位置", &g_boardLocked, 8); 
            ModernToggle((const char*)u8"自动拿牌", &g_auto_buy, 6); 
            
            if (ModernToggle((const char*)u8"极速退游", &g_instant, 7)) {
                if (g_instant) ImGui::OpenPopup((const char*)u8"警告: 确认退出?");
            }
            
            ImGui::SetNextWindowSize(ImVec2(320 * g_autoScale * g_scale, 0));
            if (ImGui::BeginPopupModal((const char*)u8"警告: 确认退出?", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::SetWindowFontScale(fontScaleVal);
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), (const char*)u8"你确定要立即强制退出游戏吗？");
                ImGui::Spacing();
                
                float btnW = 135 * g_autoScale * g_scale;
                float btnH = 45 * g_autoScale * g_scale;
                if (ImGui::Button((const char*)u8"确定退出", ImVec2(btnW, btnH))) {
                    exit(0); 
                }
                ImGui::SameLine();
                if (ImGui::Button((const char*)u8"取消", ImVec2(btnW, btnH))) {
                    g_instant = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 55 * g_autoScale))) {
                SaveConfig();
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 8. 主循环 (帧率同步模式)
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    
    LoadConfig(); 
    UpdateFontHD(true);  
    
    static bool running = true; 
    std::thread it([&] { 
        while(running) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::yield(); 
        } 
    });

    while (running) {
        if (g_needUpdateFontSafe) { 
            UpdateFontHD(true); 
            g_needUpdateFontSafe = false; 
        }
        
        imgui.BeginFrame(); 
        
        if (!g_resLoaded) { 
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); 
            g_resLoaded = true; 
        }
        
        DrawBoard(); 
        DrawBench();
        DrawShop();
        DrawExtraWindows();
        DrawMenu();
        
        imgui.EndFrame(); 
        std::this_thread::yield();
    }
    
    g_HexShader.Cleanup();
    running = false; 
    if (it.joinable()) it.join(); 
    return 0;
}
