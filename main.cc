// =================================================================
// 1. 系统适配与预处理 (TLS 内存对齐，解决 64 位安卓 Error 134)
// =================================================================
#ifdef __aarch64__
__attribute__((tls_model("initial-exec"))) 
__attribute__((aligned(64))) 
static thread_local char _tls_align_fix[64] = {0}; 
#endif

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
#include <algorithm>
#include <unistd.h>

// =================================================================
// 2. 全局状态变量 (com.tencent.jkchess 专用)
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini";

// 功能开关
bool g_predict_enemy = false, g_predict_hex = false;
bool g_esp_board = true, g_esp_bench = false, g_esp_shop = false;  
bool g_auto_buy = false, g_instant = false;

// UI 动画与状态
bool g_menuCollapsed = false; 
float g_anim[15] = {0.0f}; 

// 缩放与位置 (必须持久化)
float g_scale = 1.0f, g_autoScale = 1.0f, g_current_rendered_size = 0.0f; 
float g_boardScale = 2.2f, g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
ImVec2 g_menuPos = {100.0f, 100.0f}; 

// 标志位
bool g_needUpdateFont = false; 
GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 3. 配置持久化：一个都不能少 (保存与读取)
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << g_predict_enemy << " " << g_predict_hex << "\n";
        out << g_esp_board << " " << g_esp_bench << " " << g_esp_shop << "\n";
        out << g_auto_buy << " " << g_instant << "\n";
        out << g_scale << " " << g_boardManualScale << "\n";
        out << g_startX << " " << g_startY << "\n";
        out << g_menuPos.x << " " << g_menuPos.y << "\n";
        out.close();
    }
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (in.is_open()) {
        in >> g_predict_enemy >> g_predict_hex;
        in >> g_esp_board >> g_esp_bench >> g_esp_shop;
        in >> g_auto_buy >> g_instant;
        in >> g_scale >> g_boardManualScale;
        in >> g_startX >> g_startY;
        in >> g_menuPos.x >> g_menuPos.y;
        in.close();
        g_needUpdateFont = true; // 加载后标记需要应用缩放和字体
    }
}

// =================================================================
// 4. 六边形 Shader 裁剪逻辑
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
                         "void main() { Frag_UV = UV; vec2 ndc = (Position / u_Res) * 2.0 - 1.0; gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0); }";
        const char* fs = "#version 300 es\n"
                         "precision mediump float;\n"
                         "uniform sampler2D Texture;\n"
                         "in vec2 Frag_UV;\n"
                         "out vec4 Out_Color;\n"
                         "float sdHex(vec2 p, float r) { vec3 k = vec3(-0.866025, 0.5, 0.57735); p = abs(p);\n"
                         "    p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy; p -= vec2(clamp(p.x, -k.z * r, k.z * r), r); return length(p)*sign(p.y); }\n"
                         "void main() { vec2 p = (Frag_UV - 0.5) * 2.0; vec2 rotated_p = vec2(p.y, p.x); float d = sdHex(rotated_p, 0.92);\n"
                         "    float w = fwidth(d); float m = 1.0 - smoothstep(-w, w, d); vec4 tex = texture(Texture, Frag_UV);\n"
                         "    if(m <= 0.0) discard; Out_Color = tex * m; }";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        resLoc = glGetUniformLocation(program, "u_Res");
        glDeleteShader(v); glDeleteShader(f);
    }
} g_HexShader;
bool g_HexShaderInited = false;

// =================================================================
// 5. 全方位全局缩放 (严格执行顺序：在 BeginFrame 之前调用)
// =================================================================
void UpdateGlobalScale(bool forceFont = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f;
    g_autoScale = screenH / 1080.0f;
    float finalScale = g_autoScale * g_scale;

    // 字体更新 (只有在 Atlas 未锁定时才能执行)
    float targetSize = std::max(10.0f, 18.0f * finalScale); 
    if (forceFont || std::abs(targetSize - g_current_rendered_size) > 0.5f) {
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

    // 样式全方位缩放
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle def; style = def; 
    style.WindowPadding *= finalScale; style.WindowRounding *= finalScale;
    style.FramePadding *= finalScale; style.FrameRounding *= finalScale;
    style.ItemSpacing *= finalScale; style.ItemInnerSpacing *= finalScale;
    style.IndentSpacing *= finalScale; style.ScrollbarSize *= finalScale;
}

// =================================================================
// 6. UI 组件
// =================================================================
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

bool Toggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight(), w = h * 1.8f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + ImGui::GetStyle().ItemInnerSpacing.x + label_size.x, h));
    ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bb, window->GetID(label))) return false;
    bool hovered, held, pressed = ImGui::ButtonBehavior(bb, window->GetID(label), &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); } // 开关改变立刻保存
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.25f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImVec4(0, 0.45f, 0.9f, 0.8f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f * (g_autoScale * g_scale), IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + ImGui::GetStyle().ItemInnerSpacing.x, bb.Min.y + ImGui::GetStyle().FramePadding.y), label);
    return pressed;
}

