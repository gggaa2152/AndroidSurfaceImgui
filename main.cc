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
#include <mutex>         
#include <atomic>        
#include <dirent.h>      
#include <sys/uio.h>     
#include <sys/syscall.h> 

// =================================================================
// 1. 全局配置与状态
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

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

// 牌库显示状态与行列配置
bool g_show_card_pool = false;
int g_card_pool_rows = 2;
int g_card_pool_cols = 5;
float g_cardPoolX = 150.0f;
float g_cardPoolY = 150.0f;
float g_cardPoolScale = 1.0f; 
float g_cardPoolAlpha = 1.0f; 

// 预警功能状态
bool g_card_warning = false;
bool g_warning_tiers[7] = {false, false, false, false, true, false, false}; 
int g_warning_threshold = 6;   

bool g_menuCollapsed = false; 
float g_anim[25] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

// 各大模块的坐标与缩放比例
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

// 全新纯悬浮预测坐标
float g_enemy_X = 100.0f;
float g_enemy_Y = 100.0f;
float g_enemy_Scale = 1.0f;

float g_hex_X = 100.0f;
float g_hex_Y = 220.0f;
float g_hex_Scale = 1.0f;

float g_autoW_X = 300.0f;
float g_autoW_Y = 1000.0f;
float g_autoW_Scale = 1.0f;

// 整合的 8 人玩家信息覆盖层坐标 
float g_players_X = 1500.0f;
float g_players_Y = 200.0f;
float g_players_Scale = 1.0f;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, 
    {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, 
    {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// [新增] 1.5 纯外部跨进程通信引擎数据结构
// =================================================================
std::mutex g_dataMutex;

struct PlayerData {
    uint64_t objAddr;
    uint64_t dynAddr;
    int playerId;
    char name[64];
    int money;
    bool isAI;
};

PlayerData g_playersInfo[8];
int g_myPlayerId = -1;
uint64_t g_chessBattleModelAddr = 0;

int g_nextOpponentId = -1;
char g_nextOpponentName[64] = u8"等待回合开始...";
uint64_t g_nextOpponentDynArrayAddr = 0; 
std::map<int, int> g_heroLeftMap; 
std::atomic<bool> g_triggerUserExit{false};

pid_t g_gamePID = -1;
uint64_t g_libBase = 0;

// 获取目标游戏进程 PID
pid_t GetTargetPID(const char* packageName) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent* entry;
    pid_t pid = -1;
    while ((entry = readdir(dir)) != nullptr) {
        int current_pid = atoi(entry->d_name);
        if (current_pid > 0) {
            char cmdlinePath[256];
            snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%d/cmdline", current_pid);
            FILE* fp = fopen(cmdlinePath, "r");
            if (fp) {
                char cmdline[256] = {0};
                fread(cmdline, 1, sizeof(cmdline) - 1, fp);
                fclose(fp);
                if (strcmp(cmdline, packageName) == 0) {
                    pid = current_pid;
                    break;
                }
            }
        }
    }
    closedir(dir);
    return pid;
}

// 获取目标进程的模块基址 (纯外部读取 maps)
uint64_t GetModuleBaseExternally(pid_t pid, const char* moduleName) {
    char mapsPath[256];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    FILE* fp = fopen(mapsPath, "r");
    if (!fp) return 0;
    
    char line[512];
    uint64_t base = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, moduleName)) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(fp);
    return base;
}

// 纯系统调用 process_vm_readv
bool ReadMemoryExternal(pid_t pid, uint64_t addr, void* buffer, size_t size) {
    if (pid <= 0 || addr == 0 || !buffer) return false;
    
    struct iovec local_iov;
    local_iov.iov_base = buffer;
    local_iov.iov_len = size;

    struct iovec remote_iov;
    remote_iov.iov_base = (void*)addr;
    remote_iov.iov_len = size;

    ssize_t read_bytes = syscall(__NR_process_vm_readv, pid, &local_iov, 1, &remote_iov, 1, 0);
    return read_bytes == size;
}

template<typename T>
T ReadMem(uint64_t addr) {
    T value = {0};
    ReadMemoryExternal(g_gamePID, addr, &value, sizeof(T));
    return value;
}

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
        out << "espLevel=" << g_esp_level << "\n"; 
        out << "showCardPool=" << g_show_card_pool << "\n";
        out << "cardPoolRows=" << g_card_pool_rows << "\n";
        out << "cardPoolCols=" << g_card_pool_cols << "\n";
        out << "cardPoolScale=" << g_cardPoolScale << "\n";
        out << "cardPoolAlpha=" << g_cardPoolAlpha << "\n"; 
        out << "cardPoolX=" << g_cardPoolX << "\n";         
        out << "cardPoolY=" << g_cardPoolY << "\n";         
        out << "autoBuy=" << g_auto_buy << "\n";
        out << "autoRefresh=" << g_auto_refresh << "\n";
        out << "autoBuyChosen=" << g_auto_buy_chosen << "\n";
        
        out << "cardWarning=" << g_card_warning << "\n";
        out << "warningThreshold=" << g_warning_threshold << "\n";
        for (int i = 1; i <= 6; i++) {
            out << "warningTier" << i << "=" << g_warning_tiers[i] << "\n";
        }

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
                else if (k == "cardPoolScale") g_cardPoolScale = std::stof(v);
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
                    if (idx >= 1 && idx <= 6) {
                        g_warning_tiers[idx] = (v == "1");
                    }
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

