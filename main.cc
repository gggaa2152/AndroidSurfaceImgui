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
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>
#include <atomic>       // 新增，用于原子变量
#include <chrono>       // 新增，用于 sleep_for

// =================================================================
// 常量定义（替代魔法数字）
// =================================================================
namespace Constants {
    const char* CONFIG_PATH = "/data/jkchess_config.ini";
    const char* FONT_PATH = "/system/fonts/SysSans-Hans-Regular.ttf";
    const char* HERO_TEXTURE_PATH = "/data/1/heroes/FUX/aurora.png";

    const float HEX_SIZE_BASE = 38.0f;          // 六边形基础大小
    const float HEX_X_STEP_FACTOR = 1.73205f;   // sqrt(3)
    const float HEX_Y_STEP_FACTOR = 1.5f;
    const float HEX_HANDLE_OFFSET_FACTOR = 0.6f;
    const float BOARD_SCALE_DEFAULT = 2.2f;
    
    const float MENU_WIDTH_BASE = 320.0f;       // 菜单基础宽度
    const float MENU_HEIGHT_BASE = 500.0f;      // 菜单基础高度
    const float MENU_SCALE_MIN = 0.5f;          // 最小缩放系数
    const float MENU_SCALE_MAX = 5.0f;          // 最大缩放系数
    const float FONT_SIZE_BASE = 18.0f;         // 基础字体大小
    const float FONT_SIZE_MAX = 120.0f;         // 最大字体限制
    const float REFERENCE_SCREEN_HEIGHT = 1080.0f; // 参考屏幕高度
}

// =================================================================
// 全局状态变量（按功能分组到命名空间）
// =================================================================
namespace Config {
    bool predict_enemy = false;
    bool predict_hex = false;
    bool esp_board = true;
    bool esp_bench = false;
    bool esp_shop = false;
    bool auto_buy = false;
    bool instant = false;
}

namespace UI {
    bool menuCollapsed = false;
    float anim[15] = {0.0f};
    float scale = 1.0f;
    float autoScale = 1.0f;
    float current_rendered_size = 0.0f;
    float menuX = 100.0f;
    float menuY = 100.0f;
}

namespace Board {
    float boardScale = Constants::BOARD_SCALE_DEFAULT;
    float boardManualScale = 1.0f;
    float startX = 400.0f;
    float startY = 400.0f;
    int enemyBoard[4][7] = {
        {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
    };
}

namespace Resources {
    GLuint heroTexture = 0;
    bool textureLoaded = false;
    bool resLoaded = false;
}

// 标记：仅用于解决 BeginFrame 锁死问题，不改动业务逻辑
bool g_needUpdateFontSafe = false;

// =================================================================
// 配置管理
// =================================================================
void SaveConfig() {
    std::ofstream out(Constants::CONFIG_PATH);
    if (!out.is_open()) {
        __android_log_print(ANDROID_LOG_ERROR, "SaveConfig", "Failed to open config file for writing");
        return;
    }
    out << "predictEnemy=" << Config::predict_enemy << "\n";
    out << "predictHex=" << Config::predict_hex << "\n";
    out << "espBoard=" << Config::esp_board << "\n";
    out << "espBench=" << Config::esp_bench << "\n";
    out << "espShop=" << Config::esp_shop << "\n";
    out << "autoBuy=" << Config::auto_buy << "\n";
    out << "instant=" << Config::instant << "\n";
    out << "startX=" << Board::startX << "\n";
    out << "startY=" << Board::startY << "\n";
    out << "manualScale=" << Board::boardManualScale << "\n";
    out << "menuX=" << UI::menuX << "\n";
    out << "menuY=" << UI::menuY << "\n";
    out << "menuScale=" << UI::scale << "\n";
    out.close();
}

void LoadConfig() {
    std::ifstream in(Constants::CONFIG_PATH);
    if (!in.is_open()) {
        __android_log_print(ANDROID_LOG_WARN, "LoadConfig", "Config file not found, using defaults");
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos), v = line.substr(pos + 1);
        try {
            if (k == "predictEnemy") Config::predict_enemy = (v == "1");
            else if (k == "predictHex") Config::predict_hex = (v == "1");
            else if (k == "espBoard") Config::esp_board = (v == "1");
            else if (k == "espBench") Config::esp_bench = (v == "1");
            else if (k == "espShop") Config::esp_shop = (v == "1");
            else if (k == "autoBuy") Config::auto_buy = (v == "1");
            else if (k == "instant") Config::instant = (v == "1");
            else if (k == "startX") Board::startX = std::stof(v);
            else if (k == "startY") Board::startY = std::stof(v);
            else if (k == "manualScale") Board::boardManualScale = std::stof(v);
            else if (k == "menuX") UI::menuX = std::stof(v);
            else if (k == "menuY") UI::menuY = std::stof(v);
            else if (k == "menuScale") UI::scale = std::stof(v);
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "LoadConfig", "Parse error: %s", e.what());
        }
    }
    in.close();
    g_needUpdateFontSafe = true;
}