// =================================================================
// 7. 业务绘制 (棋盘 + 菜单)
// =================================================================
void DrawBoard() {
    if (!g_esp_board) return;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sz = 38.0f * g_boardScale * g_autoScale * g_boardManualScale;
    float xStep = sz * 1.73205f, yStep = sz * 1.5f;
    static bool isDraggingB = false, isScalingB = false; static ImVec2 dragOff;
    float lastCX = g_startX + 6 * xStep + (3 % 2 == 1 ? xStep * 0.5f : 0), lastCY = g_startY + 3 * yStep;
    ImVec2 p_top = ImVec2(lastCX + sz * 0.866f, lastCY - sz * 0.5f), p_ext = ImVec2(lastCX + sz * 1.2f, lastCY);
    d->AddTriangleFilled(p_top, ImVec2(p_top.x, p_top.y + sz), p_ext, IM_COL32(255, 215, 0, 240));

    if (ImGui::IsMouseClicked(0)) {
        ImRect scR(p_top, p_ext); scR.Expand(40.0f);
        if (scR.Contains(io.MousePos)) isScalingB = true;
        else if (ImRect(ImVec2(g_startX-sz, g_startY-sz), ImVec2(lastCX+sz, lastCY+sz)).Contains(io.MousePos)) {
            isDraggingB = true; dragOff = io.MousePos - ImVec2(g_startX, g_startY);
        }
    }
    if (isScalingB && ImGui::IsMouseDown(0)) g_boardManualScale = std::max((io.MousePos.x - g_startX) / ((6.5f * 1.73205f + 1.0f) * 38.0f * g_boardScale * g_autoScale), 0.1f);
    else if (isScalingB) { isScalingB = false; SaveConfig(); }
    if (isDraggingB && !isScalingB && ImGui::IsMouseDown(0)) { g_startX = io.MousePos.x - dragOff.x; g_startY = io.MousePos.y - dragOff.y; }
    else if (isDraggingB) { isDraggingB = false; SaveConfig(); }

    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r % 2 == 1 ? xStep * 0.5f : 0), cy = g_startY + r * yStep;
            if(g_enemyBoard[r][c] && g_textureLoaded) DrawHero(d, ImVec2(cx, cy), sz); 
            d->AddCircle(ImVec2(cx, cy), sz, IM_COL32(0, 255, 255, 200), 6, 2.0f);
        }
    }
}

void DrawMenu() {
    static bool isScalingM = false, isDraggingM = false; static ImVec2 sMP, sPos; static float sMS;
    ImGuiIO& io = ImGui::GetIO(); float baseW = 320.0f, baseH = 500.0f, curS = g_autoScale * g_scale;
    ImGui::SetNextWindowSize({baseW * curS, g_menuCollapsed ? ImGui::GetFrameHeight() : baseH * curS});
    ImGui::SetNextWindowPos(g_menuPos);

    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::IsMouseClicked(0) && ImGui::GetCurrentWindow()->TitleBarRect().Contains(io.MousePos)) {
            isDraggingM = true; sMP = io.MousePos; sPos = g_menuPos;
        }
        if (isDraggingM && ImGui::IsMouseDown(0)) g_menuPos = sPos + (io.MousePos - sMP);
        else if (isDraggingM) { isDraggingM = false; SaveConfig(); }

        if (ImGui::IsWindowHovered() && io.MousePos.y < (g_menuPos.y + ImGui::GetFrameHeight()) && ImGui::IsMouseReleased(0)) g_menuCollapsed = !g_menuCollapsed;

        if (!g_menuCollapsed) {
            Toggle((const char*)u8"预测对手分布", &g_predict_enemy, 1);
            Toggle((const char*)u8"对手棋盘透视", &g_esp_board, 3);
            Toggle((const char*)u8"全自动拿牌", &g_auto_buy, 6);

            ImVec2 br = g_menuPos + ImGui::GetWindowSize(); float hSz = 60.0f * curS;
            if (ImGui::IsMouseClicked(0) && ImRect(br - ImVec2(hSz, hSz), br).Contains(io.MousePos)) { isScalingM = true; sMS = g_scale; sMP = io.MousePos; }
            if (isScalingM && ImGui::IsMouseDown(0)) {
                g_scale = std::clamp(sMS + std::max((io.MousePos.x-sMP.x)/(baseW*g_autoScale), (io.MousePos.y-sMP.y)/(baseH*g_autoScale)), 0.5f, 3.0f);
                UpdateGlobalScale(); 
            } else if (isScalingM) { isScalingM = false; g_needUpdateFont = true; SaveConfig(); }
            ImGui::GetWindowDrawList()->AddTriangleFilled(br, br - ImVec2(hSz*0.4f, 0), br - ImVec2(0, hSz*0.4f), IM_COL32(0, 120, 215, 200));
        }
    }
    ImGui::End();
}

// =================================================================
// 8. 主程序：严格执行顺序
// =================================================================
int main() {
    _tls_align_fix[0] = 1; ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    LoadConfig(); // 1. 启动即加载
    UpdateGlobalScale(true); // 2. 初始应用缩放
    
    static bool running = true;
    std::thread([&]{ while(running){ imgui.ProcessInputEvent(); std::this_thread::yield(); } }).detach();

    while (running) {
        // --- 核心修复：修改字体必须在 BeginFrame 之前 ---
        if (g_needUpdateFont) { UpdateGlobalScale(true); g_needUpdateFont = false; }

        imgui.BeginFrame(); 
        if(!g_textureLoaded) {
            g_heroTexture = (int)stbi_load("/data/1/heroes/FUX/aurora.png", NULL, NULL, NULL, 4); // 简化加载逻辑示例
            if(g_heroTexture) {
                GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 100, 100, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void*)g_heroTexture);
                g_heroTexture = tid; g_textureLoaded = true;
            }
        }
        DrawBoard(); DrawMenu();
        imgui.EndFrame();
        std::this_thread::yield();
    }
    return 0;
}