// =================================================================
// 3. 基础资源 (Hex Shader / Texture)
// =================================================================
class HexShader {
public:
    GLuint program = 0; 
    GLint resLoc = -1;
    void Init() {
        const char* vs = "#version 300 es\nlayout(location=0) in vec2 Position;\nlayout(location=1) in vec2 UV;\nout vec2 Frag_UV;\nuniform vec2 u_Res;\nvoid main() {\n    Frag_UV = UV;\n    vec2 ndc = (Position / u_Res) * 2.0 - 1.0;\n    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n}";
        const char* fs = "#version 300 es\nprecision mediump float;\nuniform sampler2D Texture;\nin vec2 Frag_UV;\nout vec4 Out_Color;\nfloat sdHex(vec2 p, float r) {\n    vec3 k = vec3(-0.866025, 0.5, 0.57735);\n    p = abs(p);\n    p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;\n    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);\n    return length(p)*sign(p.y);\n}\nvoid main() {\n    vec2 p = (Frag_UV - 0.5) * 2.0;\n    float d = sdHex(vec2(p.y, p.x), 0.92);\n    float alpha = 1.0 - smoothstep(-0.02, 0.02, d);\n    if(alpha <= 0.0) discard;\n    Out_Color = texture(Texture, Frag_UV) * alpha;\n}";
        program = glCreateProgram(); GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v); 
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f); 
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program); 
        resLoc = glGetUniformLocation(program, "u_Res"); glDeleteShader(v); glDeleteShader(f);
    }
    void Cleanup() { if (program) { glDeleteProgram(program); program = 0; } }
} g_HexShader;

bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c; unsigned char* data = stbi_load(filename, &w, &h, &c, 4); if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); 
    stbi_image_free(data); return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program); glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f; g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(18.0f * g_autoScale, 12.0f, 100.0f); 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture(); io.Fonts->Clear(); 
    ImFontConfig config;
    float highResFactor = 2.5f; 
    if (targetSize * highResFactor > 90.0f) { highResFactor = 90.0f / targetSize; }
    highResFactor = std::max(1.2f, highResFactor); 
    config.OversampleH = 1; config.OversampleV = 1; config.PixelSnapH = true;
    
    const char* fonts[] = { "/system/fonts/SysSans-Hans-Regular.ttf", "/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/DroidSansFallback.ttf" };
    bool loaded = false;
    for(const char* path : fonts) {
        if (access(path, R_OK) == 0) { 
            ImFont* font = io.Fonts->AddFontFromFileTTF(path, targetSize * highResFactor, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()); 
            if (font) { font->Scale = 1.0f / highResFactor; loaded = true; break; }
        }
    }
    if(!loaded) { ImFont* font = io.Fonts->AddFontDefault(); if (font) font->Scale = 1.0f / highResFactor; }
    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture(); g_current_rendered_size = targetSize;
}

// =================================================================
// 4. 核心物理交互引擎
// =================================================================
void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, float& t_x, float& t_y, float& t_scale, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx_unscaled, float h_dy_unscaled, float c_dx_unscaled, float c_dy_unscaled, float hitMinX_unscaled, float hitMinY_unscaled, float hitMaxX_unscaled, float hitMaxY_unscaled, bool locked, bool* isOpen) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        float scaleHandleX = out_x + h_dx_unscaled * out_scale; float scaleHandleY = out_y + h_dy_unscaled * out_scale; ImVec2 p_scale(scaleHandleX, scaleHandleY);
        float closeHandleX = out_x + c_dx_unscaled * out_scale; float closeHandleY = out_y + c_dy_unscaled * out_scale; ImVec2 p_close(closeHandleX, closeHandleY);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (isOpen && ImLengthSqr(io.MousePos - p_close) < (4900.0f * g_autoScale * g_autoScale)) { *isOpen = false; return; }
            else if (ImLengthSqr(io.MousePos - p_scale) < (4900.0f * g_autoScale * g_autoScale)) { isScaling = true; ImVec2 targetHandleCenter(t_x + h_dx_unscaled * t_scale, t_y + h_dy_unscaled * t_scale); scaleDragOffset = io.MousePos - targetHandleCenter; } 
            else { ImRect area(ImVec2(out_x + hitMinX_unscaled * out_scale, out_y + hitMinY_unscaled * out_scale), ImVec2(out_x + hitMaxX_unscaled * out_scale, out_y + hitMaxY_unscaled * out_scale)); if (area.Contains(io.MousePos)) { isDragging = true; dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); } }
        }
        if (isScaling) { if (ImGui::IsMouseDown(0)) { ImVec2 targetHandleCenter = io.MousePos - scaleDragOffset; float targetDist = sqrtf(powf(targetHandleCenter.x - t_x, 2) + powf(targetHandleCenter.y - t_y, 2)); float baseHandleDist = sqrtf(h_dx_unscaled * h_dx_unscaled + h_dy_unscaled * h_dy_unscaled); t_scale = std::clamp(targetDist / baseHandleDist, 0.2f, 5.0f); } else { isScaling = false; } }
        if (isDragging && !isScaling) { if (ImGui::IsMouseDown(0)) { t_x = io.MousePos.x + dragOffset.x; t_y = io.MousePos.y + dragOffset.y; } else { isDragging = false; } }
    }
    float smoothness = 1.0f - expf(-20.0f * io.DeltaTime); out_x = ImLerp(out_x, t_x, smoothness); out_y = ImLerp(out_y, t_y, smoothness); out_scale = ImLerp(out_scale, t_scale, smoothness);
}

void DrawScaleHandle(ImDrawList* d, ImVec2 p_handle, bool isScaling) {
    ImU32 coreColor = isScaling ? IM_COL32(0, 255, 180, 255) : IM_COL32(255, 255, 255, 255);
    d->AddCircleFilled(p_handle, 16.0f * g_autoScale, IM_COL32(255, 215, 0, 240)); d->AddCircleFilled(p_handle, 6.0f * g_autoScale, coreColor); d->AddCircle(p_handle, 20.0f * g_autoScale, IM_COL32(255, 215, 0, 150), 32, 2.5f * g_autoScale);
}

