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
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

// 功能开关
bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

// UI 状态
bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; // 动画数组

// 缩放与位置
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

// 资源句柄
GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

// 模拟数据：敌人棋盘布局
int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置持久化
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
        out << "menuH=" << g_menuH << "\n";
        out << "menuScale=" << g_scale << "\n";
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
                else if (k == "autoBuy") g_auto_buy = (v == "1");
                else if (k == "instant") g_instant = (v == "1");
                else if (k == "startX") g_startX = std::stof(v);
                else if (k == "startY") g_startY = std::stof(v);
                else if (k == "manualScale") g_boardManualScale = std::stof(v);
                else if (k == "menuX") g_menuX = std::stof(v);
                else if (k == "menuY") g_menuY = std::stof(v);
                else if (k == "menuW") g_menuW = std::stof(v);
                else if (k == "menuH") g_menuH = std::stof(v);
                else if (k == "menuScale") g_scale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        g_needUpdateFontSafe = true; 
    }
}

// =================================================================
// 3. 自定义 Hex 裁剪着色器
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
                         "    vec2 rotated_p = vec2(p.y, p.x);\n"
                         "    float d = sdHex(rotated_p, 0.92);\n"
                         "    float w = fwidth(d);\n"
                         "    float m = 1.0 - smoothstep(-w, w, d);\n"
                         "    vec4 tex = texture(Texture, Frag_UV);\n"
                         "    if(m <= 0.0) discard;\n"
                         "    Out_Color = tex * m;\n"
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
    
    // 使用回调注入自定义着色器逻辑
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, (void*)(intptr_t)g_heroTexture);
    
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    
    // 必须重置状态，否则会影响后续 ImGui 渲染
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

// =================================================================
// 4. 动态高清字体适配
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    
    float baseSize = 18.0f * g_autoScale * g_scale;
    float targetSize = std::clamp(baseSize, 12.0f, 120.0f);

    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    
    ImFontConfig config;
    config.OversampleH = 2; 
    config.OversampleV = 2;
    config.PixelSnapH = true;

    // 多路径字体检测
    const char* fontPaths[] = {
        "/system/fonts/SysSans-Hans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf"
    };
    
    bool fontLoaded = false;
    for (const char* path : fontPaths) {
        if (access(path, R_OK) == 0) {
            io.Fonts->AddFontFromFileTTF(path, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            fontLoaded = true;
            break;
        }
    }
    
    if (!fontLoaded) io.Fonts->AddFontDefault();

    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 棋盘绘制与交互
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f; 
    float yStep = sz * 1.5f;
    
    // 渲染缩放/拖拽手柄 (右下角金色小箭头)
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(-30.0f * M_PI / 180.0f), lastCY + sz * sinf(-30.0f * M_PI / 180.0f));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(30.0f * M_PI / 180.0f), lastCY + sz * sinf(30.0f * M_PI / 180.0f));
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + sz * 0.6f, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 200));
    
    static bool isDraggingBoard = false, isScalingBoard = false;
    static ImVec2 dragOffset;

    if (ImGui::IsMouseClicked(0)) {
        ImRect hRect(p_top, p_ext); hRect.Expand(40.0f);
        if (hRect.Contains(io.MousePos)) {
            isScalingBoard = true;
        } else {
            ImRect boardArea(ImVec2(g_startX - sz, g_startY - sz), ImVec2(lastCX + sz, lastCY + sz));
            if (boardArea.Contains(io.MousePos)) {
                isDraggingBoard = true; 
                dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
            }
        }
    }

    if (isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - g_startX;
            float baseW = (6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale;
            g_boardManualScale = std::max(curW / baseW, 0.2f); 
        } else {
            isScalingBoard = false; SaveConfig(); 
        }
    }
    
    if (isDraggingBoard && !isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            g_startX = io.MousePos.x - dragOffset.x; 
            g_startY = io.MousePos.y - dragOffset.y;
        } else {
            isDraggingBoard = false; SaveConfig(); 
        }
    }

    // 绘制 4x7 六边形网格
    float time = (float)ImGui::GetTime();
    for(int r = 0; r < 4; r++) {
        for(int c = 0; c < 7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            
            // 绘制英雄头像
            if(g_enemyBoard[r][c] && g_textureLoaded) {
                DrawHero(d, ImVec2(cx, cy), sz * 0.95f); 
            }
            
            // 绘制彩虹边框
            float hue = fmodf(time * 0.4f + (cx + cy) * 0.0005f, 1.0f);
            float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 1.0f, rf, gf, bf);
            
            ImVec2 pts[6];
            for(int i = 0; i < 6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 230), ImDrawFlags_Closed, 3.5f * g_autoScale);
        }
    }
}

