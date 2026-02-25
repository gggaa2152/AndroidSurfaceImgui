// =================================================================
// 1. 头文件与宏定义 (必须置顶)
// =================================================================
#include "Global.h"             // 全局定义
#include "AImGui.h"             // Android ImGui 框架
#include "imgui_internal.h"      // 内部 API，用于 ImRect 交互
#include "imgui_impl_opengl3.h"  // OpenGL3 渲染后端

#define STB_IMAGE_IMPLEMENTATION // 启用图片解码实现
#include "stb_image.h"           

#include <thread>      
#include <cmath>       
#include <fstream>      
#include <string>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <algorithm>
#include <unistd.h>

// =================================================================
// 2. 全局状态变量 (保留 com.tencent.jkchess 原始字段名)
// =================================================================
bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false;  
bool g_auto_buy = false, g_instant = false;

bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

float g_scale = 1.0f, g_autoScale = 1.0f, g_current_rendered_size = 0.0f; 
float g_boardScale = 2.2f, g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    

ImVec2 g_menuPos = {100.0f, 100.0f}; 
bool g_needUpdateFont = false;

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 3. 渲染辅助：六边形 Shader 裁剪 (核心绘图逻辑)
// =================================================================
class HexShader {
public:
    GLuint program = 0;
    GLint resLoc = -1;
    void Init() {
        // 顶点着色器：处理坐标转换
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
        // 片元着色器：实现六边形裁剪算法
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

// 绘制英雄头像函数
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

// =================================================================
// 4. 工具函数：字体更新与贴图加载
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    // 确保字体大小不为 0
    float targetSize = std::max(10.0f, 18.0f * g_autoScale * g_scale); 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
}

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c;
    unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tid;
}

bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(), w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + ImGui::GetStyle().ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, window->GetID(label))) return false;
    bool hovered, held, pressed = ImGui::ButtonBehavior(bb, window->GetID(label), &hovered, &held);
    if (pressed) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.25f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + ImGui::GetStyle().ItemInnerSpacing.x, bb.Min.y + ImGui::GetStyle().FramePadding.y), label);
    return pressed;
}

