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
#include <atomic>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

// --- 底层注入依赖 ---
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/uio.h>

// 由 build.yml 自动生成的头文件 (xxd -i libJKHook.so)
#include "hook_payload.h"

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

const char* TARGET_PACKAGE = "com.tencent.jkchess";
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态
// =================================================================
bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_esp_level = false; 
bool g_auto_buy = false;
bool g_instant = false;
bool g_boardLocked = false; 

bool g_auto_refresh = false;
bool g_auto_buy_chosen = false;

bool g_show_card_pool = false;
int g_card_pool_rows = 2;
int g_card_pool_cols = 5;
float g_cardPoolX = 150.0f;
float g_cardPoolY = 150.0f;
float g_cardPoolScaleX = 1.0f; 
float g_cardPoolScaleY = 1.0f; 
float g_cardPoolAlpha = 1.0f; 

bool g_card_warning = false;
bool g_warning_tiers[7] = {false, false, false, false, true, false, false}; 
int g_warning_threshold = 6;   

bool g_menuCollapsed = false; 
float g_anim[25] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f;
float g_startY = 400.0f;    
float g_menuX = 100.0f;
float g_menuY = 100.0f;
float g_menuW = 350.0f;
float g_menuH = 550.0f; 

float g_benchX = 200.0f;
float g_benchY = 700.0f;
float g_benchScale = 1.0f;

float g_shopX = 200.0f;
float g_shopY = 850.0f;
float g_shopScale = 1.0f;

float g_enemy_X = 100.0f;
float g_enemy_Y = 100.0f;
float g_enemy_Scale = 1.0f;

float g_hex_X = 100.0f;
float g_hex_Y = 220.0f;
float g_hex_Scale = 1.0f;

float g_autoW_X = 300.0f;
float g_autoW_Y = 1000.0f;
float g_autoW_Scale = 1.0f;

float g_players_X = 1500.0f;
float g_players_Y = 200.0f;
float g_players_Scale = 1.0f;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

