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
bool g_auto_buy = false;
bool g_instant = false;
bool g_boardLocked = false; 

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
float g_menuX = 100.0f, g_menuY = 100.0f;
float g_menuW = 350.0f, g_menuH = 550.0f; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

// 模拟的敌方棋盘数据
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
        out << "menuW=" << g_menuW << "\n";
        out << "menuScale=" << g_scale << "\n";
        out << "boardLocked=" << g_boardLocked << "\n";
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
                else if (k == "startX") g_startX = std::stof(v);
                else if (k == "startY") g_startY = std::stof(v);
                else if (k == "manualScale") g_boardManualScale = std::stof(v);
                else if (k == "menuX") g_menuX = std::stof(v);
                else if (k == "menuY") g_menuY = std::stof(v);
                else if (k == "menuW") g_menuW = std::stof(v);
                else if (k == "menuScale") g_scale = std::stof(v);
                else if (k == "boardLocked") g_boardLocked = (v == "1");
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

// =================================================================
// 3. Hex Shader 与 纹理加载
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

// =================================================================
// 4. 字体与显示适配
// =================================================================
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
// 5. 棋盘绘制逻辑 (保留流光配色 + 终极物理阻尼引擎)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    // 引入物理阻尼目标值，保证 0 跳变和极限丝滑
    static float t_startX = g_startX;
    static float t_startY = g_startY;
    static float t_scale = g_boardManualScale;
    
    static bool firstFrame = true;
    if (firstFrame) {
        t_startX = g_startX; t_startY = g_startY; t_scale = g_boardManualScale;
        firstFrame = false;
    }

    float baseSz = 38.0f * g_boardScale * g_autoScale;
    float baseXStep = baseSz * 1.73205f;
    float baseYStep = baseSz * 1.5f;

    // 手柄基于原点的物理常量偏移
    float h_dx = 6 * baseXStep + (3 % 2 == 1 ? baseXStep * 0.5f : 0) + baseSz;
    float h_dy = 3 * baseYStep + baseSz * 0.5f;

    static bool isDraggingBoard = false, isScalingBoard = false;
    static ImVec2 dragOffset;        
    static ImVec2 scaleDragOffset;   

    if (!g_boardLocked) {
        float currentHandleX = g_startX + h_dx * g_boardManualScale;
        float currentHandleY = g_startY + h_dy * g_boardManualScale;
        ImVec2 p_handle(currentHandleX, currentHandleY);

        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            float distSq = ImLengthSqr(io.MousePos - p_handle);
            if (distSq < (4900.0f * g_autoScale * g_autoScale)) { 
                isScalingBoard = true;
                // 使用双锚点逻辑，无论点在手柄哪里，都不会产生跳变居中
                ImVec2 targetHandleCenter(t_startX + h_dx * t_scale, t_startY + h_dy * t_scale);
                scaleDragOffset = io.MousePos - targetHandleCenter;
            } else {
                float currentSz = baseSz * g_boardManualScale;
                ImRect boardArea(
                    ImVec2(g_startX - currentSz*2, g_startY - currentSz*2), 
                    ImVec2(g_startX + 6.5f * baseXStep * g_boardManualScale + currentSz*2, 
                           g_startY + 3.0f * baseYStep * g_boardManualScale + currentSz*2)
                );
                if (boardArea.Contains(io.MousePos)) {
                    isDraggingBoard = true; 
                    dragOffset = ImVec2(t_startX - io.MousePos.x, t_startY - io.MousePos.y);
                }
            }
        }
        
        if (isScalingBoard) {
            if (ImGui::IsMouseDown(0)) {
                ImVec2 targetHandleCenter = io.MousePos - scaleDragOffset;
                float targetDist = sqrtf(powf(targetHandleCenter.x - t_startX, 2) + powf(targetHandleCenter.y - t_startY, 2));
                float baseHandleDist = sqrtf(h_dx * h_dx + h_dy * h_dy);
                
                t_scale = targetDist / baseHandleDist;
                t_scale = std::clamp(t_scale, 0.2f, 5.0f);
            } else { 
                isScalingBoard = false; SaveConfig(); 
            }
        }
        
        if (isDraggingBoard && !isScalingBoard) {
            if (ImGui::IsMouseDown(0)) {
                t_startX = io.MousePos.x + dragOffset.x;
                t_startY = io.MousePos.y + dragOffset.y;
            } else { 
                isDraggingBoard = false; SaveConfig(); 
            }
        }
    }

    // 指数衰减物理阻尼，消除安卓触摸屏的硬件抖动
    float smoothness = 1.0f - expf(-20.0f * io.DeltaTime);
    g_startX = ImLerp(g_startX, t_startX, smoothness);
    g_startY = ImLerp(g_startY, t_startY, smoothness);
    g_boardManualScale = ImLerp(g_boardManualScale, t_scale, smoothness);

    // 计算渲染尺寸
    float curSz = baseSz * g_boardManualScale;
    float curXStep = baseXStep * g_boardManualScale;
    float curYStep = baseYStep * g_boardManualScale;
    float time = (float)ImGui::GetTime();

    // 绘制黄金交互手柄
    if (!g_boardLocked) {
        float handleX = g_startX + h_dx * g_boardManualScale;
        float handleY = g_startY + h_dy * g_boardManualScale;
        ImVec2 p_handle(handleX, handleY);
        
        d->AddCircleFilled(p_handle, 16.0f * g_autoScale, IM_COL32(255, 215, 0, 240));
        d->AddCircleFilled(p_handle, 6.0f * g_autoScale, IM_COL32(255, 255, 255, 255));
        d->AddCircle(p_handle, (20.0f + 4.0f * sinf(time*6)) * g_autoScale, IM_COL32(255, 215, 0, 150), 32, 2.5f * g_autoScale);
    }

    // 绘制棋盘格 (保留流光变色风格)
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
            // 内部填充与边缘
            d->AddConvexPolyFilled(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 30));
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 220), ImDrawFlags_Closed, 2.5f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 菜单 UI
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
    if (pressed) { *v = !(*v); SaveConfig(); }

    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f; 
    
    // 质感配色：深灰底色 vs 极客薄荷绿
    ImVec4 col_bg_off = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    ImVec4 col_bg_on  = ImVec4(0.00f, 0.85f, 0.55f, 1.0f); 
    ImVec4 col_bg = ImLerp(col_bg_off, col_bg_on, g_anim[idx]);
    
    // 绘制轨道底色及内阴影边框
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(col_bg), h*0.5f);
    window->DrawList->AddRect(bb.Min, bb.Min + ImVec2(w, h), IM_COL32(0, 0, 0, 80), h*0.5f, 0, 1.0f);
    
    // 绘制带立体感阴影的圆钮
    float handle_radius = h * 0.5f - 2.5f;
    ImVec2 handle_center = bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f);
    window->DrawList->AddCircleFilled(handle_center + ImVec2(0, 1.5f), handle_radius, IM_COL32(0, 0, 0, 90)); // 投影
    window->DrawList->AddCircleFilled(handle_center, handle_radius, IM_COL32_WHITE);
    
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y*0.5f), label);
    return pressed;
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); 
    ImGuiStyle& style = ImGui::GetStyle();
    
    // 全局 UI 高级质感配置 (保持你喜欢的圆角与间距)
    style.WindowRounding = 16.0f * g_autoScale;
    style.FrameRounding = 8.0f * g_autoScale;
    style.PopupRounding = 8.0f * g_autoScale;
    style.GrabRounding = 8.0f * g_autoScale;
    style.ItemSpacing = ImVec2(12 * g_autoScale, 16 * g_autoScale);
    style.WindowPadding = ImVec2(16 * g_autoScale, 16 * g_autoScale);
    style.WindowBorderSize = 1.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.20f, 0.25f, 0.8f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.27f, 0.32f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.35f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.17f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.25f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.32f, 0.38f, 1.0f);

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    // 剔除 Emoji 以解决问号乱码问题
    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v2.4", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        
        g_menuX = ImGui::GetWindowPos().x;
        g_menuY = ImGui::GetWindowPos().y;
        float curW = ImGui::GetWindowSize().x;
        g_menuCollapsed = ImGui::IsWindowCollapsed();

        if (ImGui::IsMouseReleased(0) && std::abs(curW - g_menuW) > 5.0f) {
            g_menuW = curW; g_scale = curW / (350.0f * g_autoScale);
            g_needUpdateFontSafe = true; SaveConfig();
        }

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"● VSYNC 模式已开启 | FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8" 智能预测 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手预测", &g_predict_enemy, 1); 
                ModernToggle((const char*)u8"海克斯辅助", &g_predict_hex, 2); 
                ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8" 透视显示 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); 
                ModernToggle((const char*)u8"备战席透视", &g_esp_bench, 4); 
                ModernToggle((const char*)u8"商店概率透视", &g_esp_shop, 5); 
                ImGui::Unindent();
            }
            ImGui::Separator();
            ModernToggle((const char*)u8"锁定棋盘位置", &g_boardLocked, 8); 
            ModernToggle((const char*)u8"智能拿牌", &g_auto_buy, 6); 
            ModernToggle((const char*)u8"极速退房", &g_instant, 7);
            
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 55 * g_autoScale))) SaveConfig();
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 主循环 (帧率同步模式)
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    // 设置为 1 开启 VSYNC 垂直同步，渲染帧率将与屏幕刷新率完全一致
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
            // 测试头像路径
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); 
            g_resLoaded = true; 
        }
        
        DrawBoard(); 
        DrawMenu();
        
        imgui.EndFrame(); 
        // 渲染流程已通过 EGL 间隔同步，无需手动延时
        std::this_thread::yield();
    }
    
    g_HexShader.Cleanup();
    running = false; 
    if (it.joinable()) it.join(); 
    return 0;
}
