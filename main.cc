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
#include <atomic>
#include <GLES3/gl3.h>
#include <EGL/egl.h>    
#include <android/log.h>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

// --- 底层注入依赖 ---
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/uio.h>
#include <sys/errno.h>

// 由 build.yml 自动生成的头文件 (包含 internal_hook.cc 编译出的 SO 数组)
#include "hook_payload.h"

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); printf("[INFO] " __VA_ARGS__); printf("\n"); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); printf("\033[31m[ERROR] " __VA_ARGS__); printf("\033[0m\n"); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态变量
// =================================================================
bool g_predict_enemy = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_boardLocked = false; 

float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 

float g_boardScale = 2.2f;       
float g_boardManualScale = 1.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
float g_menuX = 100.0f, g_menuY = 100.0f, g_menuW = 350.0f, g_menuH = 550.0f; 
float g_benchX = 200.0f, g_benchY = 700.0f, g_benchScale = 1.0f;
float g_shopX = 200.0f, g_shopY = 850.0f, g_shopScale = 1.0f;

float g_anim[25] = {0.0f}; 

// 模拟数据 (后续通过读取内存更新)
int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, 
    {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, 
    {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 核心辅助绘图工具
// =================================================================

void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "espBoard=" << g_esp_board << "\n";
        out << "boardLocked=" << g_boardLocked << "\n";
        out << "startX=" << g_startX << "\n"; 
        out << "startY=" << g_startY << "\n";
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
            std::string k = line.substr(0, pos), v = line.substr(pos+1);
            if (k == "espBoard") g_esp_board = (v == "1");
            else if (k == "boardLocked") g_boardLocked = (v == "1");
            else if (k == "startX") g_startX = std::stof(v);
            else if (k == "startY") g_startY = std::stof(v);
        }
        in.close();
    }
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f; 
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(20.0f * g_autoScale, 15.0f, 50.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture(); 
    io.Fonts->Clear(); 
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) {
        io.Fonts->AddFontFromFileTTF(fontPath, targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        io.Fonts->AddFontDefault();
    }
    io.Fonts->Build(); 
    ImGui_ImplOpenGL3_CreateFontsTexture(); 
    g_current_rendered_size = targetSize;
}

// =================================================================
// 3. UI 绘制逻辑 (子函数必须在渲染线程之上)
// =================================================================

void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, float& t_x, float& t_y, float& t_scale, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx, float h_dy, bool locked) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 p_scale(out_x + h_dx * out_scale, out_y + h_dy * out_scale);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (ImLengthSqr(io.MousePos - p_scale) < 5000.0f * g_autoScale) { 
                isScaling = true; scaleDragOffset = io.MousePos - p_scale; 
            } else {
                ImRect area(ImVec2(out_x - 50, out_y - 50), ImVec2(out_x + h_dx*out_scale + 50, out_y + h_dy*out_scale + 50));
                if (area.Contains(io.MousePos)) { isDragging = true; dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); }
            }
        }
        if (isScaling && ImGui::IsMouseDown(0)) {
            ImVec2 target = io.MousePos - scaleDragOffset;
            float dist = sqrtf(powf(target.x - t_x, 2) + powf(target.y - t_y, 2));
            t_scale = std::clamp(dist / h_dx, 0.5f, 5.0f);
        } else isScaling = false;
        if (isDragging && !isScaling && ImGui::IsMouseDown(0)) { t_x = io.MousePos.x + dragOffset.x; t_y = io.MousePos.y + dragOffset.y; }
        else isDragging = false;
    }
    float s = 1.0f - expf(-20.0f * io.DeltaTime); 
    out_x = ImLerp(out_x, t_x, s); out_y = ImLerp(out_y, t_y, s); out_scale = ImLerp(out_scale, t_scale, s);
}

void DrawBoard() {
    if (!g_esp_board) return; 
    ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    
    float baseSz = 40.0f * g_boardScale * g_autoScale; 
    float curXStep = baseSz * 1.732f * g_boardManualScale; 
    float curYStep = baseSz * 1.5f * g_boardManualScale;

    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 6.0f * curXStep, 3.0f * curYStep, g_boardLocked);

    for(int r = 0; r < 4; r++) { 
        for(int c = 0; c < 7; c++) { 
            float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0); 
            float cy = g_startY + r * curYStep; 
            if(g_enemyBoard[r][c]) {
                d->AddCircleFilled(ImVec2(cx, cy), baseSz * g_boardManualScale * 0.6f, IM_COL32(255, 0, 0, 150));
                d->AddCircle(ImVec2(cx, cy), baseSz * g_boardManualScale * 0.6f, IM_COL32_WHITE, 0, 2.0f);
            } else {
                d->AddCircle(ImVec2(cx, cy), baseSz * g_boardManualScale * 0.2f, IM_COL32(255, 255, 255, 50));
            }
        } 
    }
}

void DrawBench() {
    if (!g_esp_bench) return; 
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float sz = 45.0f * g_autoScale;
    for (int i = 0; i < 9; i++) {
        d->AddRect(ImVec2(g_benchX + i*sz, g_benchY), ImVec2(g_benchX + (i+1)*sz, g_benchY+sz), IM_COL32(200, 200, 200, 100));
    }
}