ImFont* g_mainFont = nullptr;
ImFont* g_hugeNumFont = nullptr;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, 
    {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, 
    {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置与资源管理函数 (必须定义在调用者上方)
// =================================================================

void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n";
        out << "predictHex=" << g_predict_hex << "\n";
        out << "espBoard=" << g_esp_board << "\n";
        out << "espBench=" << g_esp_bench << "\n";
        out << "espShop=" << g_esp_shop << "\n";
        out << "espLevel=" << g_esp_level << "\n"; 
        out << "showCardPool=" << g_show_card_pool << "\n";
        out << "cardPoolRows=" << g_card_pool_rows << "\n";
        out << "cardPoolCols=" << g_card_pool_cols << "\n";
        out << "cardPoolScaleX=" << g_cardPoolScaleX << "\n";
        out << "cardPoolScaleY=" << g_cardPoolScaleY << "\n";
        out << "cardPoolAlpha=" << g_cardPoolAlpha << "\n"; 
        out << "cardPoolX=" << g_cardPoolX << "\n";         
        out << "cardPoolY=" << g_cardPoolY << "\n";         
        out << "autoBuy=" << g_auto_buy << "\n";
        out << "autoRefresh=" << g_auto_refresh << "\n";
        out << "autoBuyChosen=" << g_auto_buy_chosen << "\n";
        out << "cardWarning=" << g_card_warning << "\n";
        out << "warningThreshold=" << g_warning_threshold << "\n";
        for (int i = 1; i <= 6; i++) out << "warningTier" << i << "=" << g_warning_tiers[i] << "\n";
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
        out << "enemyX=" << g_enemy_X << "\n"; 
        out << "enemyY=" << g_enemy_Y << "\n";
        out << "enemyScale=" << g_enemy_Scale << "\n";
        out << "hexX=" << g_hex_X << "\n"; 
        out << "hexY=" << g_hex_Y << "\n";
        out << "hexScale=" << g_hex_Scale << "\n";
        out << "autoWX=" << g_autoW_X << "\n"; 
        out << "autoWY=" << g_autoW_Y << "\n";
        out << "autoWScale=" << g_autoW_Scale << "\n";
        out << "playersX=" << g_players_X << "\n"; 
        out << "playersY=" << g_players_Y << "\n";
        out << "playersScale=" << g_players_Scale << "\n";
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
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy") g_predict_enemy = (v == "1");
                else if (k == "predictHex") g_predict_hex = (v == "1");
                else if (k == "espBoard") g_esp_board = (v == "1");
                else if (k == "espBench") g_esp_bench = (v == "1");
                else if (k == "espShop") g_esp_shop = (v == "1");
                else if (k == "espLevel") g_esp_level = (v == "1");
                else if (k == "showCardPool") g_show_card_pool = (v == "1");
                else if (k == "cardPoolRows") g_card_pool_rows = std::stoi(v);
                else if (k == "cardPoolCols") g_card_pool_cols = std::stoi(v);
                else if (k == "cardPoolScaleX") g_cardPoolScaleX = std::stof(v);
                else if (k == "cardPoolScaleY") g_cardPoolScaleY = std::stof(v);
                else if (k == "cardPoolAlpha") g_cardPoolAlpha = std::stof(v); 
                else if (k == "cardPoolX") g_cardPoolX = std::stof(v);
                else if (k == "cardPoolY") g_cardPoolY = std::stof(v);
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "autoRefresh") g_auto_refresh = (v == "1");
                else if (k == "autoBuyChosen") g_auto_buy_chosen = (v == "1");
                else if (k == "cardWarning") g_card_warning = (v == "1");
                else if (k == "warningThreshold") g_warning_threshold = std::stoi(v);
                else if (k.substr(0, 11) == "warningTier") {
                    int idx = k[11] - '0';
                    if (idx >= 1 && idx <= 6) g_warning_tiers[idx] = (v == "1");
                }
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
                else if (k == "enemyX") g_enemy_X = std::stof(v); 
                else if (k == "enemyY") g_enemy_Y = std::stof(v);
                else if (k == "enemyScale") g_enemy_Scale = std::stof(v);
                else if (k == "hexX") g_hex_X = std::stof(v); 
                else if (k == "hexY") g_hex_Y = std::stof(v);
                else if (k == "hexScale") g_hex_Scale = std::stof(v);
                else if (k == "autoWX") g_autoW_X = std::stof(v); 
                else if (k == "autoWY") g_autoW_Y = std::stof(v);
                else if (k == "autoWScale") g_autoW_Scale = std::stof(v);
                else if (k == "playersX") g_players_X = std::stof(v); 
                else if (k == "playersY") g_players_Y = std::stof(v);
                else if (k == "playersScale") g_players_Scale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

class HexShader {
public:
    GLuint program = 0; 
    GLint resLoc = -1;
    void Init() {
        const char* vs = "#version 300 es\nlayout(location=0) in vec2 Position;\nlayout(location=1) in vec2 UV;\nout vec2 Frag_UV;\nuniform vec2 u_Res;\nvoid main() {\nFrag_UV = UV;\nvec2 ndc = (Position / u_Res) * 2.0 - 1.0;\ngl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n}";
        const char* fs = "#version 300 es\nprecision mediump float;\nuniform sampler2D Texture;\nin vec2 Frag_UV;\nout vec4 Out_Color;\nfloat sdHex(vec2 p, float r) {\nvec3 k = vec3(-0.866025, 0.5, 0.57735);\np = abs(p); p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy; p -= vec2(clamp(p.x, -k.z * r, k.z * r), r); return length(p)*sign(p.y);\n}\nvoid main() {\nvec2 p = (Frag_UV - 0.5) * 2.0; float d = sdHex(vec2(p.y, p.x), 0.92); float alpha = 1.0 - smoothstep(-0.02, 0.02, d); if(alpha <= 0.0) discard; Out_Color = texture(Texture, Frag_UV) * alpha;\n}";
        program = glCreateProgram(); 
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v); 
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f); 
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program); 
        resLoc = glGetUniformLocation(program, "u_Res"); glDeleteShader(v); glDeleteShader(f);
    }
    void Cleanup() { if (program) { glDeleteProgram(program); program = 0; } }
} g_HexShader;

bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c; unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    float targetSize = std::clamp(18.0f * g_autoScale, 12.0f, 60.0f); 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture(); 
    io.Fonts->Clear(); 
    ImFontConfig configMain; configMain.OversampleH = 1; configMain.OversampleV = 1; configMain.PixelSnapH = true; 
    ImFontConfig configNum; configNum.OversampleH = 2; configNum.OversampleV = 2;
    const char* fonts[] = { "/system/fonts/SysSans-Hans-Regular.ttf", "/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/DroidSansFallback.ttf" };
    bool loaded = false;
    for(const char* path : fonts) {
        if (access(path, R_OK) == 0) { 
            g_mainFont = io.Fonts->AddFontFromFileTTF(path, targetSize * 1.5f, &configMain, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()); 
            if (g_mainFont) g_mainFont->Scale = 1.0f / 1.5f;
            g_hugeNumFont = io.Fonts->AddFontFromFileTTF(path, targetSize * 3.0f, &configNum, nullptr);
            if (g_hugeNumFont) g_hugeNumFont->Scale = 1.0f / 3.0f;
            loaded = true; break; 
        }
    }
    if(!loaded || !g_mainFont) g_mainFont = io.Fonts->AddFontDefault();
    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture(); 
    g_current_rendered_size = targetSize;
}

// =================================================================
// 3. UI 绘制与交互逻辑 (子绘制函数必须在 MainRenderThread 之上)
// =================================================================

void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, float& t_x, float& t_y, float& t_scale, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx_unscaled, float h_dy_unscaled, float c_dx_unscaled, float c_dy_unscaled, float hitMinX_unscaled, float hitMinY_unscaled, float hitMaxX_unscaled, float hitMaxY_unscaled, bool locked, bool* isOpen) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 p_scale(out_x + h_dx_unscaled * out_scale, out_y + h_dy_unscaled * out_scale);
        ImVec2 p_close(out_x + c_dx_unscaled * out_scale, out_y + c_dy_unscaled * out_scale);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (isOpen && ImLengthSqr(io.MousePos - p_close) < (4900.0f * g_autoScale * g_autoScale)) { *isOpen = false; return; }
            else if (ImLengthSqr(io.MousePos - p_scale) < (4900.0f * g_autoScale * g_autoScale)) { isScaling = true; scaleDragOffset = io.MousePos - ImVec2(t_x + h_dx_unscaled * t_scale, t_y + h_dy_unscaled * t_scale); }
            else {
                ImRect area(ImVec2(out_x + hitMinX_unscaled * out_scale, out_y + hitMinY_unscaled * out_scale), ImVec2(out_x + hitMaxX_unscaled * out_scale, out_y + hitMaxY_unscaled * out_scale));
                if (area.Contains(io.MousePos)) { isDragging = true; dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); }
            }
        }
        if (isScaling) { if (ImGui::IsMouseDown(0)) { ImVec2 target = io.MousePos - scaleDragOffset; float dist = sqrtf(powf(target.x - t_x, 2) + powf(target.y - t_y, 2)); t_scale = std::clamp(dist / sqrtf(h_dx_unscaled*h_dx_unscaled + h_dy_unscaled*h_dy_unscaled), 0.2f, 5.0f); } else isScaling = false; }
        if (isDragging && !isScaling) { if (ImGui::IsMouseDown(0)) { t_x = io.MousePos.x + dragOffset.x; t_y = io.MousePos.y + dragOffset.y; } else isDragging = false; }
    }
    float s = 1.0f - expf(-20.0f * io.DeltaTime); out_x = ImLerp(out_x, t_x, s); out_y = ImLerp(out_y, t_y, s); out_scale = ImLerp(out_scale, t_scale, s);
}

