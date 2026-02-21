#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

#include <thread>
#include <cstdio>

// ========== 数据 ==========
int gold = 100, level = 8, hp = 85;
bool g_predict = false, g_esp = false, g_instant = false;
float g_scale = 1.0f, g_boardScale = 1.0f;
ImVec2 g_winPos(50,100), g_winSize(280,400);
bool g_posInit = false;

// ========== 字体 ==========
void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* paths[] = {"/system/fonts/SysSans-Hans-Regular.ttf"};
    ImFontConfig cfg; cfg.OversampleH = cfg.OversampleV = 2;
    for (auto p : paths) if (io.Fonts->AddFontFromFileTTF(p, 16, &cfg, io.Fonts->GetGlyphRangesChineseFull())) break;
    io.Fonts->Build();
}

// ========== 开关 ==========
bool Toggle(const char* label, bool* v) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return 0;
    ImGuiContext& g = *GImGui; const ImGuiStyle& s = g.Style;
    float h = ImGui::GetFrameHeight(), wd = h*1.8f, r = h*0.45f;
    ImVec2 pos = w->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x+wd + ImGui::CalcTextSize(label).x, pos.y+h));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, 0)) return 0;
    
    w->DrawList->AddRectFilled(pos, ImVec2(pos.x+wd,pos.y+h), *v ? 0xFF33CC33 : 0xFF888888, h*0.5f);
    float shift = (*v ? 1.f : 0.f) * (wd - 2*r);
    w->DrawList->AddCircleFilled(ImVec2(pos.x+r+shift,pos.y+h/2), r-2, 0xFFFFFFFF);
    ImGui::RenderText(ImVec2(pos.x+wd + 8, pos.y), label);
    
    if (ImGui::ButtonBehavior(bb, ImGui::GetID(label), 0, 0, ImGuiButtonFlags_PressedOnClick))
        *v = !*v;
    return 1;
}

// ========== 棋盘 ==========
void DrawBoard() {
    if (!g_esp) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    float sz = 40*g_boardScale, w = 7*sz, h = 4*sz;
    static float x=200,y=200; static bool drag=0;
    
    if (ImGui::IsMouseDown(0)) {
        ImVec2 m = ImGui::GetMousePos();
        if (!drag && m.x>=x && m.x<=x+w && m.y>=y && m.y<=y+h) drag=1;
        if (drag) { x = m.x-w/2; y = m.y-h/2; }
    } else drag=0;
    
    d->AddRectFilled(ImVec2(x,y), ImVec2(x+w,y+h), 0x1E1E1E64, 4);
    for (int i=0; i<=4; i++) d->AddLine(ImVec2(x,y+i*sz), ImVec2(x+w,y+i*sz), 0x646464FF);
    for (int i=0; i<=7; i++) d->AddLine(ImVec2(x+i*sz,y), ImVec2(x+i*sz,y+h), 0x646464FF);
    
    for (int r=0; r<4; r++) for (int c=0; c<7; c++) {
        float cx = x + c*sz + sz/2, cy = y + r*sz + sz/2;
        d->AddCircleFilled(ImVec2(cx,cy), sz*0.3, (r+c)%2 ? 0x6464FFFF : 0xFF6464FF, 32);
        d->AddCircle(ImVec2(cx,cy), sz*0.3, 0xFFFFFF96, 32, 1);
    }
}

// ========== 缩放 ==========
void Scale(ImGuiSizeCallbackData* d) { 
    g_scale = d->DesiredSize.x / 280; 
    ImGui::GetIO().FontGlobalScale = g_scale;
}

// ========== 主函数 ==========
int main() {
    printf("[1] Starting...\n");
    
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    LoadFont();
    bool running = 1;
    
    std::thread input([&] { while(running) { imgui.ProcessInputEvent(); std::this_thread::yield(); } });
    
    auto lastSave = std::chrono::high_resolution_clock::now();
    while (running) {
        auto start = std::chrono::high_resolution_clock::now();
        
        imgui.BeginFrame();
        DrawBoard();
        
        {
            ImGui::SetNextWindowSizeConstraints({150,200}, {FLT_MAX,FLT_MAX}, Scale);
            if (g_posInit) ImGui::SetNextWindowPos(g_winPos, ImGuiCond_FirstUseEver);
            
            ImGui::Begin("金铲铲助手", &running);
            g_winPos = ImGui::GetWindowPos(); g_winSize = ImGui::GetWindowSize(); g_posInit=1;
            
            ImGui::Text("缩放: %.1fx", g_scale);
            Toggle("预测", &g_predict);
            Toggle("透视", &g_esp);
            Toggle("秒退", &g_instant);
            ImGui::Text("金币:%d 等级:%d 血量:%d", gold, level, hp);
            if (g_esp) ImGui::SliderFloat("棋盘", &g_boardScale, 0.3, 3);
            
            ImGui::End();
        }
        
        imgui.EndFrame();
        
        int sleep = 8333 - std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        if (sleep > 0) usleep(sleep);
        
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<float>(now - lastSave).count() > 2) {
            FILE* f = fopen("/data/local/tmp/config.txt", "w");
            if (f) { fprintf(f, "%f %d %d %d", g_scale, g_predict, g_esp, g_instant); fclose(f); }
            lastSave = now;
        }
    }
    
    input.join();
    return 0;
}
