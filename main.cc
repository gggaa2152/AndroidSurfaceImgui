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
#include <map>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态 (保留所有 1600 行时期的变量)
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false, g_esp_level = false; 
bool g_auto_buy = false, g_instant = false, g_boardLocked = false; 
bool g_auto_refresh = false, g_auto_buy_chosen = false;

// 牌库与预警
bool g_show_card_pool = false;
int g_card_pool_rows = 2, g_card_pool_cols = 5;
float g_cardPoolX = 150.0f, g_cardPoolY = 150.0f, g_cardPoolScaleX = 1.0f, g_cardPoolScaleY = 1.0f, g_cardPoolAlpha = 1.0f; 

bool g_card_warning = false;
int g_warning_threshold = 6;   
bool g_warning_tiers[7] = {false, false, false, false, true, false, false}; 

// UI 缩放与动画
float g_scale = 1.0f, g_autoScale = 1.0f, g_current_rendered_size = 0.0f; 
bool g_menuCollapsed = false;

// 模块坐标 (全系统恢复)
float g_boardScale = 2.2f, g_boardManualScale = 1.0f, g_startX = 400.0f, g_startY = 400.0f;    
float g_menuX = 100.0f, g_menuY = 100.0f, g_menuW = 350.0f, g_menuH = 550.0f; 
float g_benchX = 200.0f, g_benchY = 700.0f, g_benchScale = 1.0f;
float g_shopX = 200.0f, g_shopY = 850.0f, g_shopScale = 1.0f;
float g_enemy_X = 100.0f, g_enemy_Y = 100.0f, g_enemy_Scale = 1.0f;
float g_hex_X = 100.0f, g_hex_Y = 220.0f, g_hex_Scale = 1.0f;
float g_autoW_X = 300.0f, g_autoW_Y = 1000.0f, g_autoW_Scale = 1.0f;
float g_players_X = 1500.0f, g_players_Y = 200.0f, g_players_Scale = 1.0f;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false, g_resLoaded = false, g_needUpdateFontSafe = false;
ImFont *g_mainFont = nullptr, *g_hugeNumFont = nullptr;

int g_enemyBoard[4][7] = {{1,0,0,0,1,0,0},{0,1,0,1,0,0,0},{0,0,0,0,0,1,0},{1,0,1,0,1,0,1}};

// =================================================================
// 2. 配置与 8K 字体系统 (双引擎架构)
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (!out.is_open()) return;
    out << "predictEnemy=" << g_predict_enemy << "\npredictHex=" << g_predict_hex << "\nespBoard=" << g_esp_board << "\n";
    out << "espBench=" << g_esp_bench << "\nespShop=" << g_esp_shop << "\nespLevel=" << g_esp_level << "\n";
    out << "cardPoolScaleX=" << g_cardPoolScaleX << "\ncardPoolScaleY=" << g_cardPoolScaleY << "\ncardPoolAlpha=" << g_cardPoolAlpha << "\n";
    out << "autoBuy=" << g_auto_buy << "\nboardLocked=" << g_boardLocked << "\n";
    out.close();
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t p = line.find('='); if (p == std::string::npos) continue;
        std::string k = line.substr(0, p), v = line.substr(p+1);
        try {
            if (k == "espBoard") g_esp_board = (v == "1");
            else if (k == "showCardPool") g_show_card_pool = (v == "1");
            else if (k == "cardPoolScaleX") g_cardPoolScaleX = std::stof(v);
            else if (k == "cardPoolScaleY") g_cardPoolScaleY = std::stof(v);
            else if (k == "autoBuy") g_auto_buy = (v == "1");
            else if (k == "boardLocked") g_boardLocked = (v == "1");
        } catch(...) {}
    }
    in.close(); g_needUpdateFontSafe = true;
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.f) ? io.DisplaySize.y : 2400.f; 
    g_autoScale = screenH / 1080.f;
    float tSz = std::clamp(18.f * g_autoScale, 12.f, 60.f); 
    if (!force && std::abs(tSz - g_current_rendered_size) < 0.5f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture(); io.Fonts->Clear();
    ImFontConfig cM, cN; cM.OversampleH = 1; cN.OversampleH = 2; // 8K 均衡配置
    static const ImWchar nR[] = { 0x0020, 0x00FF, 0 }; 
    const char* fP = "/system/fonts/NotoSansCJK-Regular.ttc";

    if (access(fP, R_OK) == 0) {
        g_mainFont = io.Fonts->AddFontFromFileTTF(fP, tSz * 1.5f, &cM, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()); 
        if(g_mainFont) g_mainFont->Scale = 1.f/1.5f;
        g_hugeNumFont = io.Fonts->AddFontFromFileTTF(fP, tSz * 4.0f, &cN, nR); 
        if(g_hugeNumFont) g_hugeNumFont->Scale = 1.f/4.0f;
    } else { g_mainFont = io.Fonts->AddFontDefault(); }

    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture(); g_current_rendered_size = tSz;
}