void HandleGridInteractionXY(float& out_x, float& out_y, float& out_scaleX, float& out_scaleY, float& t_x, float& t_y, float& t_scaleX, float& t_scaleY, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx_unscaled, float h_dy_unscaled, float c_dx_unscaled, float c_dy_unscaled, float hitMinX_unscaled, float hitMinY_unscaled, float hitMaxX_unscaled, float hitMaxY_unscaled, bool locked, bool* isOpen) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 p_scale(out_x + h_dx_unscaled * out_scaleX, out_y + h_dy_unscaled * out_scaleY);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (ImLengthSqr(io.MousePos - p_scale) < (4900.0f * g_autoScale * g_autoScale)) { isScaling = true; scaleDragOffset = io.MousePos - ImVec2(t_x + h_dx_unscaled * t_scaleX, t_y + h_dy_unscaled * t_scaleY); }
            else {
                ImRect area(ImVec2(out_x + hitMinX_unscaled * out_scaleX, out_y + hitMinY_unscaled * out_scaleY), ImVec2(out_x + hitMaxX_unscaled * out_scaleX, out_y + hitMaxY_unscaled * out_scaleY));
                if (area.Contains(io.MousePos)) { isDragging = true; dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); }
            }
        }
        if (isScaling) { if (ImGui::IsMouseDown(0)) { ImVec2 target = io.MousePos - scaleDragOffset; t_scaleX = std::clamp((target.x - t_x) / (h_dx_unscaled > 0.1f ? h_dx_unscaled : 1.0f), 0.2f, 5.0f); t_scaleY = std::clamp((target.y - t_y) / (h_dy_unscaled > 0.1f ? h_dy_unscaled : 1.0f), 0.2f, 5.0f); } else isScaling = false; }
        if (isDragging && !isScaling) { if (ImGui::IsMouseDown(0)) { t_x = io.MousePos.x + dragOffset.x; t_y = io.MousePos.y + dragOffset.y; } else isDragging = false; }
    }
    float s = 1.0f - expf(-20.0f * io.DeltaTime); out_x = ImLerp(out_x, t_x, s); out_y = ImLerp(out_y, t_y, s); out_scaleX = ImLerp(out_scaleX, t_scaleX, s); out_scaleY = ImLerp(out_scaleY, t_scaleY, s);
}

void DrawScaleHandle(ImDrawList* d, ImVec2 p, bool isScaling) { d->AddCircleFilled(p, 16.0f * g_autoScale, IM_COL32(255, 215, 0, 240)); d->AddCircleFilled(p, 6.0f * g_autoScale, isScaling ? IM_COL32(0, 255, 180, 255) : IM_COL32_WHITE); }
void DrawCloseHandle(ImDrawList* d, ImVec2 p, bool* v) { d->AddCircleFilled(p, 13.0f * g_autoScale, IM_COL32(220, 50, 50, 200)); }

void DrawBoard() {
    if (!g_esp_board) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    float baseSz = 38.0f * g_boardScale * g_autoScale; float curXStep = baseSz * 1.73205f * g_boardManualScale; float curYStep = baseSz * 1.5f * g_boardManualScale;
    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 7.0f * baseSz * 1.73205f, 1.5f * baseSz * 1.5f, -baseSz, 1.5f * baseSz * 1.5f, -baseSz*2, -baseSz*2, 8.0f*baseSz*1.73205f, 4.0f*baseSz*1.5f, g_boardLocked, &g_esp_board);
    if (!g_boardLocked) { DrawScaleHandle(d, ImVec2(g_startX + 7.0f * baseSz * 1.73205f * g_boardManualScale, g_startY + 1.5f * baseSz * 1.5f * g_boardManualScale), isScaling); }
    for(int r = 0; r < 4; r++) { for(int c = 0; c < 7; c++) { float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0); float cy = g_startY + r * curYStep; if(g_enemyBoard[r][c]) DrawHero(d, ImVec2(cx, cy), baseSz * g_boardManualScale * 0.95f); } }
}

