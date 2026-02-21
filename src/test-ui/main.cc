#include "Global.h"
#include "AImGui.h"
#include <thread>
#include <cstdio>
#include <chrono>
#include <unistd.h>
#include <cmath>

// ========== 金铲铲助手数据 ==========
int gold = 100;
int level = 8;
int hp = 85;

// ========== 功能开关 ==========
bool g_featurePredict = false;     // 预测
bool g_featureESP = false;         // 透视
bool g_featureInstantQuit = false; // 秒退

// ========== 全局缩放控制 ==========
float g_globalScale = 1.0f;
const float MIN_SCALE = 0.5f;
const float MAX_SCALE = 3.0f;

// ========== 圆形菜单状态 ==========
bool g_menuOpen = true;             // 菜单是否打开
float g_circlePosX = 100;           // 圆形位置X
float g_circlePosY = 100;           // 圆形位置Y
float g_circleRadius = 40;          // 圆形半径

// ========== 配置文件路径 ==========
const char* CONFIG_PATH = "/data/local/tmp/jcc_assistant_config.txt";

// ========== 读取游戏数据 ==========
void ReadGameData() {
    FILE* f = fopen("/data/local/tmp/game_data.txt", "r");
    if (f) {
        fscanf(f, "gold=%d\n", &gold);
        fscanf(f, "level=%d\n", &level);
        fscanf(f, "hp=%d\n", &hp);
        fclose(f);
    }
}

// ========== 加载中文字体 ==========
void LoadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 一加/OPPO 系统字体
    const char* fontPath = "/system/fonts/SysSans-Hans-Regular.ttf";
    
    printf("[+] Loading font: %s\n", fontPath);
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    
    if (font) {
        printf("[+] Font loaded successfully\n");
        io.FontDefault = font;
    } else {
        printf("[-] Font loading failed, using default\n");
        io.Fonts->AddFontDefault();
    }
    
    io.Fonts->Build();
}

// ========== 保存配置 ==========
void SaveConfig() {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (f) {
        fprintf(f, "# JCC Assistant Config\n");
        fprintf(f, "scale=%.2f\n", g_globalScale);
        fprintf(f, "predict=%d\n", g_featurePredict ? 1 : 0);
        fprintf(f, "esp=%d\n", g_featureESP ? 1 : 0);
        fprintf(f, "instantQuit=%d\n", g_featureInstantQuit ? 1 : 0);
        fprintf(f, "circlePosX=%.2f\n", g_circlePosX);
        fprintf(f, "circlePosY=%.2f\n", g_circlePosY);
        fclose(f);
    }
}

// ========== 加载配置 ==========
void LoadConfig() {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            
            float fval;
            int ival;
            if (sscanf(line, "scale=%f", &fval) == 1) {
                g_globalScale = fval;
                if (g_globalScale < MIN_SCALE) g_globalScale = MIN_SCALE;
                if (g_globalScale > MAX_SCALE) g_globalScale = MAX_SCALE;
            }
            else if (sscanf(line, "predict=%d", &ival) == 1) {
                g_featurePredict = (ival != 0);
            }
            else if (sscanf(line, "esp=%d", &ival) == 1) {
                g_featureESP = (ival != 0);
            }
            else if (sscanf(line, "instantQuit=%d", &ival) == 1) {
                g_featureInstantQuit = (ival != 0);
            }
            else if (sscanf(line, "circlePosX=%f", &fval) == 1) {
                g_circlePosX = fval;
            }
            else if (sscanf(line, "circlePosY=%f", &fval) == 1) {
                g_circlePosY = fval;
            }
        }
        fclose(f);
        
        // 应用加载的缩放
        ImGui::GetIO().FontGlobalScale = g_globalScale;
    }
}

// ========== 绘制圆形图标（优化版） ==========
void DrawCircleIcon() {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    
    // 静态变量避免重复计算
    static bool isDragging = false;
    static bool wasMouseDown = false;
    
    bool isMouseDown = io.MouseDown[0];
    
    // ===== 拖动逻辑优化 =====
    if (isMouseDown) {
        if (!wasMouseDown) {
            // 只在按下瞬间检测点击区域（使用平方比较避免sqrt）
            float dx = io.MousePos.x - g_circlePosX;
            float dy = io.MousePos.y - g_circlePosY;
            float distSq = dx*dx + dy*dy;
            float radiusSq = g_circleRadius * g_circleRadius;
            
            if (distSq < radiusSq) {
                isDragging = true;
            }
        }
        
        // 拖动时直接更新位置（最快）
        if (isDragging) {
            g_circlePosX = io.MousePos.x;
            g_circlePosY = io.MousePos.y;
        }
    } else {
        if (isDragging) {
            isDragging = false;
            SaveConfig();  // 拖动结束保存位置
        } else if (!g_menuOpen && wasMouseDown) {
            // 点击打开菜单
            float dx = io.MousePos.x - g_circlePosX;
            float dy = io.MousePos.y - g_circlePosY;
            float distSq = dx*dx + dy*dy;
            float radiusSq = g_circleRadius * g_circleRadius;
            
            if (distSq < radiusSq) {
                g_menuOpen = true;
            }
        }
    }
    
    wasMouseDown = isMouseDown;
    
    // ===== 绘制（每帧执行，但很快） =====
    ImVec2 center(g_circlePosX, g_circlePosY);
    
    // 圆形背景
    drawList->AddCircleFilled(center, g_circleRadius, IM_COL32(0, 120, 255, 200), 32);
    
    // 白色外圈
    drawList->AddCircle(center, g_circleRadius, IM_COL32(255, 255, 255, 255), 32, 2.0f);
    
    // 显示开启功能数量
    int activeCount = (g_featurePredict ? 1 : 0) + 
                      (g_featureESP ? 1 : 0) + 
                      (g_featureInstantQuit ? 1 : 0);
    
    char text[8];
    snprintf(text, sizeof(text), "%d", activeCount);
    
    // 文字位置居中
    float textWidth = ImGui::CalcTextSize(text).x;
    float textHeight = ImGui::GetFontSize();
    ImVec2 textPos(center.x - textWidth/2, center.y - textHeight/2);
    
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
}