// =================================================================
// 3. 通用物理引擎与基础绘制
// =================================================================
void InteractionEngine(float& x, float& y, float& sx, float& sy, float& tx, float& ty, float& tsx, float& tsy, bool isXY, bool locked, float hdx, float hdy) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 pS(x + hdx * sx, y + hdy * sy);
        static bool isD = false, isS = false; static ImVec2 dO, sO;
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (ImLengthSqr(io.MousePos - pS) < 4000.f * g_autoScale) { isS = true; sO = io.MousePos - ImVec2(tx + hdx * tsx, ty + hdy * tsy); }
            else if (ImRect(ImVec2(x-20, y-20), ImVec2(x + hdx * sx + 20, y + hdy * sy + 20)).Contains(io.MousePos)) { isD = true; dO = ImVec2(tx - io.MousePos.x, ty - io.MousePos.y); }
        }
        if (isS) { 
            if (ImGui::IsMouseDown(0)) { 
                ImVec2 t = io.MousePos - sO; 
                if(!isXY) { float d = sqrtf(powf(t.x-tx,2)+powf(t.y-ty,2)); tsx = tsy = std::clamp(d/sqrtf(hdx*hdx+hdy*hdy), 0.2f, 5.f); }
                else { tsx = std::clamp((t.x-tx)/hdx, 0.2f, 5.f); tsy = std::clamp((t.y-ty)/hdy, 0.2f, 5.f); }
            } else isS = false;
        }
        if (isD && !isS) { if (ImGui::IsMouseDown(0)) { tx = io.MousePos.x + dO.x; ty = io.MousePos.y + dO.y; } else isD = false; }
    }
    float sm = 1.f - expf(-20.f * io.DeltaTime);
    x = ImLerp(x, tx, sm); y = ImLerp(y, ty, sm); sx = ImLerp(sx, tsx, sm); sy = ImLerp(sy, tsy, sm);
}

void DrawScaleHandle(ImDrawList* d, ImVec2 p, bool isS) { d->AddCircleFilled(p, 16.f * g_autoScale, IM_COL32(255, 215, 0, 240)); d->AddCircleFilled(p, 6.f * g_autoScale, isS ? IM_COL32(0, 255, 180, 255) : IM_COL32_WHITE); }

