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
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>

// =================================================================
// 1. 全局配置与状态 (恢复原始变量名)
// =================================================================
const char* g_configPath = "/data/jkchess_config.ini"; 

bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_auto_buy = false;
bool g_instant = false;

float g_menuX = 100.0f, g_menuY = 100.0f;
float g_menuW = 380.0f, g_menuH = 600.0f;
float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_startX = 400.0f, g_startY = 400.0f;
float g_boardManualScale = 1.0f;

bool g_needUpdateFontSafe = false;
float g_anim[20] = {0.0f}; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false; 

int g_enemyGrid[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 持久化管理 (恢复原始函数名)
// =================================================================
void SaveConfig() {
    std::ofstream out(g_configPath);
    if (!out.is_open()) return;
    out << "p_enemy=" << g_predict_enemy << "\n";
    out << "p_hex=" << g_predict_hex << "\n";
    out << "e_board=" << g_esp_board << "\n";
    out << "m_x=" << g_menuX << "\n";
    out << "m_y=" << g_menuY << "\n";
    out << "m_scale=" << g_scale << "\n";
    out << "b_x=" << g_startX << "\n";
    out << "b_y=" << g_startY << "\n";
    out << "b_scale=" << g_boardManualScale << "\n";
    out.close();
}

void LoadConfig() {
    std::ifstream in(g_configPath);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos), v = line.substr(pos + 1);
        try {
            if (k == "p_enemy") g_predict_enemy = (v == "1");
            else if (k == "m_x") g_menuX = std::stof(v);
            else if (k == "m_y") g_menuY = std::stof(v);
            else if (k == "m_scale") g_scale = std::stof(v);
            else if (k == "b_x") g_startX = std::stof(v);
            else if (k == "b_y") g_startY = std::stof(v);
            else if (k == "b_scale") g_boardManualScale = std::stof(v);
        } catch (...) {}
    }
}

// =================================================================
// 3. 字体与纹理管理 (恢复原始函数名)
// =================================================================
void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    g_autoScale = io.DisplaySize.y / 1080.0f;
    float target = std::clamp(18.0f * g_autoScale * g_scale, 12.0f, 80.0f);
    
    if (!force && std::abs(target - g_current_rendered_size) < 0.2f) return;

    ImGui_ImplOpenGL3_DestroyFontsTexture(); 
    io.Fonts->Clear();
    
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) != 0) fontPath = "/system/fonts/DroidSansFallback.ttf";
    
    io.Fonts->AddFontFromFileTTF(fontPath, target, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    
    // 必须调用此函数以构建图集，防止断言失败
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = target;
}

GLuint LoadTextureFromFile(const char* path) {
    int w, h, n;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) return 0;
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data); return id;
}

// =================================================================
// 4. 渲染引擎
// =================================================================
class HexRenderer {
public:
    GLuint program = 0;
    GLint uRes = -1;
    void Init() {
        const char* vs = "#version 300 es\nlayout(location=0) in vec2 Position;layout(location=1) in vec2 UV;out vec2 v_uv;uniform vec2 u_res;void main() {v_uv = UV;vec2 ndc = (Position / u_res) * 2.0 - 1.0;gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);}";
        const char* fs = "#version 300 es\nprecision mediump float;uniform sampler2D u_tex;in vec2 v_uv;out vec4 o_color;float sdHex(vec2 p, float r) {vec3 k = vec3(-0.866, 0.5, 0.577);p = abs(p); p -= 2.0*min(dot(k.xy, p), 0.0)*k.xy;p -= vec2(clamp(p.x, -k.z*r, k.z*r), r);return length(p)*sign(p.y);}void main() {vec2 p = (v_uv - 0.5) * 2.0;float d = sdHex(vec2(p.y, p.x), 0.95);float edge = 1.0 - smoothstep(-0.03, 0.01, d);o_color = texture(u_tex, v_uv) * edge;if(o_color.a < 0.1) discard;}";
        program = glCreateProgram();
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        uRes = glGetUniformLocation(program, "u_res");
    }
} g_HexRender;