int main()
{
    printf("[1] Starting JCC Assistant...\n");
    
    // 先创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    // 加载字体
    LoadChineseFont();
    
    // 创建窗口
    android::AImGui imgui(android::AImGui::Options{
        .renderType = android::AImGui::RenderType::RenderNative,
        .autoUpdateOrientation = true
    });

    if (!imgui)
    {
        printf("[-] ImGui initialization failed\n");
        return 0;
    }
    
    printf("[2] AImGui created\n");

    // 加载配置
    LoadConfig();

    bool state = true;
    bool showDemoWindow = false;

    printf("[3] Entering main loop\n");
    
    while (state)
    {
        // 读取游戏数据
        ReadGameData();

        // 处理输入事件
        imgui.ProcessInputEvent();

        // 开始新帧
        imgui.BeginFrame();

        // 绘制圆形图标（始终绘制）
        DrawCircleIcon();

        // ========== 主菜单窗口 ==========
        if (g_menuOpen)
        {
            ImGui::SetNextWindowPos(ImVec2(g_circlePosX + g_circleRadius + 10, g_circlePosY), ImGuiCond_FirstUseEver);
            
            ImGui::Begin("金铲铲助手", &g_menuOpen, ImGuiWindowFlags_NoSavedSettings);
            
            // ===== 全局缩放控制 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "⚙️ 全局缩放");
            
            float prevScale = g_globalScale;
            if (ImGui::SliderFloat("缩放", &g_globalScale, MIN_SCALE, MAX_SCALE, "%.2f")) {
                ImGui::GetIO().FontGlobalScale = g_globalScale;
            }
            ImGui::SameLine();
            ImGui::Text("(%.0f%%)", g_globalScale * 100);
            
            if (prevScale != g_globalScale) {
                SaveConfig();
            }
            
            ImGui::Separator();
            
            // ===== 功能开关 =====
            ImGui::TextColored(ImVec4(1,1,0,1), "🔧 功能设置");
            
            bool prevPredict = g_featurePredict;
            bool prevESP = g_featureESP;
            bool prevInstantQuit = g_featureInstantQuit;
            
            // 1. 预测开关
            ImGui::Checkbox("1. 预测", &g_featurePredict);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后预测敌方下一步行动");
            }
            
            // 2. 透视开关
            ImGui::Checkbox("2. 透视", &g_featureESP);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后显示敌方位置");
            }
            
            // 3. 秒退开关
            ImGui::Checkbox("3. 秒退", &g_featureInstantQuit);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("开启后快速退出对局");
            }
            
            // 如果有变化就保存
            if (prevPredict != g_featurePredict || 
                prevESP != g_featureESP || 
                prevInstantQuit != g_featureInstantQuit) {
                SaveConfig();
            }
            
            ImGui::Separator();
            
            // ===== 游戏数据 =====
            ImGui::TextColored(ImVec4(1,1,0,1), "💰 金币: %d", gold);
            ImGui::TextColored(ImVec4(0,1,0,1), "📊 等级: %d", level);
            ImGui::TextColored(ImVec4(1,0,0,1), "❤️ 血量: %d", hp);
            
            // 进度条
            float progressWidth = 200.0f * g_globalScale;
            float progressHeight = 20.0f * g_globalScale;
            ImGui::ProgressBar(hp/100.0f, ImVec2(progressWidth, progressHeight), "");
            
            // ===== 当前功能状态 =====
            ImGui::TextColored(ImVec4(0,1,1,1), "📋 当前状态");
            ImGui::Text("预测: %s", g_featurePredict ? "✅开启" : "❌关闭");
            ImGui::Text("透视: %s", g_featureESP ? "✅开启" : "❌关闭");
            ImGui::Text("秒退: %s", g_featureInstantQuit ? "✅开启" : "❌关闭");
            
            ImGui::End();
        }

        // Demo窗口（可选）
        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        // 结束帧并渲染
        imgui.EndFrame();
        
        // 控制帧率
        usleep(16000);
    }

    // 退出前保存配置
    SaveConfig();

    printf("[4] JCC Assistant exited\n");
    return 0;
}
