#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"

// ===== 添加图片加载支持 =====
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <thread>
#include <cstdio>
#include <string>
#include <cmath>
#include <map>

// ========== 数据 ==========
int gold = 100, level = 8, hp = 85;
bool g_predict = false, g_esp = false, g_instant = false;
float g_scale = 1.0f, g_boardScale = 1.0f;
ImVec2 g_winPos(50,100), g_winSize(280,400);
bool g_posInit = false;

// ========== 开关动画 ==========
float g_anim[3] = {0,0,0};

// ========== 头像纹理 ==========
GLuint g_testTexture = 0;
bool g_textureLoaded = false;

// ========== 从文件加载纹理 ==========
GLuint LoadTextureFromFile(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data) {
        printf("[-] Failed to load image: %s\n", filename);
        return 0;
    }
    
    printf("[+] Loaded image: %s (%dx%d)\n", filename, width, height);
    
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    return texture_id;
}

// ========== 字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    
    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        font = io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
        if (font) {
            printf("[+] Loaded font: %s\n", path);
            io.FontDefault = font;
            break;
        }
    }
    
    if (!font) {
        printf("[-] No Chinese font, using fallback\n");
        ImFontConfig fallbackConfig;
        fallbackConfig.MergeMode = true;
        io.Fonts->AddFontDefault(&fallbackConfig);
        for (const char* path : fontPaths) {
            io.Fonts->AddFontFromFileTTF(path, 16.0f, &fallbackConfig, io.Fonts->GetGlyphRangesChineseFull());
        }
    }
    
    io.Fonts->Build();
}

// ========== 带动画的开关 ==========
bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return 0;
    float h = ImGui::GetFrameHeight(), wd = h*1.8f, r = h*0.45f;
    ImVec2 pos = w->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x+wd + ImGui::CalcTextSize(label).x, pos.y+h));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, 0)) return 0;
    
    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.25f;
    if (fabs(g_anim[idx] - target) < 0.01f) g_anim[idx] = target;
    
    ImU32 bgColor = ImGui::GetColorU32(ImVec4(
        0.2f + g_anim[idx]*0.6f,
        0.2f + g_anim[idx]*0.6f,
        0.2f,
        0.9f
    ));
    
    w->DrawList->AddRectFilled(pos, ImVec2(pos.x+wd, pos.y+h), bgColor, h*0.5f);
    float shift = g_anim[idx] * (wd - 2*r);
    w->DrawList->AddCircleFilled(ImVec2(pos.x+r+shift, pos.y+h/2), r-2, 0xFFFFFFFF);
    ImGui::RenderText(ImVec2(pos.x+wd + 8, pos.y), label);
    
    if (ImGui::ButtonBehavior(bb, ImGui::GetID(label), 0, 0, ImGuiButtonFlags_PressedOnClick))
        *v = !*v;
    return 1;
}

// ========== 棋盘（带头像） ==========
void DrawBoard() {
    if (!g_esp) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    float sz = 40 * g_boardScale;
    float w = 7*sz, h = 4*sz;
    static float x=200,y=200; static bool drag=0;
    
    if (ImGui::IsMouseDown(0)) {
        ImVec2 m = ImGui::GetMousePos();
        if (!drag && m.x>=x && m.x<=x+w && m.y>=y && m.y<=y+h) drag=1;
        if (drag) { x = m.x-w/2; y = m.y-h/2; }
    } else drag=0;
    
    d->AddRectFilled(ImVec2(x,y), ImVec2(x+w,y+h), 0x1E1E1E64, 4);
    for (int i=0; i<=4; i++) d->AddLine(ImVec2(x,y+i*sz), ImVec2(x+w,y+i*sz), 0x646464FF);
    for (int i=0; i<=7; i++) d->AddLine(ImVec2(x+i*sz,y), ImVec2(x+i*sz,y+h), 0x646464FF);
    
    // ===== 如果有头像纹理，绘制头像 =====
    if (g_testTexture) {
        ImTextureID texID = (ImTextureID)(intptr_t)g_testTexture;
        float imgSize = sz * 0.7f;  // 头像大小（比格子小一点）
        
        for (int r=0; r<4; r++) {
            for (int c=0; c<7; c++) {
                float cx = x + c*sz + sz/2;
                float cy = y + r*sz + sz/2;
                
                // 计算头像位置（居中）
                float imgX = cx - imgSize/2;
                float imgY = cy - imgSize/2;
                
                // 绘制头像
                d->AddImage(texID, 
                           ImVec2(imgX, imgY), 
                           ImVec2(imgX + imgSize, imgY + imgSize));
            }
        }
    } else {
        // 没有头像就画圆圈（保持原有逻辑）
        for (int r=0; r<4; r++) for (int c=0; c<7; c++) {
            float cx = x + c*sz + sz/2, cy = y + r*sz + sz/2;
            d->AddCircleFilled(ImVec2(cx,cy), sz*0.3, (r+c)%2 ? 0x6464FFFF : 0xFF6464FF, 32);
            d->AddCircle(ImVec2(cx,cy), sz*0.3, 0xFFFFFF96, 32, 1);
        }
    }
}

