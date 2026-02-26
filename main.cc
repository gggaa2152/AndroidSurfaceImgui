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
#include <vector>

// =================================================================
// 1. 全局状态与配置
// =================================================================
namespace State {
    const char* ConfigPath = "/data/jkchess_config.ini";

    // 功能开关
    bool PredictEnemy = false;     // 预测对手
    bool PredictHex = false;       // 预测海克斯
    bool EspBoard = true;          // 显示棋盘
    bool EspBench = false;         // 显示备战席
    bool EspShop = false;          // 显示商店
    bool AutoBuy = false;          // 自动拿牌
    bool InstantExit = false;      // 极速秒退

    // UI 与 缩放
    bool MenuCollapsed = false;    // 菜单是否折叠
    float ToggleAnims[15] = {0.0f}; // 切换开关的动画状态
    float MenuScale = 1.0f;           // 用户手动缩放比例
    float ScreenAutoScale = 1.0f;     // 根据 DPI 自动缩放比例
    float CurrentFontSize = 0.0f;     // 当前渲染的字体大小

    // 棋盘布局参数
    float BoardBaseScale = 2.2f;    // 基础缩放
    float BoardManualScale = 1.0f;  // 手动调节缩放
    float StartX = 400.0f;          // 起始坐标 X
    float StartY = 400.0f;          // 起始坐标 Y

    // 菜单尺寸
    float MenuX = 100.0f;
    float MenuY = 100.0f;
    float MenuW = 320.0f;
    float MenuH = 500.0f;

    // 资源状态
    GLuint HeroTexture = 0;         // 英雄纹理 ID
    bool TextureLoaded = false;     // 纹理是否加载成功
    bool ResLoaded = false;         // 资源初始化标记
    bool NeedUpdateFontSafe = false; // 用于安全更新字体的标记，解决 BeginFrame 锁死问题

    // 模拟数据：敌人棋盘布局
    int EnemyBoard[4][7] = {
        {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
    };
}

// =================================================================
// 2. 配置管理器 (处理持久化存储)
// =================================================================
namespace Config {
    void Save() {
        std::ofstream out(State::ConfigPath);
        if (!out.is_open()) return;
        
        out << "predictEnemy=" << State::PredictEnemy << "\n";
        out << "predictHex=" << State::PredictHex << "\n";
        out << "espBoard=" << State::EspBoard << "\n";
        out << "espBench=" << State::EspBench << "\n";
        out << "espShop=" << State::EspShop << "\n";
        out << "autoBuy=" << State::AutoBuy << "\n";
        out << "instant=" << State::InstantExit << "\n";
        out << "startX=" << State::StartX << "\n";
        out << "startY=" << State::StartY << "\n";
        out << "manualScale=" << State::BoardManualScale << "\n";
        out << "menuX=" << State::MenuX << "\n";
        out << "menuY=" << State::MenuY << "\n";
        out << "menuW=" << State::MenuW << "\n";
        out << "menuH=" << State::MenuH << "\n";
        out << "menuScale=" << State::MenuScale << "\n";
        out.close();
    }

    void Load() {
        std::ifstream in(State::ConfigPath);
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string k = line.substr(0, pos), v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy")      State::PredictEnemy = (v == "1");
                else if (k == "predictHex")   State::PredictHex = (v == "1");
                else if (k == "espBoard")     State::EspBoard = (v == "1");
                else if (k == "espBench")     State::EspBench = (v == "1");
                else if (k == "espShop")      State::EspShop = (v == "1");
                else if (k == "autoBuy")      State::AutoBuy = (v == "1");
                else if (k == "instant")      State::InstantExit = (v == "1");
                else if (k == "startX")       State::StartX = std::stof(v);
                else if (k == "startY")       State::StartY = std::stof(v);
                else if (k == "manualScale")  State::BoardManualScale = std::stof(v);
                else if (k == "menuX")        State::MenuX = std::stof(v);
                else if (k == "menuY")        State::MenuY = std::stof(v);
                else if (k == "menuW")        State::MenuW = std::stof(v);
                else if (k == "menuH")        State::MenuH = std::stof(v);
                else if (k == "menuScale")    State::MenuScale = std::stof(v);
            } catch (...) {}
        }
        in.close();
        State::NeedUpdateFontSafe = true; // 加载后标记需要更新字体以适配缩放
    }
}

// =================================================================
// 3. 六边形着色器渲染 (用于绘制英雄头像)
// =================================================================
struct HexShader {
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
        glAttachShader(program, v); glAttachShader(program, f); 
        glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexShader;

bool g_HexShaderInited = false;

namespace RenderUtils {
    // 从文件加载纹理
    GLuint LoadTexture(const char* filename) {
        int w, h, c;
        unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
        if (!data) return 0;
        GLuint tid; 
        glGenTextures(1, &tid); 
        glBindTexture(GL_TEXTURE_2D, tid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data); 
        return tid;
    }