// =================================================================
// 渲染辅助
// =================================================================
class HexShader {
public:
    GLuint program = 0;
    GLint resLoc = -1;
    bool valid = false;

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
        glShaderSource(v, 1, &vs, NULL);
        glCompileShader(v);
        GLint compiled;
        glGetShaderiv(v, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(v, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                std::string infoLog(infoLen, '\0');
                glGetShaderInfoLog(v, infoLen, NULL, &infoLog[0]);
                __android_log_print(ANDROID_LOG_ERROR, "HexShader", "Vertex shader compile error:\n%s", infoLog.c_str());
            }
            glDeleteShader(v);
            valid = false;
            return;
        }

        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, NULL);
        glCompileShader(f);
        glGetShaderiv(f, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(f, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                std::string infoLog(infoLen, '\0');
                glGetShaderInfoLog(f, infoLen, NULL, &infoLog[0]);
                __android_log_print(ANDROID_LOG_ERROR, "HexShader", "Fragment shader compile error:\n%s", infoLog.c_str());
            }
            glDeleteShader(v);
            glDeleteShader(f);
            valid = false;
            return;
        }

        glAttachShader(program, v);
        glAttachShader(program, f);
        glLinkProgram(program);
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint infoLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                std::string infoLog(infoLen, '\0');
                glGetProgramInfoLog(program, infoLen, NULL, &infoLog[0]);
                __android_log_print(ANDROID_LOG_ERROR, "HexShader", "Program link error:\n%s", infoLog.c_str());
            }
            glDeleteShader(v);
            glDeleteShader(f);
            glDeleteProgram(program);
            program = 0;
            valid = false;
            return;
        }

        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v);
        glDeleteShader(f);
        valid = true;
        __android_log_print(ANDROID_LOG_INFO, "HexShader", "Shader initialized successfully");
    }
} g_HexShader;
bool g_HexShaderInited = false;

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) {
        __android_log_print(ANDROID_LOG_ERROR, "LoadTexture", "Failed to load texture: %s", filename);
        return 0;
    }
    GLuint tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    __android_log_print(ANDROID_LOG_INFO, "LoadTexture", "Loaded texture %s, id=%u", filename, tid);
    return tid;
}