bool ModernToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); 
    const ImGuiStyle& style = ImGui::GetStyle(); 
    const ImGuiID id = window->GetID(label);
    float h = ImGui::GetFrameHeight() * 0.8f; float w = h * 2.1f;
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x, h));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held; if (ImGui::ButtonBehavior(bb, id, &hovered, &held)) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.22f, 0.27f, 1.0f), ImVec4(0.0f, 0.85f, 0.55f, 1.0f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y), label); return true;
}

void DrawMenu() {
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver); 
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((const char*)u8"金铲铲助手 V1.0", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        g_menuX = ImGui::GetWindowPos().x; g_menuY = ImGui::GetWindowPos().y;
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] Dobby Hook: 已就绪");
        ImGui::Separator();
        ModernToggle((const char*)u8"敌方棋盘显示", &g_esp_board, 1);
        ModernToggle((const char*)u8"备战席显示", &g_esp_bench, 2);
        ModernToggle((const char*)u8"锁定所有窗口", &g_boardLocked, 3);
        ImGui::SliderFloat((const char*)u8"菜单缩放", &g_scale, 0.5f, 2.0f);
        if (ImGui::Button((const char*)u8"保存当前布局", ImVec2(-1, 40*g_autoScale))) SaveConfig();
    }
    ImGui::End();
}

// =================================================================
// 4. 渲染与注入核心
// =================================================================

void MainRenderThread() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    LoadConfig(); UpdateFontHD(true);  
    std::thread it([&] { while(g_game_running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
    while (g_game_running) {
        imgui.BeginFrame(); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        DrawBoard(); DrawBench(); DrawMenu();
        imgui.EndFrame(); std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (it.joinable()) it.join(); ImGui::DestroyContext();
}

long get_module_base_remote(pid_t pid, const char* module_name) {
    FILE *fp; long addr = 0; char filename[64], line[1024]; 
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r"); 
    if (fp) { 
        while (fgets(line, sizeof(line), fp)) { 
            if (strstr(line, module_name)) { addr = strtoul(line, NULL, 16); break; } 
        } fclose(fp); 
    } return addr;
}

int perform_injection(pid_t pid, const char* drop_path) {
    LOGI("[*] 正在执行 Ptrace 注入流程...");
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = (void*)get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    
    struct user_pt_regs regs, saved; struct iovec iov = {&regs, sizeof(regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov); memcpy(&saved, &regs, sizeof(regs));
    for (int i = 0; i < 6; i++) regs.regs[i] = m_p[i];
    regs.pc = (uintptr_t)r_mmap; regs.regs[30] = 0;
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    long r_mem = regs.regs[0];

    char buf[256] = {0}; strncpy(buf, drop_path, 255);
    for (size_t i = 0; i < sizeof(buf); i += 8) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));

    void* r_dl = (void*)get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = (void*)get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    
    regs.regs[0] = r_mem; regs.regs[1] = RTLD_NOW;
    regs.pc = (uintptr_t)r_dl; regs.regs[30] = 0;
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    long h = regs.regs[0];

    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, (void*)&saved);
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    return (h == 0) ? -1 : 0;
}

int main(int argc, char** argv) {
    LOGI("=============================================");
    LOGI("   JKHelper Daemon 稳定版已启动");
    LOGI("=============================================");
    system("setenforce 0 > /dev/null 2>&1");
    while (true) {
        pid_t pid = 0; uid_t game_uid = 0;
        DIR* dir = opendir("/proc");
        if (dir) {
            struct dirent* ptr;
            while ((ptr = readdir(dir)) != NULL) {
                if (ptr->d_type == DT_DIR && atoi(ptr->d_name) > 0) {
                    char path[256]; snprintf(path, 256, "/proc/%s/cmdline", ptr->d_name);
                    std::ifstream f_cmd(path, std::ios::binary);
                    if (f_cmd) {
                        std::string cmd; std::getline(f_cmd, cmd, '\0'); 
                        if (cmd == TARGET_PACKAGE) {
                            pid = atoi(ptr->d_name);
                            struct stat st; snprintf(path, 256, "/proc/%d", pid);
                            if (stat(path, &st) == 0) game_uid = st.st_uid;
                            break; 
                        }
                    }
                }
            } closedir(dir);
        }
        if (pid > 0) {
            FILE* f = fopen(DROP_SO_PATH, "wb");
            if(f) {
                fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
                chmod(DROP_SO_PATH, 0777); if (game_uid > 0) chown(DROP_SO_PATH, game_uid, game_uid);
                system("chcon u:object_r:apk_data_file:s0 /data/data/com.tencent.jkchess/cache/libJKHook.so > /dev/null 2>&1");
            }
            if (get_module_base_remote(pid, "libJKHook.so") != 0) {
                if (!g_game_running) {
                    LOGI("[*] 载荷已激活，拉起 UI...");
                    g_game_running = true; std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            } else {
                LOGI("[!] 捕获游戏进程 (%d)，正在注入...", pid);
                while (get_module_base_remote(pid, "libil2cpp.so") == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
                if (perform_injection(pid, DROP_SO_PATH) == 0) {
                    g_game_running = true; std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
