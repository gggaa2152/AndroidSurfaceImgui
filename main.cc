#include <stdarg.h>
#include "Global.h"
#include "AImGui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#include <thread>      
#define _USE_MATH_DEFINES // For M_PI
#include <cmath>       
#include <fstream>      
#include <string>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// 定义 M_PI，以防 _USE_MATH_DEFINES 不起作用或平台不支持
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// =================================================================
// 2. 全局状态变量 (保持你原始所有变量名)
// =================================================================
// 修复建议：在 Android 上，/data 目录通常需要 root 权限才能写入。
// 建议将配置文件存储在应用程序的私有数据目录中。
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f;    
float g_startY = 400.0f;    

float g_menuX = 100.0f;
float g_menuY = 100.0f;
float g_menuW = 320.0f; 
float g_menuH = 500.0f; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 

// 标记：仅用于解决 BeginFrame 锁死问题，不改动业务逻辑
bool g_needUpdateFontSafe = false;

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 3. 配置管理 (完整保留你的stof解析逻辑)
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
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Failed to open config file for writing: %s", g_configPath);
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find("=");
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
            } catch (const std::exception& e) {
                __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Failed to parse config value for key '%s' with value '%s': %s", k.c_str(), v.c_str(), e.what());
            } catch (...) {
                __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Unknown error parsing config value for key '%s' with value '%s'", k.c_str(), v.c_str());
            }
        }
        in.close();
        g_needUpdateFontSafe = true; 
    } else {
        __android_log_print(ANDROID_LOG_INFO, "JKChess", "Config file not found or failed to open for reading: %s", g_configPath);
    }
}

// =================================================================
// 4. 渲染辅助 (完全保留你的 HexShader 算法)
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
        
        GLint success;
        GLchar infoLog[512];
        glGetShaderiv(v, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(v, 512, NULL, infoLog);
            __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Vertex shader compilation failed: %s", infoLog);
        }

        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glGetShaderiv(f, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(f, 512, NULL, infoLog);
            __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Fragment shader compilation failed: %s", infoLog);
        }

        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 512, NULL, infoLog);
            __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Shader program linking failed: %s", infoLog);
        }

        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexShader;
bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) {
        __android_log_print(ANDROID_LOG_ERROR, "JKChess", "Failed to load image: %s", filename);
        return 0;
    }
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!g_textureLoaded) return;
    if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, (void*)(intptr_t)g_heroTexture);
    drawList->AddImage((ImTextureID)(intptr_t)g_heroTexture, center - ImVec2(size, size), center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    float baseSize = 18.0f * g_autoScale * g_scale;
    float targetSize = (baseSize > 120.0f) ? 120.0f : baseSize; 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    ImFontConfig config;
    config.OversampleH = 1; config.PixelSnapH = true;
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        __android_log_print(ANDROID_LOG_WARN, "JKChess", "Font file not found or not readable: %s. Using default font.", fontPath);
        io.Fonts->AddFontDefault(&config);
    }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

// =================================================================
// 5. 棋盘绘制 (保留原始位移与缩放逻辑)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float HEX_SIZE_BASE = 38.0f;
    const float HEX_X_MULTIPLIER = 1.73205f; 
    const float HEX_Y_MULTIPLIER = 1.5f;

    float sz = HEX_SIZE_BASE * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * HEX_X_MULTIPLIER; 
    float yStep = sz * HEX_Y_MULTIPLIER;
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    float hOffset = sz * 0.6f; 
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + hOffset, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 240));
    
    static bool isDraggingBoard = false, isScalingBoard = false;
    static ImVec2 dragOffset;

    if (ImGui::IsMouseClicked(0)) {
        ImRect hRect(p_top, p_ext); hRect.Expand(40.0f);
        if (hRect.Contains(io.MousePos)) isScalingBoard = true;
        else if (ImRect(ImVec2(g_startX-sz, g_startY-sz), ImVec2(lastCX+sz, lastCY+sz)).Contains(io.MousePos)) {
            isDraggingBoard = true; dragOffset = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    if (isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - g_startX;
            float baseW = (6.5f * HEX_X_MULTIPLIER + 1.0f) * HEX_SIZE_BASE * g_boardScale * g_autoScale;
            g_boardManualScale = std::max(curW / baseW, 0.1f); 
        } else {
            isScalingBoard = false; SaveConfig(); 
        }
    }
    if (isDraggingBoard && !isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            g_startX = io.MousePos.x - dragOffset.x; g_startY = io.MousePos.y - dragOffset.y;
        } else {
            isDraggingBoard = false; SaveConfig(); 
        }
    }

    float time = (float)ImGui::GetTime();
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz); 
            float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
            float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 255), ImDrawFlags_Closed, 4.0f * g_autoScale);
        }
    }
}

// =================================================================
// 6. 菜单 UI (完整保留原始动画与补偿逻辑)
// =================================================================
bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(); float w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    return pressed;
}