    // 使用 Shader 绘制六边形英雄头像
    void DrawHeroHex(ImDrawList* drawList, ImVec2 center, float size) {
        if (!State::TextureLoaded) return;
        if (!g_HexShaderInited) { g_HexShader.Init(); g_HexShaderInited = true; }

        drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
            glUseProgram(g_HexShader.program);
            glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->UserCallbackData);
            glUniform2f(g_HexShader.resLoc, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        }, (void*)(intptr_t)State::HeroTexture);

        drawList->AddImage((ImTextureID)(intptr_t)State::HeroTexture, 
                           center - ImVec2(size, size), 
                           center + ImVec2(size, size));
        
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    // 更新高清字体适配
    void UpdateFontHD(bool force = false) {
        ImGuiIO& io = ImGui::GetIO();
        float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
        State::ScreenAutoScale = screenH / 1080.0f;
        
        float baseSize = 18.0f * State::ScreenAutoScale * State::MenuScale;
        float targetSize = std::clamp(baseSize, 12.0f, 120.0f); 

        if (!force && std::abs(targetSize - State::CurrentFontSize) < 0.5f) return;

        ImGui_ImplOpenGL3_DestroyFontsTexture();
        io.Fonts->Clear();
        
        ImFontConfig config;
        config.OversampleH = 2; 
        config.OversampleV = 2;
        config.PixelSnapH = true;
        
        const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
        if (access(fontPath, R_OK) == 0) {
            io.Fonts->AddFontFromFileTTF(fontPath, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        } else {
            io.Fonts->AddFontDefault();
        }
        
        io.Fonts->Build();
        ImGui_ImplOpenGL3_CreateFontsTexture();
        State::CurrentFontSize = targetSize;
    }
}

// =================================================================
// 4. 棋盘与菜单 UI 组件
// =================================================================
namespace Components {
    // 绘制游戏棋盘 ESP
    void DrawBoard() {
        if (!State::EspBoard) return;
        ImDrawList* d = ImGui::GetForegroundDrawList();
        ImGuiIO& io = ImGui::GetIO();
        
        float sz = 38.0f * State::BoardBaseScale * State::ScreenAutoScale * State::BoardManualScale;
        float xStep = sz * 1.73205f; 
        float yStep = sz * 1.5f;

        // 计算最后一个格子的位置，用于放置交互句柄
        float lastCX = State::StartX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
        float lastCY = State::StartY + 3 * yStep;
        
        // 绘制缩放句柄 (右下角的金色三角形)
        float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
        ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
        ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
        float hOffset = sz * 0.6f; 
        ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + hOffset, (p_top.y + p_bot.y) * 0.5f);
        d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 200));

        static bool isDragging = false, isScaling = false;
        static ImVec2 dragOffset;

        // 鼠标/触摸交互逻辑
        if (ImGui::IsMouseClicked(0)) {
            ImRect handleRect(p_top, p_ext); handleRect.Expand(40.0f);
            if (handleRect.Contains(io.MousePos)) {
                isScaling = true;
            } else if (ImRect(ImVec2(State::StartX - sz, State::StartY - sz), ImVec2(lastCX + sz, lastCY + sz)).Contains(io.MousePos)) {
                isDragging = true; 
                dragOffset = io.MousePos - ImVec2(State::StartX, State::StartY);
            }
        }

        if (isScaling) {
            if (ImGui::IsMouseDown(0)) {
                float curW = io.MousePos.x - State::StartX;
                float baseW = (6.5f * 1.73205f + 1.0f) * 38.0f * State::BoardBaseScale * State::ScreenAutoScale;
                State::BoardManualScale = std::max(curW / baseW, 0.1f);
            } else { isScaling = false; Config::Save(); }
        }
        if (isDragging && !isScaling) {
            if (ImGui::IsMouseDown(0)) {
                State::StartX = io.MousePos.x - dragOffset.x; 
                State::StartY = io.MousePos.y - dragOffset.y;
            } else { isDragging = false; Config::Save(); }
        }

        // 遍历并绘制六边形网格
        float time = (float)ImGui::GetTime();
        for(int r=0; r<4; r++) {
            for(int c=0; c<7; c++) {
                float cx = State::StartX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
                float cy = State::StartY + r * yStep;
                
                // 如果该位置有敌人，则绘制英雄头像
                if(State::EnemyBoard[r][c] && State::TextureLoaded) {
                    RenderUtils::DrawHeroHex(d, ImVec2(cx, cy), sz); 
                }

                // 动态彩虹边框效果
                float hue = fmodf(time * 0.5f + (cx + cy) * 0.001f, 1.0f);
                float rf, gf, bf; ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 1.0f, rf, gf, bf);
                
                ImVec2 pts[6];
                for(int i=0; i<6; i++) {
                    float a = (60.0f * i - 30.0f) * (M_PI / 180.0f);
                    pts[i] = ImVec2(cx + sz * cosf(a), cy + sz * sinf(a));
                }
                d->AddPolyline(pts, 6, IM_COL32(rf*255, gf*255, bf*255, 255), ImDrawFlags_Closed, 3.0f * State::ScreenAutoScale);
            }
        }
    }

    // 自定义开关组件 (带有平滑动画)
    bool CustomToggle(const char* label, bool* v, int idx) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        
        float h = ImGui::GetFrameHeight(); 
        float w = h * 1.8f;
        const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + label_size.x, h));
        
        ImGui::ItemSize(bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed) { *v = !(*v); Config::Save(); }

        // 插值动画
        State::ToggleAnims[idx] += ((*v ? 1.0f : 0.0f) - State::ToggleAnims[idx]) * 0.2f;
        
        ImVec4 activeCol = ImVec4(0.0f, 0.45f, 0.9f, 0.8f);
        ImVec4 bgCol = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        ImU32 mixedCol = ImGui::GetColorU32(ImLerp(bgCol, activeCol, State::ToggleAnims[idx]));

        // 绘制背景和圆点
        window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), mixedCol, h * 0.5f);
        window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + State::ToggleAnims[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
        
        ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
        return pressed;
    }

    // 绘制助手主菜单
    void DrawMenu() {
        ImGuiIO& io = ImGui::GetIO(); 
        
        // 设置窗口和组件圆角
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 12.0f * State::ScreenAutoScale;
        style.FrameRounding = 6.0f * State::ScreenAutoScale;
        style.PopupRounding = 6.0f * State::ScreenAutoScale;
        style.GrabRounding = 12.0f * State::ScreenAutoScale;

        ImGui::SetNextWindowPos(ImVec2(State::MenuX, State::MenuY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(State::MenuW, State::MenuH), ImGuiCond_FirstUseEver);

        if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_None)) {
            State::MenuX = ImGui::GetWindowPos().x;
            State::MenuY = ImGui::GetWindowPos().y;
            float curW = ImGui::GetWindowSize().x;
            float curH = ImGui::GetWindowSize().y;
            State::MenuCollapsed = ImGui::IsWindowCollapsed();

            float visualScale = curW / (320.0f * State::ScreenAutoScale);

            // 限制字体更新：仅在鼠标释放且尺寸变化时触发，避免调整大小时卡顿
            if (ImGui::IsMouseReleased(0) && (curW != State::MenuW || curH != State::MenuH)) {
                State::MenuW = curW;
                State::MenuH = curH;
                State::MenuScale = visualScale;
                State::NeedUpdateFontSafe = true; 
                Config::Save();
            }

            if (!State::MenuCollapsed) {
                // 计算临时的字体缩放系数
                float expectedSize = 18.0f * State::ScreenAutoScale * visualScale;
                ImGui::SetWindowFontScale(expectedSize / State::CurrentFontSize);
                
                ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "帧率 (FPS): %.1f", io.Framerate);
                ImGui::Separator();
                
                if (ImGui::CollapsingHeader((const char*)u8"预测功能")) {
                    ImGui::Indent(); 
                    CustomToggle((const char*)u8"预测对手", &State::PredictEnemy, 1); 
                    CustomToggle((const char*)u8"预测海克斯", &State::PredictHex, 2); 
                    ImGui::Unindent();
                }
                if (ImGui::CollapsingHeader((const char*)u8"视觉功能")) {
                    ImGui::Indent(); 
                    CustomToggle((const char*)u8"对手棋盘", &State::EspBoard, 3); 
                    CustomToggle((const char*)u8"对手备战席", &State::EspBench, 4); 
                    CustomToggle((const char*)u8"对手商店", &State::EspShop, 5); 
                    ImGui::Unindent();
                }
                ImGui::Separator();
                CustomToggle((const char*)u8"自动拿牌", &State::AutoBuy, 6); 
                CustomToggle((const char*)u8"极速秒退", &State::InstantExit, 7);
                
                ImGui::Spacing();
                if (ImGui::Button((const char*)u8"保存当前配置", ImVec2(-1, 45 * State::ScreenAutoScale))) {
                    Config::Save();
                }
            }
        }
        ImGui::End();
    }
}