// =================================================================
// 5. 棋盘绘制 (已修复 Expand 报错)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f, yStep = sz * 1.5f;

    static bool isDraggingB = false, isScalingB = false;
    static ImVec2 dragOff;
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0);
    float lastCY = g_startY + 3 * yStep;
    
    // 计算缩放手柄位置
    float a1 = -30.0f * M_PI / 180.0f, a2 = 30.0f * M_PI / 180.0f;
    ImVec2 p_top = ImVec2(lastCX + sz * cosf(a1), lastCY + sz * sinf(a1));
    ImVec2 p_bot = ImVec2(lastCX + sz * cosf(a2), lastCY + sz * sinf(a2));
    ImVec2 p_ext = ImVec2((p_top.x + p_bot.x) * 0.5f + sz * 0.6f, (p_top.y + p_bot.y) * 0.5f);
    d->AddTriangleFilled(p_top, p_bot, p_ext, IM_COL32(255, 215, 0, 240));

    // --- 交互判断逻辑 (修复报错点) ---
    if (ImGui::IsMouseClicked(0)) {
        // 修正：将 Expand 分步调用，避免链式引用 void
        ImRect scaleRect(p_top, p_ext);
        scaleRect.Expand(40.0f); 
        
        if (scaleRect.Contains(io.MousePos)) {
            isScalingB = true;
        } else {
            ImRect dragRect(ImVec2(g_startX - sz, g_startY - sz), ImVec2(lastCX + sz, lastCY + sz));
            if (dragRect.Contains(io.MousePos)) {
                isDraggingB = true; 
                dragOff = io.MousePos - ImVec2(g_startX, g_startY);
            }
        }
    }

    if (isScalingB && ImGui::IsMouseDown(0)) {
        float baseW = (6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale;
        g_boardManualScale = std::max((io.MousePos.x - g_startX) / baseW, 0.1f);
    } else { isScalingB = false; }

    if (isDraggingB && !isScalingB && ImGui::IsMouseDown(0)) {
        g_startX = io.MousePos.x - dragOff.x; g_startY = io.MousePos.y - dragOff.y;
    } else { isDraggingB = false; }

    // 绘制棋盘格
    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0);
            float cy = g_startY + r * yStep;
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz); 
            
            float rf, gf, bf;
            ImGui::ColorConvertHSVtoRGB(fmodf((float)ImGui::GetTime() * 0.5f + (cx+cy)*0.001f, 1.0f), 0.8f, 1.0f, rf, gf, bf);
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
// 6. 菜单 UI
// =================================================================
void DrawMenu() {
    static bool isScalingM = false, isDraggingM = false;
    static ImVec2 sMP, sPos; static float sMS;
    ImGuiIO& io = ImGui::GetIO();
    float bW = 320 * g_autoScale, bH = 500 * g_autoScale;

    ImGui::SetNextWindowSize({bW * g_scale, g_menuCollapsed ? ImGui::GetFrameHeight() : bH * g_scale});
    ImGui::SetNextWindowPos(g_menuPos);

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::IsMouseClicked(0) && ImGui::GetCurrentWindow()->TitleBarRect().Contains(io.MousePos)) {
            isDraggingM = true; sMP = io.MousePos; sPos = g_menuPos;
        }
        if (isDraggingM && ImGui::IsMouseDown(0)) g_menuPos = sPos + (io.MousePos - sMP);
        else isDraggingM = false;

        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuPos.y + ImGui::GetFrameHeight()) && ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) 
            g_menuCollapsed = !g_menuCollapsed;

        if (!g_menuCollapsed) {
            ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
            ImGui::TextColored(ImVec4(0,1,0.5,1), "FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader((const char*)u8"预测功能")) {
                ImGui::Indent(); Toggle((const char*)u8"预测对手分布", &g_predict_enemy, 1);
                Toggle((const char*)u8"海克斯强化预测", &g_predict_hex, 2); ImGui::Unindent();
            }
            if (ImGui::CollapsingHeader((const char*)u8"透视功能")) {
                ImGui::Indent(); Toggle((const char*)u8"对手棋盘透视", &g_esp_board, 3);
                Toggle((const char*)u8"对手备战席透视", &g_esp_bench, 4);
                Toggle((const char*)u8"对手商店透视", &g_esp_shop, 5); ImGui::Unindent();
            }
            ImGui::Separator();
            Toggle((const char*)u8"全自动拿牌", &g_auto_buy, 6);
            Toggle((const char*)u8"极速秒退助手", &g_instant, 7);

            ImVec2 br = g_menuPos + ImGui::GetWindowSize();
            float hSz = 60.0f * g_autoScale * g_scale;
            if (ImGui::IsMouseClicked(0) && ImRect(br-ImVec2(hSz,hSz), br).Contains(io.MousePos)) {
                isScalingM = true; sMS = g_scale; sMP = io.MousePos;
            }
            if (isScalingM && ImGui::IsMouseDown(0)) {
                g_scale = std::clamp(sMS + std::max((io.MousePos.x-sMP.x)/bW, (io.MousePos.y-sMP.y)/bH), 0.4f, 4.0f);
            } else if (isScalingM) { isScalingM = false; g_needUpdateFont = true; }
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br-ImVec2(hSz*0.4f,0), br-ImVec2(0,hSz*0.4f), IM_COL32(0,120,215,200));
        }
    }
    ImGui::End();
}

// =================================================================
// 7. 程序主入口
// =================================================================
int main() {
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true; 
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    UpdateFontHD(true); 
    static bool running = true;
    // 分离线程处理输入
    std::thread([&]{ while(running){ imgui.ProcessInputEvent(); std::this_thread::yield(); } }).detach();

    while (running) {
        if (g_needUpdateFont) { UpdateFontHD(true); g_needUpdateFont = false; }
        imgui.BeginFrame();
        
        if(!g_textureLoaded) {
            g_heroTexture = LoadTextureFromFile("/data/1/heroes/FUX/aurora.png");
            g_textureLoaded = (g_heroTexture != 0);
        }

        DrawBoard(); 
        DrawMenu();
        
        imgui.EndFrame();
        std::this_thread::yield();
    }
    return 0;
}