// =================================================================
// 4. 高级业务组件 (ESP / 预测 / 覆盖层)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return; ImDrawList* d = ImGui::GetWindowDrawList();
    static float tx=g_startX, ty=g_startY, ts=g_boardManualScale;
    float bSz = 38.f*g_boardScale*g_autoScale, bX = bSz*1.732f, bY = bSz*1.5f;
    InteractionEngine(g_startX, g_startY, g_boardManualScale, g_boardManualScale, tx, ty, ts, ts, false, g_boardLocked, 7.f*bX, 1.5f*bY);
    if (!g_boardLocked) DrawScaleHandle(d, ImVec2(g_startX+7.f*bX*g_boardManualScale, g_startY+1.5f*bY*g_boardManualScale), false);
    for(int r=0; r<4; r++) for(int c=0; c<7; c++) {
        float cx = g_startX + (c*bX + (r%2?bX*0.5f:0))*g_boardManualScale, cy = g_startY + r*bY*g_boardManualScale;
        if(g_enemyBoard[r][c]) { DrawHero(d, ImVec2(cx,cy), bSz*g_boardManualScale*0.95f); d->AddText(g_hugeNumFont, 25.f*g_boardManualScale*g_autoScale, ImVec2(cx-10, cy+bSz*0.4f), IM_COL32(255,215,0,255), "1/3"); }
        ImVec2 pts[6]; for(int i=0; i<6; i++) { float a = (60.f*i-30.f)*M_PI/180.f; pts[i] = ImVec2(cx+bSz*g_boardManualScale*cosf(a), cy+bSz*g_boardManualScale*sinf(a)); }
        d->AddPolyline(pts, 6, IM_COL32(0,255,180,200), ImDrawFlags_Closed, 2.5f*g_autoScale);
    }
}

void DrawBench() {
    if (!g_esp_bench) return; ImDrawList* d = ImGui::GetWindowDrawList();
    static float tx=g_benchX, ty=g_benchY, ts=g_benchScale;
    float bSz = 40.f*g_autoScale;
    InteractionEngine(g_benchX, g_benchY, g_benchScale, g_benchScale, tx, ty, ts, ts, false, g_boardLocked, 9*bSz, bSz);
    for(int i=0; i<9; i++) {
        ImVec2 p(g_benchX+i*bSz*g_benchScale, g_benchY);
        d->AddRect(p, p+ImVec2(bSz,bSz)*g_benchScale, IM_COL32(255,255,255,100), 4.f);
        d->AddText(g_hugeNumFont, 18.f*g_benchScale*g_autoScale, p+ImVec2(5,5), IM_COL32_WHITE, "3");
    }
}

void DrawPlayersOverlay() {
    if (!g_esp_level && !g_card_warning) return; ImDrawList* d = ImGui::GetWindowDrawList();
    static float tx=g_players_X, ty=g_players_Y, ts=g_players_Scale;
    float r = 20.f*g_autoScale, rowH = r*2.8f;
    InteractionEngine(g_players_X, g_players_Y, g_players_Scale, g_players_Scale, tx, ty, ts, ts, false, g_boardLocked, 200.f, 8*rowH);
    for(int i=0; i<8; i++) {
        ImVec2 p(g_players_X, g_players_Y + i*rowH*g_players_Scale);
        d->AddCircleFilled(p+ImVec2(r,r)*g_players_Scale, r*g_players_Scale, IM_COL32(30,35,45,200));
        if(g_esp_level) d->AddText(g_hugeNumFont, 16.f*g_players_Scale*g_autoScale, p+ImVec2(r*2.5f, r*0.5f)*g_players_Scale, IM_COL32(255,215,0,255), "G:28/LV5");
    }
}

void DrawPurePredictEnemy() {
    if (!g_predict_enemy) return; ImDrawList* d = ImGui::GetWindowDrawList();
    static float tx=g_enemy_X, ty=g_enemy_Y, ts=g_enemy_Scale;
    InteractionEngine(g_enemy_X, g_enemy_Y, g_enemy_Scale, g_enemy_Scale, tx, ty, ts, ts, false, g_boardLocked, 120.f, 40.f);
    d->AddRectFilled(ImVec2(g_enemy_X, g_enemy_Y), ImVec2(g_enemy_X+120*g_enemy_Scale, g_enemy_Y+40*g_enemy_Scale), IM_COL32(10,15,20,180), 20.f*g_enemy_Scale);
    d->AddText(g_mainFont, 20.f*g_enemy_Scale*g_autoScale, ImVec2(g_enemy_X+15, g_enemy_Y+10), IM_COL32(255,80,80,255), u8"玩家 3");
}