void DrawHero(ImDrawList* drawList, ImVec2 center, float size) {
    if (!Resources::textureLoaded || !g_HexShader.valid) return;
    if (!g_HexShaderInited) {
        g_HexShader.Init();
        g_HexShaderInited = true;
    }
    drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        glUseProgram(g_HexShader.program);
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
        glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, (void*)(intptr_t)Resources::heroTexture);
    drawList->AddImage((ImTextureID)(intptr_t)Resources::heroTexture,
                       center - ImVec2(size, size),
                       center + ImVec2(size, size));
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    UI::autoScale = screenH / Constants::REFERENCE_SCREEN_HEIGHT;
    float baseSize = Constants::FONT_SIZE_BASE * UI::autoScale * UI::scale;
    float targetSize = std::min(baseSize, Constants::FONT_SIZE_MAX);
    if (!force && std::abs(targetSize - UI::current_rendered_size) < 0.5f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    ImFontConfig config;
    config.OversampleH = 1;
    config.PixelSnapH = true;
    if (access(Constants::FONT_PATH, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(Constants::FONT_PATH, targetSize, &config,
                                      io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        __android_log_print(ANDROID_LOG_WARN, "UpdateFontHD", "Font file not found, using default font");
    }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    UI::current_rendered_size = targetSize;
}

// =================================================================
// 棋盘绘制
// =================================================================
void DrawBoard() {
    if (!Config::esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    float sz = Constants::HEX_SIZE_BASE * Board::boardScale * UI::autoScale * Board::boardManualScale;
    float xStep = sz * Constants::HEX_X_STEP_FACTOR;
    float yStep = sz * Constants::HEX_Y_STEP_FACTOR;

    float lastCX = Board::startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = Board::startY + 3 * yStep;

    float a1 = -30.0f * M_PI / 180.0f;
    float a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    float hOffset = sz * Constants::HEX_HANDLE_OFFSET_FACTOR;
    ImVec2 p_ext((p_top.x + p_bot.x) * 0.5f + hOffset, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 240));

    static bool isDraggingBoard = false, isScalingBoard = false;
    static ImVec2 dragOffset;

    if (ImGui::IsMouseClicked(0)) {
        // 手动实现矩形包含检测，避免使用 ImRect
        float left = std::min(p_top.x, p_ext.x);
        float right = std::max(p_top.x, p_ext.x);
        float top = std::min(p_top.y, p_ext.y);
        float bottom = std::max(p_top.y, p_ext.y);
        const float expand = 40.0f;
        bool inHandle = (io.MousePos.x >= left - expand && io.MousePos.x <= right + expand &&
                         io.MousePos.y >= top - expand && io.MousePos.y <= bottom + expand);

        float boardLeft = Board::startX - sz;
        float boardRight = lastCX + sz;
        float boardTop = Board::startY - sz;
        float boardBottom = lastCY + sz;
        bool inBoard = (io.MousePos.x >= boardLeft && io.MousePos.x <= boardRight &&
                        io.MousePos.y >= boardTop && io.MousePos.y <= boardBottom);

        if (inHandle) {
            isScalingBoard = true;
        } else if (inBoard) {
            isDraggingBoard = true;
            dragOffset = io.MousePos - ImVec2(Board::startX, Board::startY);
        }
    }

    if (isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            float curW = io.MousePos.x - Board::startX;
            float baseW = (6.5f * Constants::HEX_X_STEP_FACTOR + 1.0f) *
                          Constants::HEX_SIZE_BASE * Board::boardScale * UI::autoScale;
            Board::boardManualScale = std::max(curW / baseW, 0.1f);
        } else {
            isScalingBoard = false;
            SaveConfig();
        }
    }

    if (isDraggingBoard && !isScalingBoard) {
        if (ImGui::IsMouseDown(0)) {
            Board::startX = io.MousePos.x - dragOffset.x;
            Board::startY = io.MousePos.y - dragOffset.y;
        } else {
            isDraggingBoard = false;
            SaveConfig();
        }
    }

    float time = (float)ImGui::GetTime();
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 7; c++) {
            float cx = Board::startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = Board::startY + r * yStep;
            if (Board::enemyBoard[r][c] && Resources::textureLoaded)
                DrawHero(d, ImVec2(cx, cy), sz);
            float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, rf, gf, bf);
            ImVec2 pts[6];
            for (int i = 0; i < 6; i++) {
                float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
            }
            d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 255),
                           ImDrawFlags_Closed, 4.0f * UI::autoScale);
        }
    }
}

// =================================================================
// 菜单 UI（使用公开API，但仍保留 imgui_internal.h）
// =================================================================
bool Toggle(const char* label, bool* v, int idx) {
    ImGuiIO& io = ImGui::GetIO();
    float h = ImGui::GetFrameHeight();
    float w = h * 1.8f;
    ImVec2 labelSize = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // 创建不可见按钮，用于捕获点击和交互状态
    ImGui::InvisibleButton(label, ImVec2(w + ImGui::GetStyle().ItemInnerSpacing.x + labelSize.x, h));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    bool clicked = ImGui::IsItemClicked();

    if (clicked) {
        *v = !(*v);
        SaveConfig();
    }

    // 动画插值
    UI::anim[idx] += ((*v ? 1.0f : 0.0f) - UI::anim[idx]) * 0.2f;

    // 绘制背景和滑块（使用公开的绘制列表）
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(cursorPos, cursorPos + ImVec2(w, h),
        ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),
                                   ImVec4(0, 0.45f, 0.9f, 0.8f), UI::anim[idx])),
        h * 0.5f);
    drawList->AddCircleFilled(cursorPos + ImVec2(h * 0.5f + UI::anim[idx] * (w - h), h * 0.5f),
                               h * 0.5f - 2.5f, IM_COL32_WHITE);

    // 绘制标签
    ImGui::SetCursorScreenPos(cursorPos + ImVec2(w + ImGui::GetStyle().ItemInnerSpacing.x, 0));
    ImGui::Text("%s", label);

    return clicked;
}