void DrawCloseHandle(ImDrawList* d, ImVec2 p_handle, bool* isOpen) {
    if (!isOpen) return; ImGuiIO& io = ImGui::GetIO(); float cr = 13.0f * g_autoScale; static std::map<void*, float> hover_map; 
    bool cHov = ImLengthSqr(io.MousePos - p_handle) < (cr*cr * 2.5f); hover_map[isOpen] = ImLerp(hover_map[isOpen], cHov ? 1.0f : 0.0f, 1.0f - expf(-15.0f * io.DeltaTime)); float cha = hover_map[isOpen];
    d->AddCircleFilled(p_handle, cr, IM_COL32(200 + 55*cha, 50, 50, 200 + 55*cha));
    d->AddLine(p_handle - ImVec2(cr*0.35f, cr*0.35f), p_handle + ImVec2(cr*0.35f, cr*0.35f), IM_COL32_WHITE, 2.5f * g_autoScale); d->AddLine(p_handle + ImVec2(cr*0.35f, -cr*0.35f), p_handle - ImVec2(cr*0.35f, -cr*0.35f), IM_COL32_WHITE, 2.5f * g_autoScale);
}

// =================================================================
// 5. 纯悬浮预测模块 
// =================================================================
void DrawPurePredictEnemy() {
    static float alpha = 0.0f; alpha = ImLerp(alpha, g_predict_enemy ? 1.0f : 0.0f, 1.0f - expf(-20.0f * ImGui::GetIO().DeltaTime));
    if (alpha < 0.01f) return;
    ImDrawList* d = ImGui::GetForegroundDrawList(); static float t_x = g_enemy_X, t_y = g_enemy_Y, t_scale = g_enemy_Scale; static bool first = true; 
    if (first) { t_x = g_enemy_X; t_y = g_enemy_Y; t_scale = g_enemy_Scale; first = false; }
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;

    // 动态获取外部线程扫描出的名字
    char currentOpponentName[128];
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        snprintf(currentOpponentName, sizeof(currentOpponentName), "%s", g_nextOpponentName);
    }

    ImFont* font = ImGui::GetFont(); float fsz = ImGui::GetFontSize() * 1.5f; ImVec2 tSz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, currentOpponentName);
    float pad = 15.0f * g_autoScale; float baseW = tSz.x + pad * 2.0f; float baseH = tSz.y + pad * 2.0f;
    float h_dx = baseW + 10.0f * g_autoScale; float h_dy = baseH * 0.5f;

    if (g_predict_enemy) HandleGridInteraction(g_enemy_X, g_enemy_Y, g_enemy_Scale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, h_dx, h_dy, 0, 0, 0, 0, baseW, baseH, g_boardLocked, nullptr);

    float curW = baseW * g_enemy_Scale; float curH = baseH * g_enemy_Scale;
    d->AddRectFilled(ImVec2(g_enemy_X, g_enemy_Y), ImVec2(g_enemy_X + curW, g_enemy_Y + curH), IM_COL32(10, 15, 20, 160 * alpha), curH * 0.5f);
    float r, g, b; ImGui::ColorConvertHSVtoRGB(fmodf((float)ImGui::GetTime() * 0.5f, 1.0f), 0.8f, 1.0f, r, g, b);
    d->AddRect(ImVec2(g_enemy_X, g_enemy_Y), ImVec2(g_enemy_X + curW, g_enemy_Y + curH), IM_COL32(r*255, g*255, b*255, 255 * alpha), curH * 0.5f, 0, 2.0f * g_autoScale * g_enemy_Scale);
    d->AddText(font, fsz * g_enemy_Scale, ImVec2(g_enemy_X + pad * g_enemy_Scale, g_enemy_Y + pad * g_enemy_Scale), IM_COL32(255, 80, 80, 255 * alpha), currentOpponentName);
    if (!g_boardLocked && alpha > 0.9f) DrawScaleHandle(d, ImVec2(g_enemy_X + h_dx * g_enemy_Scale, g_enemy_Y + h_dy * g_enemy_Scale), isScaling);
}

void DrawPurePredictHex() {
    static float alpha = 0.0f; alpha = ImLerp(alpha, g_predict_hex ? 1.0f : 0.0f, 1.0f - expf(-20.0f * ImGui::GetIO().DeltaTime));
    if (alpha < 0.01f) return;
    ImDrawList* d = ImGui::GetForegroundDrawList(); static float t_x = g_hex_X, t_y = g_hex_Y, t_scale = g_hex_Scale; static bool first = true; 
    if (first) { t_x = g_hex_X; t_y = g_hex_Y; t_scale = g_hex_Scale; first = false; }
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    ImFont* font = ImGui::GetFont(); float fsz = ImGui::GetFontSize() * 1.5f; 
    const char* t1 = (const char*)u8"银色"; const char* t2 = (const char*)u8"金色"; const char* t3 = (const char*)u8"彩色";
    ImVec2 sz1 = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, t1); ImVec2 sz2 = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, t2); ImVec2 sz3 = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, t3);
    float gap = 20.0f * g_autoScale; float pad = 15.0f * g_autoScale; float baseW = sz1.x + sz2.x + sz3.x + gap * 2.0f + pad * 2.0f; float baseH = sz1.y + pad * 2.0f;
    float h_dx = baseW + 10.0f * g_autoScale; float h_dy = baseH * 0.5f;
    if (g_predict_hex) HandleGridInteraction(g_hex_X, g_hex_Y, g_hex_Scale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, h_dx, h_dy, 0, 0, 0, 0, baseW, baseH, g_boardLocked, nullptr);
    float curW = baseW * g_hex_Scale; float curH = baseH * g_hex_Scale;
    d->AddRectFilled(ImVec2(g_hex_X, g_hex_Y), ImVec2(g_hex_X + curW, g_hex_Y + curH), IM_COL32(10, 15, 20, 160 * alpha), curH * 0.5f);
    float cx = g_hex_X + pad * g_hex_Scale; float cy = g_hex_Y + pad * g_hex_Scale; float cFsz = fsz * g_hex_Scale;
    d->AddText(font, cFsz, ImVec2(cx, cy), IM_COL32(200, 200, 200, 255 * alpha), t1); cx += (sz1.x + gap) * g_hex_Scale;
    d->AddText(font, cFsz, ImVec2(cx, cy), IM_COL32(255, 215, 0, 255 * alpha), t2); cx += (sz2.x + gap) * g_hex_Scale;
    d->AddText(font, cFsz, ImVec2(cx, cy), IM_COL32(255, 100, 255, 255 * alpha), t3);
    if (!g_boardLocked && alpha > 0.9f) DrawScaleHandle(d, ImVec2(g_hex_X + h_dx * g_hex_Scale, g_hex_Y + h_dy * g_hex_Scale), isScaling);
}

