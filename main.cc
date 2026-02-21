#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

#include <thread>
#include <cstdio>
#include <string>

// ========== 数据 ==========
int gold = 100, level = 8, hp = 85;
bool g_predict = false, g_esp = false, g_instant = false;
float g_scale = 1.0f, g_boardScale = 1.0f;
ImVec2 g_winPos(50,100), g_winSize(280,400);
bool g_posInit = false;

// ========== 字体（修复问号） ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 中文字体路径列表
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",   // 一加/OPPO
        "/system/fonts/NotoSansCJK-Regular.ttc",    // Google
        "/system/fonts/DroidSansFallback.ttf",      // 备用
    };
    
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    config.MergeMode = false;  // 不合并，直接使用
    
    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        font = io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
        if (font) {
            printf("[+] Loaded font: %s\n", path);
            io.FontDefault = font;
            break;
        }
    }
    
    // 如果都没找到，用默认字体但强制包含中文范围
    if (!font) {
        printf("[-] No Chinese font, using fallback\n");
        ImFontConfig fallbackConfig;
        fallbackConfig.MergeMode = true;
        io.Fonts->AddFontDefault(&fallbackConfig);
        
        // 尝试加载备用字体合并
        for (const char* path : fontPaths) {
            io.Fonts->AddFontFromFileTTF(path, 16.0f, &fallbackConfig, io.Fonts->GetGlyphRangesChineseFull());
        }
    }
    
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

// ========== 保存配置 ==========
void SaveConfig() {
    FILE* f = fopen("/data/local/tmp/jcc_config.txt", "w");
    if (f) {
        fprintf(f, "%.2f %d %d %d %.2f\n", 
                g_scale, g_predict, g_esp, g_instant, g_boardScale);
        fclose(f);
    }
}

// ========== 加载配置 ==========
void LoadConfig() {
    FILE* f = fopen("/data/local/tmp/jcc_config.txt", "r");
    if (f) {
        fscanf(f, "%f %d %d %d %f", &g_scale, &g_predict, &g_esp, &g_instant, &g_boardScale);
        fclose(f);
        ImGui::GetIO().FontGlobalScale = g_scale;
    }
}

// ========== 主函数 ==========
int main() {
    printf("[1] Starting JCC Assistant...\n");
    
    IMGUI_CHECKVERSION(); 
    ImGui::CreateContext();
    
    // 先加载字体（关键！要在 ImGui 初始化后立即加载）
    LoadChineseFont();
    
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    if (!imgui) { printf("[-] Init failed\n"); return 0; }
    
    LoadConfig();
    bool running = 1;
    
    std::thread input([&] { 
        while(running) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::yield(); 
        } 
    });
    
    auto lastSave = std::chrono::high_resolution_clock::now();
    printf("[2] Entering main loop\n");
    
    while (running) {
        auto start = std::chrono::high_resolution_clock::now();
        
        imgui.BeginFrame();
        DrawBoard();
        
        {
            ImGui::SetNextWindowSizeConstraints({150,200}, {FLT_MAX,FLT_MAX}, Scale);
            if (g_posInit) ImGui::SetNextWindowPos(g_winPos, ImGuiCond_FirstUseEver);
            
            ImGui::Begin("金铲铲助手", &running);
            
            // 获取窗口位置
            ImVec2 curPos = ImGui::GetWindowPos();
            ImVec2 curSize = ImGui::GetWindowSize();
            if (curPos.x != g_winPos.x || curPos.y != g_winPos.y) g_winPos = curPos;
            if (curSize.x != g_winSize.x || curSize.y != g_winSize.y) g_winSize = curSize;
            g_posInit = true;
            
            ImGui::Text("缩放: %.1fx", g_scale);
            ImGui::Separator();
            
            ImGui::Text("功能设置");
            Toggle("预测", &g_predict);
            Toggle("透视", &g_esp);
            Toggle("秒退", &g_instant);
            
            ImGui::Separator();
            ImGui::Text("游戏数据");
            ImGui::Text("金币: %d", gold);
            ImGui::Text("等级: %d", level);
            ImGui::Text("血量: %d", hp);
            
            if (g_esp) {
                ImGui::Separator();
                ImGui::Text("棋盘设置");
                ImGui::SliderFloat("棋盘缩放", &g_boardScale, 0.3f, 3.0f, "%.1f");
            }
            
            ImGui::End();
        }
        
        imgui.EndFrame();
        
        // 120fps 控制
        auto end = std::chrono::high_resolution_clock::now();
        int sleep = 8333 - std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        if (sleep > 0) usleep(sleep);
        
        // 每2秒保存配置
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<float>(now - lastSave).count() > 2) {
            SaveConfig();
            lastSave = now;
        }
    }
    
    SaveConfig();
    input.join();
    printf("[3] Exited\n");
    return 0;
}
