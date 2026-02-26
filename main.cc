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
float g_menuAlpha = 0.0f;        // 菜单淡入动画控制

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
// 5. 棋盘绘制逻辑 (修复点击跳变 + 高级美化)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    float baseSz = 38.0f * g_boardScale * g_autoScale;
    float baseXStep = baseSz * 1.73205f;
    float baseYStep = baseSz * 1.5f;

    float curSz = baseSz * g_boardManualScale;
    float curXStep = baseXStep * g_boardManualScale;
    float curYStep = baseYStep * g_boardManualScale;

    // 手柄位置逻辑
    float lastCX = g_startX + 6 * curXStep + (3 % 2 == 1 ? curXStep * 0.5f : 0);
    float lastCY = g_startY + 3 * curYStep;
    ImVec2 p_handle = ImVec2(lastCX + curSz, lastCY + curSz * 0.5f);

    if (!g_boardLocked) {
        static bool isDraggingBoard = false, isScalingBoard = false;
        
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            float distSq = ImLengthSqr(io.MousePos - p_handle);
            if (distSq < (3600.0f * g_autoScale)) {
                isScalingBoard = true;
            } else {
                ImRect boardArea(ImVec2(g_startX - curSz*2, g_startY - curSz*2), ImVec2(lastCX + curSz*2, lastCY + curSz*2));
                if (boardArea.Contains(io.MousePos)) {
                    isDraggingBoard = true; 
                }
            }
        }
        
        if (isScalingBoard) {
            if (ImGui::IsMouseDown(0)) {
                g_boardManualScale += io.MouseDelta.x * 0.005f;
                g_boardManualScale = std::max(g_boardManualScale, 0.2f);
            } else { 
                isScalingBoard = false; SaveConfig(); 
            }
        }
        
        if (isDraggingBoard && !isScalingBoard) {
            if (ImGui::IsMouseDown(0)) {
                g_startX += io.MouseDelta.x;
                g_startY += io.MouseDelta.y;
            } else { 
                isDraggingBoard = false; SaveConfig(); 
            }
        }

        // 绘制交互手柄 (海克斯科技能量核心)
        d->AddCircleFilled(p_handle, 14.0f * g_autoScale, IM_COL32(20, 30, 50, 220)); // 深层底座
        d->AddCircleFilled(p_handle, 8.0f * g_autoScale, IM_COL32(0, 210, 255, 255)); // 青色能量核心
        d->AddCircle(p_handle, (14.0f + 4.0f * sinf(ImGui::GetTime()*6)) * g_autoScale, IM_COL32(0, 255, 255, 150), 24, 2.5f); // 科技光波
    }

    // 棋盘格渲染逻辑 (科技呼吸质感)
    float time = (float)ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0);
            float cy = g_startY + r * curYStep;
            
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), curSz * 0.95f); 
            
            // 动态呼吸发光计算
            float distToCenter = sqrtf((cx - g_startX)*(cx - g_startX) + (cy - g_startY)*(cy - g_startY));
            float pulse = (sinf(time * 2.5f - distToCenter * 0.003f) + 1.0f) * 0.5f;
            
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + curSz * cosf(a), cy + curSz * sinf(a));
            }

            // 绘制底色填充（增加立体感和沉浸感）
            d->AddConvexPolyFilled(pts, 6, IM_COL32(10, 15, 30, 100 + (int)(pulse * 30)));

            // 绘制高科技质感边框 (青/冰蓝呼吸渐变色调)
            int borderR = (int)(0 + pulse * 40);
            int borderG = (int)(160 + pulse * 95);
            int borderB = (int)(220 + pulse * 35);
            
            d->AddPolyline(pts, 6, IM_COL32(borderR, borderG, borderB, 230), ImDrawFlags_Closed, 3.0f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 菜单 UI (毛玻璃与海克斯主题)
// =================================================================
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // 全局圆角与间距美化
    style.WindowRounding    = 14.0f * g_autoScale;
    style.FrameRounding     = 8.0f * g_autoScale;
    style.PopupRounding     = 8.0f * g_autoScale;
    style.GrabRounding      = 8.0f * g_autoScale;
    style.ItemSpacing       = ImVec2(12 * g_autoScale, 16 * g_autoScale);
    style.WindowPadding     = ImVec2(16 * g_autoScale, 16 * g_autoScale);
    style.FramePadding      = ImVec2(8 * g_autoScale, 8 * g_autoScale);
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f); // 标题居中

    // 海克斯科技暗黑主题 (Hextech Dark Theme - Glassmorphism)
    colors[ImGuiCol_WindowBg]       = ImVec4(0.06f, 0.07f, 0.10f, 0.85f); // 稍调低透明度增强毛玻璃感
    colors[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.10f, 0.14f, 0.95f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.06f, 0.16f, 0.24f, 0.95f);
    colors[ImGuiCol_TitleBgCollapsed]= ImVec4(0.06f, 0.07f, 0.10f, 0.95f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.00f, 0.60f, 0.80f, 0.80f);
    colors[ImGuiCol_Header]         = ImVec4(0.10f, 0.30f, 0.45f, 0.80f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.15f, 0.45f, 0.65f, 0.80f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.00f, 0.55f, 0.75f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.10f, 0.40f, 0.60f, 0.80f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.15f, 0.55f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(0.90f, 0.95f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator]      = ImVec4(0.20f, 0.25f, 0.35f, 0.60f);
}

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

    // 动画插值优化，极其丝滑
    g_anim[idx] = ImLerp(g_anim[idx], (*v ? 1.0f : 0.0f), 0.15f);
    
    // 海克斯科技配色：深渊蓝 -> 科技青
    ImVec4 col_bg_off = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
    ImVec4 col_bg_on  = ImVec4(0.0f, 0.70f, 0.85f, 1.0f);
    ImVec4 col_bg = ImLerp(col_bg_off, col_bg_on, g_anim[idx]);
    
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(col_bg), h*0.5f);
    
    // 开关旋钮与光晕发光特效
    float knob_x = bb.Min.x + h*0.5f + g_anim[idx]*(w-h);
    float knob_y = bb.Min.y + h*0.5f;
    float knob_r = h*0.5f - 2.5f;
    
    if (*v && g_anim[idx] > 0.1f) { 
        // 开启时添加背光辉光，透明度随动画渐变
        int alphaGlow = (int)(g_anim[idx] * 80);
        window->DrawList->AddCircleFilled(ImVec2(knob_x, knob_y), knob_r + 3.0f * g_autoScale, IM_COL32(0, 220, 255, alphaGlow));
    }
    window->DrawList->AddCircleFilled(ImVec2(knob_x, knob_y), knob_r, IM_COL32_WHITE);
    
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y*0.25f), label);
    return pressed;
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); 
    SetupImGuiStyle(); 

    // UI 丝滑淡入动画 (Alpha Fade-in)
    g_menuAlpha = ImLerp(g_menuAlpha, 1.0f, 0.08f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_menuAlpha);

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    // 增加外边框发光霓虹效果
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * g_autoScale);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.6f, 0.8f, 0.6f)); 

    if (ImGui::Begin((const char*)u8"✦ 金铲铲全能助手 v2.5 ✦", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        
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
            
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.7f, 1.0f), (const char*)u8"● VSYNC 引擎已激活 | FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8" ⚡ 智能预测 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                ModernToggle((const char*)u8"对手预测", &g_predict_enemy, 1); 
                ModernToggle((const char*)u8"海克斯辅助", &g_predict_hex, 2); 
                ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8" 👁️ 透视显示 ", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            if (ImGui::Button((const char*)u8"💾 保存当前配置", ImVec2(-1, 55 * g_autoScale))) SaveConfig();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2); // 弹出 BorderSize 和 Alpha
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