// =================================================================
// 6. 整合覆盖层 (金币等级 ESP + 卡牌预警)
// =================================================================
void DrawPlayersOverlay() {
    bool is_active = g_esp_level || g_card_warning; static float alpha = 0.0f; alpha = ImLerp(alpha, is_active ? 1.0f : 0.0f, 1.0f - expf(-20.0f * ImGui::GetIO().DeltaTime));
    if (alpha < 0.01f) return;
    ImDrawList* d = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
    static float t_x = g_players_X, t_y = g_players_Y, t_scale = g_players_Scale; static bool first = true; 
    if (first) { t_x = g_players_X; t_y = g_players_Y; t_scale = g_players_Scale; first = false; }
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;

    float avatar_r = 20.0f * g_autoScale; float row_h = avatar_r * 2.8f; 
    float baseW = avatar_r * 2.0f; if (g_esp_level) baseW += 120.0f * g_autoScale; if (g_card_warning) baseW += 80.0f * g_autoScale;
    float baseH = row_h * 8.0f; float h_dx = baseW + 15.0f * g_autoScale; float h_dy = baseH + 15.0f * g_autoScale;

    if (is_active) HandleGridInteraction(g_players_X, g_players_Y, g_players_Scale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, h_dx, h_dy, 0, 0, -avatar_r, -avatar_r, baseW + avatar_r, baseH + avatar_r, g_boardLocked, nullptr);
    if (!g_boardLocked && alpha > 0.9f) DrawScaleHandle(d, ImVec2(g_players_X + h_dx * g_players_Scale, g_players_Y + h_dy * g_players_Scale), isScaling);

    float curAvatarR = avatar_r * g_players_Scale; float curRowH = row_h * g_players_Scale; ImFont* font = ImGui::GetFont();

    for (int i = 0; i < 8; i++) {
        float cx = g_players_X + curAvatarR; float cy = g_players_Y + i * curRowH + curAvatarR;
        d->AddCircleFilled(ImVec2(cx, cy), curAvatarR, IM_COL32(30, 35, 45, 180 * alpha)); d->AddCircle(ImVec2(cx, cy), curAvatarR, IM_COL32(80, 90, 100, 255 * alpha), 32, 1.5f * g_autoScale * g_players_Scale);
        float draw_x = cx + curAvatarR + 10.0f * g_autoScale * g_players_Scale;
        static float esp_anim[8] = {0.0f}; esp_anim[i] = ImLerp(esp_anim[i], g_esp_level ? 1.0f : 0.0f, 1.0f - expf(-15.0f * io.DeltaTime));
        
        if (esp_anim[i] > 0.01f) {
            float fsz = ImGui::GetFontSize() * g_players_Scale * 1.1f; char buf[32]; 
            snprintf(buf, sizeof(buf), "G:28/LV5");
            ImVec2 tSz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, buf);
            d->AddRectFilled(ImVec2(draw_x, cy - tSz.y*0.6f), ImVec2(draw_x + tSz.x + 10.0f*g_autoScale*g_players_Scale, cy + tSz.y*0.6f), IM_COL32(15, 20, 25, 160 * alpha * esp_anim[i]), 4.0f * g_autoScale);
            d->AddText(font, fsz, ImVec2(draw_x + 5.0f*g_autoScale*g_players_Scale, cy - tSz.y*0.5f), IM_COL32(255, 215, 0, 255 * alpha * esp_anim[i]), buf);
            draw_x += 120.0f * g_autoScale * g_players_Scale * esp_anim[i];
        }

        bool is_warned = (g_card_warning && i == 2 && g_warning_tiers[4]); static float warn_anim[8] = {0.0f};
        warn_anim[i] = ImLerp(warn_anim[i], is_warned ? 1.0f : 0.0f, 1.0f - expf(-15.0f * io.DeltaTime));
        
        if (warn_anim[i] > 0.01f && g_textureLoaded) {
            float img_sz = 40.0f * g_autoScale * g_players_Scale; float img_y = cy - img_sz * 0.5f; float final_a = alpha * warn_anim[i];
            d->AddImageRounded((ImTextureID)(intptr_t)g_heroTexture, ImVec2(draw_x, img_y), ImVec2(draw_x + img_sz, img_y + img_sz), ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,255*final_a), 6.0f * g_autoScale * g_players_Scale);
            d->AddRect(ImVec2(draw_x, img_y), ImVec2(draw_x + img_sz, img_y + img_sz), IM_COL32(255, 50, 50, 200*final_a), 6.0f * g_autoScale * g_players_Scale, 0, 2.0f * g_autoScale * g_players_Scale);
            float bg_h = 14.0f * g_autoScale * g_players_Scale; d->AddRectFilled(ImVec2(draw_x, img_y + img_sz - bg_h), ImVec2(draw_x + img_sz, img_y + img_sz), IM_COL32(0, 0, 0, 220*final_a), 6.0f * g_autoScale * g_players_Scale, ImDrawFlags_RoundCornersBottom);
            float fsz = ImGui::GetFontSize() * 0.8f * g_players_Scale; char buf[16]; snprintf(buf, sizeof(buf), "7/12");
            ImVec2 tSz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, buf);
            d->AddText(font, fsz, ImVec2(draw_x + (img_sz - tSz.x)*0.5f, img_y + img_sz - bg_h + (bg_h - tSz.y)*0.5f), IM_COL32(255, 100, 100, 255*final_a), buf);
        }
    }
}