void DrawBench() {
    if (!g_esp_bench) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_benchX, t_y = g_benchY, t_scale = g_benchScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    float baseSz = 40.0f * g_autoScale; HandleGridInteraction(g_benchX, g_benchY, g_benchScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 9 * baseSz, baseSz * 0.5f, -baseSz * 0.3f, baseSz * 0.5f, 0, 0, 9*baseSz, baseSz, g_boardLocked, &g_esp_bench);
    if (!g_boardLocked) DrawScaleHandle(d, ImVec2(g_benchX + 9 * baseSz * g_benchScale, g_benchY + baseSz * 0.5f * g_benchScale), isScaling);
    for (int i = 0; i < 9; i++) d->AddRect(ImVec2(g_benchX + i * baseSz * g_benchScale, g_benchY), ImVec2(g_benchX + (i+1)*baseSz*g_benchScale, g_benchY + baseSz*g_benchScale), IM_COL32_WHITE);
}

void DrawShop() {
    if (!g_esp_shop) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_shopX, t_y = g_shopY, t_scale = g_shopScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    float baseSz = 55.0f * g_autoScale; HandleGridInteraction(g_shopX, g_shopY, g_shopScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 5 * baseSz, baseSz * 0.5f, -baseSz * 0.3f, baseSz * 0.5f, 0, 0, 5*baseSz, baseSz, g_boardLocked, &g_esp_shop);
    if (!g_boardLocked) DrawScaleHandle(d, ImVec2(g_shopX + 5 * baseSz * g_shopScale, g_shopY + baseSz * 0.5f * g_shopScale), isScaling);
}

void DrawPurePredictEnemy() { /* 逻辑省略，保持结构 */ }
void DrawPurePredictHex() { /* 逻辑省略，保持结构 */ }
void DrawPlayersOverlay() { /* 逻辑省略，保持结构 */ }
void DrawAutoBuyWindow() { /* 逻辑省略，保持结构 */ }
void DrawCardPool() { /* 逻辑省略，保持结构 */ }

bool ModernToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); const ImGuiStyle& style = ImGui::GetStyle(); const ImGuiID id = window->GetID(label);
    float h = ImGui::GetFrameHeight() * 0.85f; float w = h * 2.1f; const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x, h));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held; if (ImGui::ButtonBehavior(bb, id, &hovered, &held)) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.22f, 0.27f, 1.0f), ImVec4(0.0f, 0.85f, 0.55f, 1.0f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y), label); return true;
}

void DrawMenu() {
    ImGuiStyle& style = ImGui::GetStyle(); style.WindowRounding = 16.0f * g_autoScale;
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        g_menuX = ImGui::GetWindowPos().x; g_menuY = ImGui::GetWindowPos().y; g_menuCollapsed = ImGui::IsWindowCollapsed();
        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale(g_scale); ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] 双端分离模式开启");
            ModernToggle((const char*)u8"预测对手", &g_predict_enemy, 1); ModernToggle((const char*)u8"预测海克斯", &g_predict_hex, 2);
            ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); ModernToggle((const char*)u8"备战席透视", &g_esp_bench, 4);
            ModernToggle((const char*)u8"商店透视", &g_esp_shop, 5); ModernToggle((const char*)u8"锁定所有窗口", &g_boardLocked, 8);
            if (ImGui::Button((const char*)u8"保存配置", ImVec2(-1, 45*g_autoScale))) SaveConfig();
        }
    }
    ImGui::End();
}

// =================================================================
// 4. 渲染主线程
// =================================================================
void MainRenderThread() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    LoadConfig(); UpdateFontHD(true);  
    std::thread it([&] { while(g_game_running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
    while (g_game_running) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        imgui.BeginFrame(); glDisable(GL_SCISSOR_TEST); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!g_resLoaded) { g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); g_textureLoaded = (g_heroTexture != 0); g_resLoaded = true; }
        DrawBoard(); DrawBench(); DrawShop(); 
        DrawPurePredictEnemy(); DrawPurePredictHex(); DrawPlayersOverlay(); DrawAutoBuyWindow(); DrawCardPool(); 
        DrawMenu();
        imgui.EndFrame(); std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    g_HexShader.Cleanup(); if (it.joinable()) it.join(); ImGui::DestroyContext();
}