void DrawAutoBuyWindow() {
    if (!g_auto_buy) return; 
    static float tx=g_autoW_X, ty=g_autoW_Y, tsx=g_autoW_Scale, tsy=g_autoW_Scale;
    float bW = 300.f*g_autoScale, bH = 65.f*g_autoScale;
    InteractionEngine(g_autoW_X, g_autoW_Y, g_autoW_Scale, g_autoW_Scale, tx, ty, tsx, tsy, false, g_boardLocked, bW, bH);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(g_autoW_X, g_autoW_Y), ImVec2(g_autoW_X+bW*g_autoW_Scale, g_autoW_Y+bH*g_autoW_Scale), IM_COL32(15,20,25,230), bH*0.5f*g_autoW_Scale);
    AnimatedNeonButton(u8"自动刷新", ImVec2(g_autoW_X+15*g_autoW_Scale, g_autoW_Y+10*g_autoW_Scale), ImVec2(130*g_autoW_Scale, 45*g_autoW_Scale), g_autoW_Scale, &g_auto_refresh);
    AnimatedNeonButton(u8"自动拿牌", ImVec2(g_autoW_X+155*g_autoW_Scale, g_autoW_Y+10*g_autoW_Scale), ImVec2(130*g_autoW_Scale, 45*g_autoW_Scale), g_autoW_Scale, &g_auto_buy_chosen);
}

void DrawCardPool() {
    if (!g_show_card_pool) return;
    static float tx=g_cardPoolX, ty=g_cardPoolY, tsX=g_cardPoolScaleX, tsY=g_cardPoolScaleY;
    float bI = 45.f*g_autoScale, gP = 5.f*g_autoScale, tW = g_card_pool_cols*(bI+gP), tH = g_card_pool_rows*(bI+gP);
    InteractionEngine(g_cardPoolX, g_cardPoolY, g_cardPoolScaleX, g_cardPoolScaleY, tx, ty, tsX, tsY, true, g_boardLocked, tW, tH);
    for(int r=0; r<g_card_pool_rows; r++) for(int c=0; c<g_card_pool_cols; c++) {
        ImVec2 p(g_cardPoolX+c*(bI+gP)*g_cardPoolScaleX, g_cardPoolY+r*(bI+gP)*g_cardPoolScaleY);
        ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)(intptr_t)g_heroTexture, p, p+ImVec2(bI*g_cardPoolScaleX, bI*g_cardPoolScaleY), ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,255*g_cardPoolAlpha), 6.f);
    }
}

// =================================================================
// 5. 定制 UI 组件 (修复回弹与 Debug 窗口)
// =================================================================
bool AnimatedNeonButton(const char* label, ImVec2 pos, ImVec2 sz, float sc, bool* v) {
    ImGuiID id = ImGui::GetID(label); ImRect bb(pos, pos + sz);
    bool hov = bb.Contains(ImGui::GetIO().MousePos), click = hov && ImGui::IsMouseClicked(0); if(click && v) *v = !(*v);
    static std::map<ImGuiID, float> anims; anims[id] = ImLerp(anims[id], (v && *v) ? 1.f : (hov ? 0.6f : 0.f), 0.2f);
    float a = anims[id]; ImU32 col = (v && *v) ? IM_COL32(0, 255, 180, 255) : IM_COL32(40, 50, 60, 200);
    ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, col, sz.y*0.5f);
    ImVec2 tSz = g_mainFont->CalcTextSizeA(ImGui::GetFontSize()*sc, FLT_MAX, 0.f, label);
    ImGui::GetWindowDrawList()->AddText(g_mainFont, ImGui::GetFontSize()*sc, pos + (sz - tSz)*0.5f, IM_COL32_WHITE, label);
    return click;
}