// =================================================================
// 自动拿牌悬浮窗 & 绚丽霓虹按钮等
// =================================================================
bool AnimatedNeonButton(ImDrawList* d, const char* label, ImVec2 pos, ImVec2 size, int id, float scale, bool* v) {
    ImGuiIO& io = ImGui::GetIO(); ImRect bb(pos, pos + size); bool hovered = bb.Contains(io.MousePos); bool clicked = false;
    if (hovered && ImGui::IsMouseClicked(0)) { clicked = true; if (v) *v = !(*v); } bool held = hovered && ImGui::IsMouseDown(0);
    static std::map<int, float> anims; float target = (v && *v) ? 1.0f : (held ? 1.0f : (hovered ? 0.6f : 0.0f));
    anims[id] = ImLerp(anims[id], target, 1.0f - expf(-20.0f * io.DeltaTime)); float a = anims[id]; float rR = size.y * 0.5f; 
    if (v && *v) {
        float time = (float)ImGui::GetTime(); float hue = fmodf(time * 0.5f + id * 0.2f, 1.0f); float pulse = sinf(time * 8.0f) * 0.2f + 0.3f; 
        float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
        ImU32 col_glow = IM_COL32(rf*255, gf*255, bf*255, 255); ImU32 col_bg = IM_COL32(rf*255, gf*255, bf*255, pulse * 255);
        d->AddRectFilled(bb.Min, bb.Max, col_bg, rR); d->AddRect(bb.Min, bb.Max, col_glow, rR, ImDrawFlags_RoundCornersAll, 2.5f * scale * g_autoScale); d->AddRect(bb.Min, bb.Max, col_glow & 0x00FFFFFF | 0x60000000, rR, ImDrawFlags_RoundCornersAll, 6.0f * scale * g_autoScale); 
    } else {
        ImU32 bg = IM_COL32(30 + 30*a, 35 + 35*a, 40 + 40*a, 200 + 55*a); ImU32 border = IM_COL32(80, 80, 80, 150 + 105*a);
        d->AddRectFilled(bb.Min, bb.Max, bg, rR); d->AddRect(bb.Min, bb.Max, border, rR, ImDrawFlags_RoundCornersAll, 1.5f * scale * g_autoScale); 
    }
    ImFont* font = ImGui::GetFont(); float scaledFontSize = ImGui::GetFontSize() * scale; ImVec2 textSize = font->CalcTextSizeA(scaledFontSize, FLT_MAX, 0.0f, label);
    ImVec2 textPos = pos + ImVec2((size.x - textSize.x)*0.5f, (size.y - textSize.y)*0.5f); d->AddText(font, scaledFontSize, textPos, IM_COL32_WHITE, label, NULL);
    return clicked; 
}

void DrawAutoBuyWindow() {
    static float alpha = 0.0f; alpha = ImLerp(alpha, g_auto_buy ? 1.0f : 0.0f, 1.0f - expf(-20.0f * ImGui::GetIO().DeltaTime));
    if (alpha < 0.01f) return;
    ImDrawList* d = ImGui::GetForegroundDrawList(); static float t_x = g_autoW_X, t_y = g_autoW_Y, t_scale = g_autoW_Scale; static bool first = true; 
    if (first) { t_x = g_autoW_X; t_y = g_autoW_Y; t_scale = g_autoW_Scale; first = false; }
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    float baseW = 300.0f * g_autoScale; float baseH = 65.0f * g_autoScale; float h_dx = baseW + 20.0f * g_autoScale; float h_dy = baseH * 0.5f;
    if (g_auto_buy) HandleGridInteraction(g_autoW_X, g_autoW_Y, g_autoW_Scale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, h_dx, h_dy, 0, 0, 0, 0, baseW, baseH, g_boardLocked, nullptr);
    float curW = baseW * g_autoW_Scale; float curH = baseH * g_autoW_Scale; ImVec2 p_min(g_autoW_X, g_autoW_Y); ImVec2 p_max(g_autoW_X + curW, g_autoW_Y + curH); float rounding = curH * 0.5f;
    d->AddRectFilled(p_min, p_max, IM_COL32(15, 20, 25, 240 * alpha), rounding); d->AddRect(p_min, p_max, IM_COL32(0, 255, 150, 200 * alpha), rounding, 0, 2.0f * g_autoScale * g_autoW_Scale);
    if (!g_boardLocked && alpha > 0.9f) DrawScaleHandle(d, ImVec2(g_autoW_X + h_dx * g_autoW_Scale, g_autoW_Y + h_dy * g_autoW_Scale), isScaling);
    float btnW = (baseW - 40.0f * g_autoScale) * 0.5f * g_autoW_Scale; float btnH = (baseH - 20.0f * g_autoScale) * g_autoW_Scale; float gap = 10.0f * g_autoScale * g_autoW_Scale;
    ImVec2 b1_pos = p_min + ImVec2(15.0f * g_autoScale * g_autoW_Scale, 10.0f * g_autoScale * g_autoW_Scale); ImVec2 b2_pos = b1_pos + ImVec2(btnW + gap, 0);
    AnimatedNeonButton(d, (const char*)u8"自动刷新", b1_pos, ImVec2(btnW, btnH), 101, g_autoW_Scale, &g_auto_refresh); AnimatedNeonButton(d, (const char*)u8"自动拿天选", b2_pos, ImVec2(btnW, btnH), 102, g_autoW_Scale, &g_auto_buy_chosen);
}