void DrawHexHero(ImDrawList* dl, ImVec2 pos, float size) {
    if (!g_textureLoaded) return;
    static bool inited = false; if(!inited) { g_HexRender.Init(); inited=true; }
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
        glUseProgram(g_HexRender.program);
        glUniform2f(g_HexRender.uRes, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }, 0);
    dl->AddImage((ImTextureID)(intptr_t)g_heroTexture, pos - ImVec2(size, size), pos + ImVec2(size, size));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void RenderWorldESP() {
    if (!g_esp_board) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float baseCell = 40.0f * 2.2f * g_autoScale;
    float cellSz = baseCell * g_boardManualScale;
    float xStep = cellSz * 1.732f, yStep = cellSz * 1.5f;
    ImVec2 handlePos = ImVec2(g_startX + 6.5f * xStep, g_startY + 3.5f * yStep);
    
    static bool dragging = false, scaling = false;
    static ImVec2 offset;
    if (ImGui::IsMouseClicked(0)) {
        if (ImLengthSqr(io.MousePos - handlePos) < 2000.0f) scaling = true;
        else if (ImRect(ImVec2(g_startX - cellSz, g_startY - cellSz), handlePos).Contains(io.MousePos)) { dragging = true; offset = io.MousePos - ImVec2(g_startX, g_startY); }
    }
    if (scaling && ImGui::IsMouseDown(0)) g_boardManualScale = std::max((io.MousePos.x - g_startX) / (baseCell * 7.0f), 0.3f);
    else if (dragging && ImGui::IsMouseDown(0)) { g_startX = io.MousePos.x - offset.x; g_startY = io.MousePos.y - offset.y; }
    else if (scaling || dragging) { scaling = dragging = false; SaveConfig(); }

    for(int r=0; r<4; r++) {
        for(int c=0; c<7; c++) {
            float cx = g_startX + c * xStep + (r%2 ? xStep*0.5f : 0);
            float cy = g_startY + r * yStep;
            if (g_enemyGrid[r][c]) DrawHexHero(dl, ImVec2(cx, cy), cellSz * 0.92f);
            ImVec2 pts[6];
            for(int i=0; i<6; i++) {
                float a = (60.0f * i - 30.0f) * M_PI / 180.0f;
                pts[i] = ImVec2(cx + cellSz * cosf(a), cy + cellSz * sinf(a));
            }
            dl->AddPolyline(pts, 6, IM_COL32(0, 255, 200, 180), ImDrawFlags_Closed, 2.5f);
        }
    }
    dl->AddCircleFilled(handlePos, 15.0f * g_autoScale, IM_COL32(255, 200, 0, 200));
}

// =================================================================
// 5. 交互组件
// =================================================================
bool ModernToggle(const char* label, bool* v, int id) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID im_id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight() * 0.8f, w = h * 2.2f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + 10 + label_size.x, h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, im_id)) return false;
    bool hovered, held, pressed = ImGui::ButtonBehavior(bb, im_id, &hovered, &held);
    if (pressed) { *v = !(*v); SaveConfig(); }
    g_anim[id] += ((*v ? 1.0f : 0.0f) - g_anim[id]) * 0.15f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.2f, 0.25f, 1.0f), ImVec4(0.1f, 0.75f, 0.55f, 1.0f), g_anim[id])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[id]*(w-h), h*0.5f), h*0.4f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + 8, bb.Min.y), label);
    return pressed;
}

void RenderMenu() {
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((const char*)u8"JK助手", NULL, ImGuiWindowFlags_NoCollapse)) {
        g_menuX = ImGui::GetWindowPos().x; g_menuY = ImGui::GetWindowPos().y;
        if (ImGui::IsMouseReleased(0) && std::abs(ImGui::GetWindowWidth() - g_menuW) > 5.0f) {
            g_menuW = ImGui::GetWindowWidth(); g_scale = g_menuW / 380.0f;
            g_needUpdateFontSafe = true; SaveConfig();
        }
        ImGui::SetWindowFontScale((18.0f * g_autoScale * g_scale) / g_current_rendered_size);
        if (ImGui::BeginChild("Func", ImVec2(0, 0), true)) {
            ModernToggle((const char*)u8"敌方棋盘透视", &g_esp_board, 1);
            ModernToggle((const char*)u8"海克斯预测", &g_predict_hex, 2);
            ModernToggle((const char*)u8"对手阵容分析", &g_predict_enemy, 3);
            ModernToggle((const char*)u8"智能拿牌", &g_auto_buy, 4);
            ModernToggle((const char*)u8"极速退房", &g_instant, 5);
            if (ImGui::Button((const char*)u8"保存设置", ImVec2(-1, 40 * g_autoScale))) SaveConfig();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// =================================================================
// 6. 主程序入口
// =================================================================
int main() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative});
    
    LoadConfig();
    UpdateFontHD(true); 

    static bool running = true;
    std::thread inputThread([&]{
        while(running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    });

    while (running) {
        if (g_needUpdateFontSafe) { 
            UpdateFontHD(true); 
            g_needUpdateFontSafe = false; 
        }
        
        imgui.BeginFrame();
        
        if (!g_textureLoaded) {
            // 恢复为你代码中原始的路径逻辑
            g_heroTexture = LoadTextureFromFile("/data/1/hero.png"); 
            g_textureLoaded = (g_heroTexture != 0);
        }

        RenderWorldESP();
        RenderMenu();

        imgui.EndFrame();
        std::this_thread::yield();
    }

    running = false;
    if(inputThread.joinable()) inputThread.join();
    return 0;
}