// =================================================================
// 5. 底层 ptrace 注入引擎
// =================================================================
long get_module_base_remote(pid_t pid, const char* module_name) {
    FILE *fp; long addr = 0; char filename[64], line[1024]; snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r"); if (fp) { while (fgets(line, sizeof(line), fp)) { if (strstr(line, module_name)) { addr = strtoul(line, NULL, 16); break; } } fclose(fp); } return addr;
}

void* get_remote_func_addr(pid_t pid, const char* module_name, void* local_func) {
    long lb = get_module_base_remote(getpid(), module_name); long rb = get_module_base_remote(pid, module_name);
    if (!lb || !rb) return NULL; return (void *)((uintptr_t)local_func - lb + rb);
}

long ptrace_call_target(pid_t pid, uintptr_t func, long *params, int num) {
    struct user_pt_regs regs, saved; struct iovec iov = {&regs, sizeof(regs)};
    if (ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) < 0) return -1;
    memcpy(&saved, &regs, sizeof(regs)); for (int i = 0; i < num && i < 8; i++) regs.regs[i] = params[i];
    regs.pc = func; regs.regs[30] = 0; ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); int status = 0; waitpid(pid, &status, WUNTRACED);
    struct user_pt_regs ret_regs; struct iovec ret_iov = {&ret_regs, sizeof(ret_regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &ret_iov); long rv = ret_regs.regs[0];
    struct iovec siov = {&saved, sizeof(saved)}; ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &siov); return rv;
}

bool is_safe_to_inject(pid_t pid) {
    char p[256]; snprintf(p, sizeof(p), "/proc/%d/wchan", pid); std::ifstream f(p); std::string s;
    if (std::getline(f, s)) if (s.find("epoll") != std::string::npos || s.find("futex") != std::string::npos) return true;
    return false;
}

int perform_injection(pid_t pid, const char* drop_path) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1; waitpid(pid, NULL, WUNTRACED);
    void* r_mmap = get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; }
    char buf[256] = {0}; strncpy(buf, drop_path, 255);
    for (size_t i = 0; i < sizeof(buf); i += 4) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(uint32_t*)(buf + i));
    void* r_dl = get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    long d_p[] = {(long)r_mem, RTLD_NOW}; long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    ptrace(PTRACE_DETACH, pid, NULL, 0); return (h == 0) ? -1 : 0;
}

// =================================================================
// 6. 守护进程入口
// =================================================================
int main(int argc, char** argv) {
    printf("==================================================\n");
    printf("   JKHelper 终极守护进程 (UI + Hook 一体化)\n");
    printf("==================================================\n");
    FILE* f = fopen(DROP_SO_PATH, "wb");
    if(f) {
        fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
        struct stat st; if (stat("/data/data/com.tencent.jkchess", &st) == 0) chown(DROP_SO_PATH, st.st_uid, st.st_gid);
        chmod(DROP_SO_PATH, 0755); printf("[+] 内部 Hook 载荷部署成功。\n");
    } else { printf("[-] 无法释放载荷，请检查 Root。\n"); return 1; }

    while (true) {
        pid_t pid = 0; DIR* dir = opendir("/proc");
        if (dir) {
            struct dirent* ptr; while ((ptr = readdir(dir)) != NULL) {
                if (ptr->d_type == DT_DIR && atoi(ptr->d_name) > 0) {
                    char path[256]; snprintf(path, 256, "/proc/%s/cmdline", ptr->d_name);
                    std::ifstream f_cmd(path); std::string s; std::getline(f_cmd, s);
                    if (s.find(TARGET_PACKAGE) != std::string::npos && s.find(":") == std::string::npos) { pid = atoi(ptr->d_name); break; }
                }
            } closedir(dir);
        }
        if (pid > 0) {
            printf("[+] 捕捉到游戏进程 (%d)，执行注入并拉起菜单...\n", pid);
            while (get_module_base_remote(pid, "libil2cpp.so") == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
            if (perform_injection(pid, DROP_SO_PATH) == 0) printf("[+] Hook SO 注入成功。\n");
            g_game_running = true; std::thread render_thread(MainRenderThread);
            while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
            g_game_running = false; if (render_thread.joinable()) render_thread.join();
            printf("[*] 游戏已退出，资源已回收，继续监控...\n");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