// ========== 缩放回调 ==========
void Scale(ImGuiSizeCallbackData* data) { 
    g_scale = data->DesiredSize.x / 280; 
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
        g_anim[0] = g_predict ? 1 : 0;
        g_anim[1] = g_esp ? 1 : 0;
        g_anim[2] = g_instant ? 1 : 0;
    }
}

// ========== 主函数 ==========
int main() {
    printf("[1] Starting JCC Assistant...\n");
    
    IMGUI_CHECKVERSION(); 
    ImGui::CreateContext();
    
    // 多重放大右下角三角
    ImGuiStyle& style = ImGui::GetStyle();
    style.GrabMinSize = 40.0f;
    style.FramePadding = ImVec2(8, 6);
    style.WindowPadding = ImVec2(12, 12);
    style.TouchExtraPadding = ImVec2(4, 4);
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
    
    LoadChineseFont();
    
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    if (!imgui) { printf("[-] Init failed\n"); return 0; }
    
    // ===== 加载测试头像 =====
    const char* testPath = "/data/1/heroes/aatrox.png";
    FILE* f = fopen(testPath, "r");
    if (f) {
        printf("[+] Found aatrox.png\n");
        fclose(f);
        g_testTexture = LoadTextureFromFile(testPath);
        if (g_testTexture) {
            g_textureLoaded = true;
            printf("[+] Texture loaded successfully\n");
        }
    } else {
        printf("[-] aatrox.png not found at %s\n", testPath);
        printf("    Please push image: adb push aatrox.png /data/1/heroes/\n");
    }
    
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
            
            ImVec2 curPos = ImGui::GetWindowPos();
            ImVec2 curSize = ImGui::GetWindowSize();
            if (curPos.x != g_winPos.x || curPos.y != g_winPos.y) g_winPos = curPos;
            if (curSize.x != g_winSize.x || curSize.y != g_winSize.y) g_winSize = curSize;
            g_posInit = true;
            
            // 显示纹理状态
            if (g_textureLoaded) {
                ImGui::TextColored(ImVec4(0,1,0,1), "✓ 头像已加载");
            } else {
                ImGui::TextColored(ImVec4(1,0,0,1), "✗ 头像未加载");
            }
            
            ImGui::Text("缩放: %.1fx", g_scale);
            ImGui::Separator();
            
            ImGui::Text("功能设置");
            Toggle("预测", &g_predict, 0);
            Toggle("透视", &g_esp, 1);
            Toggle("秒退", &g_instant, 2);
            
            ImGui::Separator();
            ImGui::Text("游戏数据");
            ImGui::Text("金币: %d", gold);
            ImGui::Text("等级: %d", level);
            ImGui::Text("血量: %d", hp);
            
            if (g_esp) {
                ImGui::Separator();
                ImGui::Text("棋盘设置");
                ImGui::SliderFloat("棋盘缩放", &g_boardScale, 0.1f, 10.0f, "%.1f");
            }
            
            ImGui::End();
        }
        
        imgui.EndFrame();
        
        auto end = std::chrono::high_resolution_clock::now();
        int sleep = 8333 - std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        if (sleep > 0) usleep(sleep);
        
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