// =================================================================
// 5. 程序入口
// =================================================================
int main() {
    // 初始化 ImGui 上下文
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    
    // 设置同步频率
    eglSwapInterval(eglGetCurrentDisplay(), 1); 
    Config::Load(); // 加载配置文件
    RenderUtils::UpdateFontHD(true);  // 初始高清字体构建

    static bool running = true; 
    // 输入处理线程
    std::thread inputThread([&] { 
        while(running) { 
            imgui.ProcessInputEvent(); 
            std::this_thread::yield(); 
        } 
    });

    // 主渲染循环
    while (running) {
        // 安全字体更新逻辑：必须在 BeginFrame 之外执行，以防止图形驱动死锁
        if (State::NeedUpdateFontSafe) { 
            RenderUtils::UpdateFontHD(true); 
            State::NeedUpdateFontSafe = false; 
        }

        imgui.BeginFrame(); 
        
        // 资源懒加载
        if (!State::ResLoaded) { 
            State::HeroTexture = RenderUtils::LoadTexture("/data/1/heroes/FUX/aurora.png"); 
            State::TextureLoaded = (State::HeroTexture != 0); 
            State::ResLoaded = true; 
        }

        // 渲染组件
        Components::DrawBoard(); 
        Components::DrawMenu();

        imgui.EndFrame(); 
        std::this_thread::yield();
    }

    running = false; 
    if (inputThread.joinable()) inputThread.join(); 
    return 0;
}