void DrawCardPool() { /* ... 卡库渲染逻辑 ... */ }

void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool firstFrame = true;
    if (firstFrame) { t_x = g_startX; t_y = g_startY; t_scale = g_boardManualScale; firstFrame = false; }
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;   

    float baseSz = 38.0f * g_boardScale * g_autoScale;
    float baseXStep = baseSz * 1.73205f; float baseYStep = baseSz * 1.5f;
    float h_dx = 7.0f * baseXStep; float h_dy = 1.5f * baseYStep;              
    float c_dx = -baseXStep * 0.8f; float c_dy = 1.5f * baseYStep;              

    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale,
                          isDragging, isScaling, dragOffset, scaleDragOffset,
                          h_dx, h_dy, c_dx, c_dy, -baseSz*2, -baseSz*2, 7.5f*baseXStep + baseSz*2, 3.0f*baseYStep + baseSz*2, g_boardLocked, &g_esp_board);
    if (!g_esp_board) return;

    float curSz = baseSz * g_boardManualScale; float curXStep = baseXStep * g_boardManualScale; float curYStep = baseYStep * g_boardManualScale;
    float time = (float)ImGui::GetTime();

    if (!g_boardLocked) {
        DrawScaleHandle(d, ImVec2(g_startX + h_dx * g_boardManualScale, g_startY + h_dy * g_boardManualScale), isScaling);
        DrawCloseHandle(d, ImVec2(g_startX + c_dx * g_boardManualScale, g_startY + c_dy * g_boardManualScale), &g_esp_board);
    }

    int localBoard[4][7];
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        memcpy(localBoard, g_enemyBoard, sizeof(g_enemyBoard));
    }

    for(int r = 0; r < 4; r++) {
        for(int c = 0; c < 7; c++) {
            float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0); float cy = g_startY + r * curYStep;
            float hue = fmodf(time * 0.3f + (cx + cy) * 0.0008f, 1.0f); float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);

            if(localBoard[r][c]) {
                if (g_textureLoaded) DrawHero(d, ImVec2(cx, cy), curSz * 0.95f); 
                ImFont* font = ImGui::GetFont(); float lvlFsz = ImGui::GetFontSize() * 2.5f * g_boardManualScale; const char* lvlTxt = "1/3";
                ImVec2 tSz = font->CalcTextSizeA(lvlFsz, FLT_MAX, 0.0f, lvlTxt); ImVec2 txtPos(cx - tSz.x*0.5f, cy + curSz * 0.4f);
                d->AddText(font, lvlFsz, txtPos + ImVec2(2.0f, 2.0f), IM_COL32(0,0,0,255), lvlTxt); d->AddText(font, lvlFsz, txtPos, IM_COL32(255, 215, 0, 255), lvlTxt); 
            }
            
            ImVec2 pts[6];
            for(int i = 0; i < 6; i++) { float a = (60.0f * i - 30.0f) * (M_PI / 180.0f); pts[i] = ImVec2(cx + curSz * cosf(a), cy + curSz * sinf(a)); }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 220), ImDrawFlags_Closed, 2.5f * g_autoScale);
        }
    }
}

void DrawBench() { /* ... 备战席 ... */ }
void DrawShop() { /* ... 商店 ... */ }

// =================================================================
// 7. 顶级定制菜单 UI 控件 与 状态显示
// =================================================================
bool ModernToggle(const char* label, bool* v, int idx) { /* ... Toggle 源码保持原样 ...*/ 
    ImGuiWindow* window = ImGui::GetCurrentWindow(); const ImGuiStyle& style = ImGui::GetStyle(); const ImGuiID id = window->GetID(label); const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight() * 0.85f; float w = h * 2.1f; const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, style.FramePadding.y); bool is_clipped = !ImGui::ItemAdd(bb, id); bool pressed = false;
    if (!is_clipped) { bool hovered, held; pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held); if (pressed) { *v = !(*v); } }
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f; 
    if (!is_clipped) {
        ImVec4 col_bg = ImLerp(ImVec4(0.20f, 0.22f, 0.27f, 1.0f), ImVec4(0.00f, 0.85f, 0.55f, 1.0f), g_anim[idx]);
        window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(col_bg), h*0.5f); window->DrawList->AddRect(bb.Min, bb.Min + ImVec2(w, h), IM_COL32(0, 0, 0, 80), h*0.5f, 0, 1.0f);
        float handle_radius = h * 0.5f - 2.5f; ImVec2 handle_center = bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f);
        window->DrawList->AddCircleFilled(handle_center + ImVec2(0, 1.5f), handle_radius, IM_COL32(0, 0, 0, 90)); window->DrawList->AddCircleFilled(handle_center, handle_radius, IM_COL32_WHITE);
        ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y*0.5f), label);
    } return pressed;
}