void DrawMenu() {
    static bool isScalingMenu = false; static float startMS = 1.0f; static ImVec2 startMP;
    ImGuiIO& io = ImGui::GetIO(); 
    float baseW = 320.0f * g_autoScale; float baseH = 500.0f * g_autoScale;
    float currentW = baseW * g_scale;
    
    // 修复：折叠逻辑。我们使用 NoTitleBar 来完全接管标题栏，这样可以实现全标题栏点击折叠。
    float titleBarHeight = ImGui::GetFrameHeight();
    float currentH = g_menuCollapsed ? titleBarHeight : (baseH * g_scale);

    ImGui::SetNextWindowSize(ImVec2(currentW, currentH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_Always);

    // 修复：使用 NoTitleBar，因为我们要手动绘制标题栏以支持全区域点击折叠
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar)) {
        
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImRect titleBarRect(windowPos, windowPos + ImVec2(currentW, titleBarHeight));
        bool isHoveringTitle = titleBarRect.Contains(io.MousePos);

        // 修复：全标题栏点击折叠逻辑
        if (isHoveringTitle && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) {
            g_menuCollapsed = !g_menuCollapsed;
            SaveConfig();
        }

        // 修复：拖动逻辑。通过同步窗口位置到全局变量来解决拖动失效问题。
        if (!isScalingMenu && isHoveringTitle && ImGui::IsMouseDragging(0)) {
            g_menuX += io.MouseDelta.x;
            g_menuY += io.MouseDelta.y;
            if (ImGui::IsMouseReleased(0)) SaveConfig();
        }

        // 绘制标题栏背景（模拟原生风格）
        ImGui::GetWindowDrawList()->AddRectFilled(titleBarRect.Min, titleBarRect.Max, ImGui::GetColorU32(ImGuiCol_TitleBgActive));

        // 绘制折叠三角图标（模拟原生风格）
        float arrowSize = titleBarHeight * 0.4f;
        ImVec2 arrowPos = windowPos + ImVec2(ImGui::GetStyle().FramePadding.x + arrowSize * 0.5f, titleBarHeight * 0.5f);
        ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPos - ImVec2(arrowSize * 0.5f, arrowSize * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), g_menuCollapsed ? ImGuiDir_Right : ImGuiDir_Down, arrowSize);

        // 绘制标题文本
        ImGui::RenderText(windowPos + ImVec2(ImGui::GetStyle().FramePadding.x + arrowSize * 1.5f, ImGui::GetStyle().FramePadding.y), (const char*)u8"金铲铲助手");
        
        ImGui::ItemSize(ImVec2(0, titleBarHeight)); // 占位

        if (!g_menuCollapsed) {
            float expectedSize = 18.0f * g_autoScale * g_scale;
            ImGui::SetWindowFontScale(expectedSize / g_current_rendered_size);
            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            if (ImGui::CollapsingHeader((const char*)u8"预测功能")) {
                ImGui::Indent(); Toggle((const char*)u8"预测对手分布", &g_predict_enemy, 1); Toggle((const char*)u8"海克斯强化预测", &g_predict_hex, 2); ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8"透视功能")) {
                ImGui::Indent(); Toggle((const char*)u8"对手棋盘透视", &g_esp_board, 3); Toggle((const char*)u8"对手备战席透视", &g_esp_bench, 4); Toggle((const char*)u8"对手商店透视", &g_esp_shop, 5); ImGui::Unindent();
            }
            ImGui::Separator();
            Toggle((const char*)u8"全自动拿牌", &g_auto_buy, 6); Toggle((const char*)u8"极速秒退助手", &g_instant, 7);
            ImGui::Spacing();
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 45 * g_autoScale * g_scale))) SaveConfig();

            ImVec2 br = ImGui::GetWindowPos() + ImGui::GetWindowSize();
            float hSz = 50.0f * g_autoScale * g_scale; 
            if (ImGui::IsMouseClicked(0) && ImRect(br - ImVec2(hSz, hSz), br).Contains(io.MousePos)) { isScalingMenu = true; startMS = g_scale; startMP = io.MousePos; }
            if (isScalingMenu) { 
                if (ImGui::IsMouseDown(0)) {
                    float oldS = g_scale; g_scale = std::clamp(startMS + ((io.MousePos.x - startMP.x) / baseW), 0.5f, 5.0f);
                    g_menuX -= (baseW * g_scale - baseW * oldS) * 0.5f; g_menuY -= (baseH * g_scale - baseH * oldS) * 0.5f;
                } else { isScalingMenu = false; g_needUpdateFontSafe = true; SaveConfig(); } 
            }
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br - ImVec2(hSz*0.6f, 0), br - ImVec2(0, hSz*0.6f), IM_COL32(0, 120, 215, 200));
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 程序入口 (无 TLS 版本)
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    LoadConfig(); UpdateFontHD(true);  
    static bool running = true; 
    std::thread it([&] { while(running) { imgui.ProcessInputEvent(); std::this_thread::yield(); } });

    while (running) {
        if (g_needUpdateFontSafe) { UpdateFontHD(true); g_needUpdateFontSafe = false; }
        imgui.BeginFrame(); 
        if (!g_resLoaded) { 
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png"); 
            g_textureLoaded = (g_heroTexture != 0); g_resLoaded = true; 
        }
        DrawBoard(); DrawMenu();
        imgui.EndFrame(); 
        std::this_thread::yield();
    }
    running = false; 
    if (it.joinable()) it.join(); 

    // 修复：在程序退出前释放 OpenGL 资源
    if (g_heroTexture != 0) {
        glDeleteTextures(1, &g_heroTexture);
        g_heroTexture = 0;
    }
    if (g_HexShader.program != 0) {
        glDeleteProgram(g_HexShader.program);
        g_HexShader.program = 0;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