void DrawMenu() {
    static bool isScalingMenu = false;
    static float startMS = 1.0f;
    static ImVec2 startMP;

    ImGuiIO& io = ImGui::GetIO();
    float baseW = Constants::MENU_WIDTH_BASE * UI::autoScale;
    float baseH = Constants::MENU_HEIGHT_BASE * UI::autoScale;
    float currentW = baseW * UI::scale;
    float currentH = UI::menuCollapsed ? ImGui::GetFrameHeight() : (baseH * UI::scale);

    ImGui::SetNextWindowSize(ImVec2(currentW, currentH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(UI::menuX, UI::menuY), ImGuiCond_Always);

    if (ImGui::Begin("金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        // 标题栏交互（手动检测区域）
        float titleBarHeight = ImGui::GetFrameHeight();
        ImVec2 windowPos = ImGui::GetWindowPos();
        float mouseY = io.MousePos.y;
        bool hoverTitle = (mouseY >= windowPos.y && mouseY <= windowPos.y + titleBarHeight);

        if (hoverTitle && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) {
            UI::menuCollapsed = !UI::menuCollapsed;
            SaveConfig();
        }

        if (!isScalingMenu && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0) && hoverTitle) {
            UI::menuX += io.MouseDelta.x;
            UI::menuY += io.MouseDelta.y;
            if (ImGui::IsMouseReleased(0)) SaveConfig();
        }

        if (!UI::menuCollapsed) {
            float expectedSize = Constants::FONT_SIZE_BASE * UI::autoScale * UI::scale;
            ImGui::SetWindowFontScale(expectedSize / UI::current_rendered_size);

            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();

            if (ImGui::CollapsingHeader("预测功能")) {
                ImGui::Indent();
                Toggle("预测对手分布", &Config::predict_enemy, 1);
                Toggle("海克斯强化预测", &Config::predict_hex, 2);
                ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader("透视功能")) {
                ImGui::Indent();
                Toggle("对手棋盘透视", &Config::esp_board, 3);
                Toggle("对手备战席透视", &Config::esp_bench, 4);
                Toggle("对手商店透视", &Config::esp_shop, 5);
                ImGui::Unindent();
            }
            ImGui::Separator();
            Toggle("全自动拿牌", &Config::auto_buy, 6);
            Toggle("极速秒退助手", &Config::instant, 7);
            ImGui::Spacing();

            if (ImGui::Button("保存设置", ImVec2(-1, 45 * UI::autoScale * UI::scale))) {
                SaveConfig();
            }

            // 右下角缩放手柄
            ImVec2 br = ImGui::GetWindowPos() + ImGui::GetWindowSize();
            float hSz = 50.0f * UI::autoScale * UI::scale;
            ImVec2 handleMin = br - ImVec2(hSz, hSz);
            ImVec2 handleMax = br;

            // 绘制三角形
            ImGui::GetWindowDrawList()->AddTriangleFilled(br,
                br - ImVec2(hSz * 0.6f, 0),
                br - ImVec2(0, hSz * 0.6f),
                IM_COL32(0, 120, 215, 200));

            // 检测手柄点击（手动矩形）
            bool inHandle = (io.MousePos.x >= handleMin.x && io.MousePos.x <= handleMax.x &&
                             io.MousePos.y >= handleMin.y && io.MousePos.y <= handleMax.y);
            if (ImGui::IsMouseClicked(0) && inHandle) {
                isScalingMenu = true;
                startMS = UI::scale;
                startMP = io.MousePos;
            }
            if (isScalingMenu) {
                if (ImGui::IsMouseDown(0)) {
                    float oldS = UI::scale;
                    UI::scale = std::clamp(startMS + ((io.MousePos.x - startMP.x) / baseW),
                                           Constants::MENU_SCALE_MIN, Constants::MENU_SCALE_MAX);
                    UI::menuX -= (baseW * UI::scale - baseW * oldS) * 0.5f;
                    UI::menuY -= (baseH * UI::scale - baseH * oldS) * 0.5f;
                } else {
                    isScalingMenu = false;
                    g_needUpdateFontSafe = true;
                    SaveConfig();
                }
            }
        }
    }
    ImGui::End();
}

// =================================================================
// 程序入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    eglSwapInterval(eglGetCurrentDisplay(), 1);

    LoadConfig();
    UpdateFontHD(true);

    std::atomic<bool> running = true;
    std::thread it([&] {
        while (running) {
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

        if (!Resources::resLoaded) {
            Resources::heroTexture = LoadTextureFromFile(Constants::HERO_TEXTURE_PATH);
            Resources::textureLoaded = (Resources::heroTexture != 0);
            Resources::resLoaded = true;
        }

        DrawBoard();
        DrawMenu();

        imgui.EndFrame();

        // 降低 CPU 占用
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    running = false;
    if (it.joinable()) it.join();

    // 释放纹理资源
    if (Resources::heroTexture != 0) {
        glDeleteTextures(1, &Resources::heroTexture);
    }

    return 0;
}