bool ModernAnimatedFolder(const char* label, bool* state, int child_item_count) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); ImGuiID id = window->GetID(label); ImVec2 pos = window->DC.CursorPos; ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 1.2f);
    const ImRect bb(pos, pos + size); ImGui::ItemSize(bb); bool is_clipped = !ImGui::ItemAdd(bb, id); bool hovered = false, held = false; 
    if (!is_clipped) { bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held); if (pressed) { *state = !(*state); } }
    static std::map<ImGuiID, float> anim_map; float& anim = anim_map[id]; anim = ImLerp(anim, *state ? 1.0f : 0.0f, 1.0f - expf(-18.0f * ImGui::GetIO().DeltaTime));
    if (!is_clipped) {
        ImU32 bg_col = hovered ? IM_COL32(50, 60, 75, 200) : IM_COL32(40, 48, 60, 150); window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, 8.0f * g_autoScale);
        float cx = bb.Min.x + 15.0f * g_autoScale; float cy = bb.Min.y + size.y * 0.5f; float arrow_sz = 5.0f * g_autoScale; float ang = anim * 1.5708f; 
        ImVec2 p1(cx + cosf(ang)*arrow_sz, cy + sinf(ang)*arrow_sz); ImVec2 p2(cx + cosf(ang + 2.094f)*arrow_sz, cy + sinf(ang + 2.094f)*arrow_sz); ImVec2 p3(cx + cosf(ang - 2.094f)*arrow_sz, cy + sinf(ang - 2.094f)*arrow_sz);
        window->DrawList->AddTriangleFilled(p1, p2, p3, IM_COL32(200, 200, 200, 255)); window->DrawList->AddText(ImVec2(cx + 15.0f * g_autoScale, bb.Min.y + (size.y - ImGui::GetFontSize())*0.5f), IM_COL32_WHITE, label);
    }
    if (anim > 0.01f) {
        float exact_target_height = (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y) * child_item_count + ImGui::GetStyle().ItemSpacing.y * 1.0f; float current_height = (float)(int)(exact_target_height * anim); 
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, anim); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15.0f * (1.0f - anim)); ImGui::BeginChild(id + 1, ImVec2(0, current_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground); return true;
    } return false;
}
void EndModernAnimatedFolder() { ImGui::EndChild(); ImGui::PopStyleVar(); }

void ModernNumberAdjuster(const char* label, int* v, int v_min, int v_max) { /* ... */ }
void ModernFloatAdjuster(const char* label, float* v, float v_min, float v_max, float step = 0.1f) { /* ... */ }
void ModernTierSelector() { /* ... */ }

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding = 16.0f * g_autoScale; style.FrameRounding = 8.0f * g_autoScale; style.PopupRounding = 8.0f * g_autoScale;
    style.ItemSpacing = ImVec2(12 * g_autoScale, 16 * g_autoScale); style.WindowPadding = ImVec2(16 * g_autoScale, 16 * g_autoScale); 
    style.WindowBorderSize = 1.0f; style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.85f); style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.90f); style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.13f, 0.15f, 0.90f);

    static bool firstMenuOpen = true; if (firstMenuOpen) { ImGui::SetNextWindowCollapsed(g_menuCollapsed); firstMenuOpen = false; }
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v3.1 (极速纯净版)", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        g_menuX = ImGui::GetWindowPos().x; g_menuY = ImGui::GetWindowPos().y;
        
        if (ImGui::IsMouseReleased(0)) {
            float curW = ImGui::GetWindowSize().x; float curH = ImGui::GetWindowSize().y;
            if (std::abs(curW - g_menuW) > 5.0f || std::abs(curH - g_menuH) > 5.0f) { g_menuW = curW; g_menuH = curH; g_scale = curW / (350.0f * g_autoScale); }
        }
        g_menuCollapsed = ImGui::IsWindowCollapsed();

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale(g_scale);
            
            // -------------------------------------------------------------
            // [新增] 运行环境与目标进程连接状态显示 (极其重要)
            // -------------------------------------------------------------
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] VSYNC 模式已开启 | FPS: %.1f", io.Framerate);
            
            // 只有看到这两行绿字，才代表你真正的跨进程读取连上了！
            if (g_gamePID > 0) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[+] 游戏已连接 PID: %d", g_gamePID);
                if (g_libBase > 0) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[+] 基址: 0x%lX", g_libBase);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[-] 正在扫描特征基址...");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[-] 未检测到金铲铲或无 Root 权限!");
            }
            ImGui::Separator();
            
            static bool header_pred = true;
            if (ModernAnimatedFolder((const char*)u8"预测系统", &header_pred, 2)) {
                ModernToggle((const char*)u8"预测对手", &g_predict_enemy, 1); 
                ModernToggle((const char*)u8"预测海克斯", &g_predict_hex, 2); 
                EndModernAnimatedFolder();
            }
            
            static bool header_esp = true;
            if (ModernAnimatedFolder((const char*)u8"投食透视", &header_esp, 4)) {
                ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); 
                ModernToggle((const char*)u8"备战席投食", &g_esp_bench, 4); 
                ModernToggle((const char*)u8"商店投食", &g_esp_shop, 5);
                ModernToggle((const char*)u8"金币等级投食", &g_esp_level, 9); 
                EndModernAnimatedFolder();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            ModernToggle((const char*)u8"锁定所有窗体", &g_boardLocked, 8); 
            ModernToggle((const char*)u8"云端自动拿牌", &g_auto_buy, 6); 
            ModernToggle((const char*)u8"牌库透视显示", &g_show_card_pool, 10);
            
            // ... 省略展开动画 ... 
            
            if (ModernToggle((const char*)u8"极速退游 (秒退)", &g_instant, 7)) {
                if (g_instant) ImGui::OpenPopup((const char*)u8"警告: 确认退出?");
            }
            
            ImGui::SetNextWindowSize(ImVec2(320 * g_autoScale * g_scale, 0));
            if (ImGui::BeginPopupModal((const char*)u8"警告: 确认退出?", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::SetWindowFontScale(g_scale);
                const char* warn_txt = (const char*)u8"你确定要立即强制退出游戏吗？";
                float txt_w = ImGui::CalcTextSize(warn_txt).x; ImGui::SetCursorPosX((ImGui::GetWindowSize().x - txt_w) * 0.5f);
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", warn_txt);
                
                ImGui::Spacing(); ImGui::Spacing();
                float btnW = 120 * g_autoScale * g_scale; float btnH = 45 * g_autoScale * g_scale; float btn_total_w = btnW * 2.0f + style.ItemSpacing.x;
                ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btn_total_w) * 0.5f);
                
                if (ImGui::Button((const char*)u8"确定退出", ImVec2(btnW, btnH))) { 
                    g_triggerUserExit = true; g_instant = false; ImGui::CloseCurrentPopup(); 
                }
                ImGui::SameLine();
                if (ImGui::Button((const char*)u8"取消", ImVec2(btnW, btnH))) { 
                    g_instant = false; ImGui::CloseCurrentPopup(); 
                }
                ImGui::EndPopup();
            }
            
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 55 * g_autoScale))) SaveConfig(); 
            ImGui::Dummy(ImVec2(0.0f, 15.0f * g_autoScale * g_scale));
        }
    }
    ImGui::End();
}