bool ModernToggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); ImGuiID id = window->GetID(label);
    ImVec2 lSz = ImGui::CalcTextSize(label, NULL, true); float h = ImGui::GetFrameHeight()*0.8f, w = h*2.f;
    ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + 10, h)); ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov, held; if (ImGui::ButtonBehavior(bb, id, &hov, &held)) *v = !(*v);
    static std::map<ImGuiID, float> anims; anims[id] = ImLerp(anims[id], *v?1.f:0.f, 0.2f);
    window->DrawList->AddRectFilled(bb.Min, bb.Min+ImVec2(w,h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f,0.22f,0.27f,1.f), ImVec4(0.f,0.85f,0.55f,1.f), anims[id])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min+ImVec2(h*0.5f + anims[id]*(w-h), h*0.5f), h*0.5f-2.f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Max.x + 5, bb.Min.y), label); return true;
}

bool ModernAnimatedFolder(const char* label, bool* state, int count) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos, sz(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()*1.2f);
    ImRect bb(pos, pos+sz); ImGui::ItemSize(bb); bool is_clipped = !ImGui::ItemAdd(bb, id);
    bool hov, held; if(!is_clipped && ImGui::ButtonBehavior(bb, id, &hov, &held)) *state = !(*state);
    static std::map<ImGuiID, float> anims; anims[id] = ImLerp(anims[id], *state?1.0f:0.0f, 0.15f);
    if(!is_clipped) window->DrawList->AddRectFilled(bb.Min, bb.Max, hov?IM_COL32(50,60,75,200):IM_COL32(40,48,60,150), 8.f*g_autoScale);
    if (anims[id] > 0.01f) {
        ImGui::BeginChild(id+1, ImVec2(0, (ImGui::GetFrameHeight()+16*g_autoScale)*count + 10), false, ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar);
        return true;
    }
    return false;
}

template <typename T>
void ModernAdjuster(const char* label, T* v, T min, T max, T step, const char* fmt) {
    ImGui::PushID(label); ImGui::Text("%s", label); ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120*g_autoScale);
    if(ImGui::Button("-", ImVec2(30*g_autoScale, 30*g_autoScale)) && *v > min) *v -= step; ImGui::SameLine();
    ImGui::Text(fmt, *v); ImGui::SameLine();
    if(ImGui::Button("+", ImVec2(30*g_autoScale, 30*g_autoScale)) && *v < max) *v += step; ImGui::PopID();
}

// =================================================================
// 6. 主循环与“全屏主图层”架构
// =================================================================
int main() {
    ImGui::CreateContext(); android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    eglSwapInterval(eglGetCurrentDisplay(), 1); LoadConfig(); UpdateFontHD(true);
    static bool run = true; std::thread it([&]{ while(run){ imgui.ProcessInputEvent(); std::this_thread::yield(); } });

    while (run) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        imgui.BeginFrame(); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
        if (!g_resLoaded) { g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); g_textureLoaded = (g_heroTexture!=0); g_resLoaded = true; }

        // 【核心修复】：建立隐形主图层，解决 Debug 窗口弹出问题
        ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("##MasterOverlay", nullptr, ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoBackground);
        DrawBoard(); DrawBench(); DrawPlayersOverlay(); DrawPurePredictEnemy(); DrawAutoBuyWindow(); DrawCardPool();
        ImGui::End();

        // 菜单
        if (ImGui::Begin(u8"金铲铲全能助手 v3.1", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetWindowFontScale(g_scale);
            if(ModernAnimatedFolder(u8"预测与透视", &g_predict_enemy, 3)) {
                ModernToggle(u8"棋盘透视", &g_esp_board); ModernToggle(u8"备战席透视", &g_esp_bench); ModernToggle(u8"玩家覆盖层", &g_esp_level);
                ImGui::EndChild();
            }
            ModernToggle(u8"自动拿牌", &g_auto_buy); ModernToggle(u8"牌库显示", &g_show_card_pool);
            if(g_show_card_pool) ModernAdjuster<float>(u8"牌库透明度", &g_cardPoolAlpha, 0.1f, 1.f, 0.1f, "%.1f");
            if (ImGui::Button(u8"保存配置", ImVec2(-1, 50*g_autoScale))) SaveConfig();
        }
        ImGui::End();
        imgui.EndFrame();
    }
    run = false; it.join(); return 0;
}