// =================================================================
// 6. UI 组件与菜单
// =================================================================
bool CustomToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    
    float height = ImGui::GetFrameHeight() * 0.8f;
    float width = height * 2.0f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width + style.ItemInnerSpacing.x + label_size.x, height + style.FramePadding.y * 2.0f));
    
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { 
        *v = !(*v); 
        SaveConfig(); 
    }

    // 动画平滑处理
    float target = *v ? 1.0f : 0.0f;
    g_anim[idx] += (target - g_anim[idx]) * 0.15f;

    ImVec4 col_bg = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0.15f, 0.65f, 0.35f, 1.0f), g_anim[idx]);
    
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(width, height), ImGui::GetColorU32(col_bg), height * 0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(height * 0.5f + g_anim[idx] * (width - height), height * 0.5f), height * 0.5f - 2.0f, IM_COL32_WHITE);
    
    ImGui::RenderText(ImVec2(bb.Min.x + width + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    return pressed;
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO(); 
    
    // 菜单视觉风格设置
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 15.0f * g_autoScale;
    style.ChildRounding = 8.0f * g_autoScale;
    style.FrameRounding = 6.0f * g_autoScale;
    style.ItemSpacing = ImVec2(8 * g_autoScale, 12 * g_autoScale);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 0.94f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.25f, 0.5f);

    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin((const char*)u8"金铲铲全能助手 v1.2", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        
        g_menuX = ImGui::GetWindowPos().x;
        g_menuY = ImGui::GetWindowPos().y;
        float curW = ImGui::GetWindowSize().x;
        float curH = ImGui::GetWindowSize().y;
        g_menuCollapsed = ImGui::IsWindowCollapsed();

        // 自动缩放适配
        float visualScale = curW / (350.0f * g_autoScale);
        if (ImGui::IsMouseReleased(0) && (std::abs(curW - g_menuW) > 5.0f)) {
            g_menuW = curW; g_menuH = curH;
            g_scale = visualScale;
            g_needUpdateFontSafe = true; 
            SaveConfig();
        }

        if (!g_menuCollapsed) {
            // 根据当前窗口大小调整字体显示比例
            ImGui::SetWindowFontScale( (18.0f * g_autoScale * visualScale) / g_current_rendered_size );
            
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1), u8"系统负载稳定 | FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader(u8" 智能预测模块 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                CustomToggle(u8"下一回合对手预测", &g_predict_enemy, 1); 
                CustomToggle(u8"海克斯强化概率", &g_predict_hex, 2); 
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader(u8" 视觉增强模块 ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(); 
                CustomToggle(u8"对手棋盘实时透视", &g_esp_board, 3); 
                CustomToggle(u8"备战席动态监控", &g_esp_bench, 4); 
                CustomToggle(u8"商店刷新概率透视", &g_esp_shop, 5); 
                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::Spacing();
            CustomToggle(u8"自动优选拿牌", &g_auto_buy, 6); 
            CustomToggle(u8"战败极速退出", &g_instant, 7);
            
            ImGui::Spacer(); 
            if (ImGui::Button(u8"立即保存当前配置", ImVec2(-1, 50 * g_autoScale))) {
                SaveConfig();
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 主程序循环
// =================================================================
int main() {
    ImGui::CreateContext();
    
    // 初始化 AImGui (假设此库处理 Android Native Surface)
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    
    LoadConfig(); 
    UpdateFontHD(true);  

    static bool running = true; 
    // 输入处理线程
    std::thread inputThread([&] { 
        while(running) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
        } 
    });

    while (running) {
        // 安全地在主线程更新字体纹理
        if (g_needUpdateFontSafe) { 
            UpdateFontHD(true); 
            g_needUpdateFontSafe = false; 
        }

        imgui.BeginFrame(); 
        
        // 资源按需加载
        if (!g_resLoaded) { 
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); 
            g_resLoaded = true; 
        }

        DrawBoard(); 
        DrawMenu();

        imgui.EndFrame(); 
        std::this_thread::yield();
    }

    running = false; 
    if (inputThread.joinable()) inputThread.join(); 
    
    // 资源清理
    if (g_heroTexture) glDeleteTextures(1, &g_heroTexture);
    if (g_HexShaderInited) glDeleteProgram(g_HexShader.program);
    
    return 0;
}