// =================================================================
// [核心原理] 8. 纯外部轮询引擎 (请仔细阅读下面的注释)
// =================================================================
extern bool g_running_flag;

void GameLogicThread() {
    // 【前提条件】：这段代码如果是在普通的 APK 壳子里跑，它是永远拿不到 com.tencent.jkchess 的 PID 的。
    // 必须在 Root 终端 (su) 下，用 ./xxx.sh 或 ./main 运行。
    while (g_running_flag && g_gamePID == -1) {
        g_gamePID = GetTargetPID("com.tencent.jkchess");
        if (g_gamePID != -1) {
            // 获取真正的基址 (由于腾讯反挂策略，基址可能在 libtersafe.so 或 libil2cpp.so 或匿名的 executable 内存里)
            g_libBase = GetModuleBaseExternally(g_gamePID, "libil2cpp.so");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    auto lastBoardScanTime = std::chrono::steady_clock::now();
    
    while (g_running_flag) {
        if (g_gamePID <= 0 || g_libBase == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto now = std::chrono::steady_clock::now();

        // -------------------------------------------------------------
        // 【核心偏差解释】：为什么你给的地址读不出来数据？
        // 你在要求里写着：Hook 3f7f704, 调用 58543a8，调用 3da60c0 退游。
        // 这些全是“函数机器码所在的内存地址”！
        // 如果我用外部 ReadMem 去读 0x3f7f704，读出来的不是任何对象，而是一串指令，比如 0xA9BF7BFD (STP)。
        // 外部读取无法 "Hook" 或 "调用"！它只能读 "全局数据指针(Static Pointers)"。
        // -------------------------------------------------------------

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBoardScanTime).count() >= 2000) {
            lastBoardScanTime = now;
            
            // 下面这行代码是一个【必须由你自己逆向修改的数据指针占位符】。
            // 假设你通过 CE/IDA 找到了某个真正的存放 GameManager 对象的指针，
            // 比如 0x8888888 (注意，这是指数据地址，千万别再放函数的地址进来了)。
            uint64_t dataPointerOffset = 0x8888888; // <--- 【你需要修改这里】
            
            // 从基址 + 静态偏移，拿到真正堆内存里的大对象指针
            uint64_t playerListPtr = ReadMem<uint64_t>(g_libBase + dataPointerOffset); 
            
            if (playerListPtr != 0 && playerListPtr != 0xA9BF7BFD) { // 简单防止你放了函数地址进来
                std::lock_guard<std::mutex> lock(g_dataMutex);
                
                // 找到数据对象后，剩下的 +0x50, +0x18 等对象内部偏移依然是有效的！
                for (int i = 0; i < 8; i++) {
                    uint64_t objAddr = ReadMem<uint64_t>(playerListPtr + i * 8);
                    g_playersInfo[i].objAddr = objAddr;
                    if (objAddr != 0) {
                        g_playersInfo[i].isAI = ReadMem<bool>(objAddr + 0x58);
                        uint64_t dynAddr = ReadMem<uint64_t>(objAddr + 0x50);
                        g_playersInfo[i].dynAddr = dynAddr;
                        if (dynAddr) {
                            g_playersInfo[i].playerId = ReadMem<int>(dynAddr + 0x20);
                        }
                    }
                }
            }
        }

        // 外部极速退游：外部是无法强行“执行/调用”游戏内 0x3da60c0 那个 UserExit 函数的。
        // 所以截图里的人，是找到了负责控制“游戏投降”的一个 bool 变量的地址，直接把它从 0 改成 1。
        if (g_triggerUserExit.load()) {
            g_triggerUserExit.store(false);
            // 同样，如果你想用外部纯 C 退游，请找到投降布尔值的全局偏移：
            // uint64_t isSurrenderAddr = g_libBase + 0xAAAAAAA;
            // bool trigger = true;
            // WriteMemoryExternal(g_gamePID, isSurrenderAddr, &trigger, sizeof(bool));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20)); 
    }
}

// =================================================================
// 9. 主循环
// =================================================================
bool g_running_flag = true; 

int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    LoadConfig(); UpdateFontHD(true);  
    
    std::thread logicThread(GameLogicThread);
    std::thread it([&] { while(g_running_flag) { imgui.ProcessInputEvent(); std::this_thread::yield(); } });

    while (g_running_flag) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        
        imgui.BeginFrame(); 
        glDisable(GL_SCISSOR_TEST); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!g_resLoaded) { g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); g_textureLoaded = (g_heroTexture != 0); g_resLoaded = true; }
        
        DrawBoard(); DrawBench(); DrawShop();  
        DrawPurePredictEnemy(); DrawPurePredictHex(); DrawPlayersOverlay();   
        DrawAutoBuyWindow(); DrawCardPool(); DrawMenu();
        
        imgui.EndFrame(); 
        std::this_thread::yield();
    }
    
    g_HexShader.Cleanup(); g_running_flag = false; 
    if (it.joinable()) it.join(); if (logicThread.joinable()) logicThread.join(); 
    return 0;
}
