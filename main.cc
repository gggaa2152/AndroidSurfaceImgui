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

// 由 build.yml 自动生成的头文件 (包含 internal_hook.cc 编译出的 SO 数组)
#include "hook_payload.h"

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

const char* TARGET_PACKAGE = "com.tencent.jkchess";
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态变量
// =================================================================
bool g_predict_enemy = false;
bool g_predict_hex = false;
bool g_esp_board = true;
bool g_esp_bench = false; 
bool g_esp_shop = false;  
bool g_esp_level = false; 
bool g_auto_buy = false;
bool g_boardLocked = false; 

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

float g_benchX = 200.0f;
float g_benchY = 700.0f;
float g_benchScale = 1.0f;

float g_shopX = 200.0f;
float g_shopY = 850.0f;
float g_shopScale = 1.0f;

bool g_menuCollapsed = false; 
float g_anim[25] = {0.0f}; 

GLuint g_heroTexture = 0;           
bool g_textureLoaded = false;    
bool g_resLoaded = false; 
bool g_needUpdateFontSafe = false;

ImFont* g_mainFont = nullptr;

// 模拟数据
int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, 
    {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, 
    {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 配置管理与绘图工具函数
// =================================================================

void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "predictEnemy=" << g_predict_enemy << "\n";
        out << "espBoard=" << g_esp_board << "\n";
        out << "boardLocked=" << g_boardLocked << "\n";
        out << "startX=" << g_startX << "\n"; 
        out << "startY=" << g_startY << "\n";
        out << "menuX=" << g_menuX << "\n"; 
        out << "menuY=" << g_menuY << "\n";
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
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            try {
                if (k == "predictEnemy") g_predict_enemy = (v == "1");
                else if (k == "espBoard") g_esp_board = (v == "1");
                else if (k == "boardLocked") g_boardLocked = (v == "1");
                else if (k == "startX") g_startX = std::stof(v); 
                else if (k == "startY") g_startY = std::stof(v);
                else if (k == "menuX") g_menuX = std::stof(v); 
                else if (k == "menuY") g_menuY = std::stof(v);
            } catch (...) {}
        }
        in.close();
    }
}

GLuint LoadTextureFromFile(const char* filename) {
    int w, h, c; unsigned char* data = stbi_load(filename, &w, &h, &c, 4);
    if (!data) return 0;
    GLuint tid; glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); 
    stbi_image_free(data); return tid;
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenH = (io.DisplaySize.y > 100.0f) ? io.DisplaySize.y : 2400.0f; 
    g_autoScale = screenH / 1080.0f;
    float targetSize = std::clamp(18.0f * g_autoScale, 12.0f, 60.0f); 
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    
    ImGui_ImplOpenGL3_DestroyFontsTexture(); 
    io.Fonts->Clear(); 
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) {
        g_mainFont = io.Fonts->AddFontFromFileTTF(fontPath, targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    } else {
        g_mainFont = io.Fonts->AddFontDefault();
    }
    io.Fonts->Build(); 
    ImGui_ImplOpenGL3_CreateFontsTexture(); 
    g_current_rendered_size = targetSize;
}

// =================================================================
// 3. UI 交互与子绘制函数 (定义在 MainRenderThread 之上)
// =================================================================

void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, float& t_x, float& t_y, float& t_scale, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx_unscaled, float h_dy_unscaled, float hitMinX_unscaled, float hitMinY_unscaled, float hitMaxX_unscaled, float hitMaxY_unscaled, bool locked, bool* isOpen) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 p_scale(out_x + h_dx_unscaled * out_scale, out_y + h_dy_unscaled * out_scale);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (ImLengthSqr(io.MousePos - p_scale) < (4900.0f * g_autoScale * g_autoScale)) { 
                isScaling = true; 
                scaleDragOffset = io.MousePos - ImVec2(t_x + h_dx_unscaled * t_scale, t_y + h_dy_unscaled * t_scale); 
            } else {
                ImRect area(ImVec2(out_x + hitMinX_unscaled * out_scale, out_y + hitMinY_unscaled * out_scale), ImVec2(out_x + hitMaxX_unscaled * out_scale, out_y + hitMaxY_unscaled * out_scale));
                if (area.Contains(io.MousePos)) { 
                    isDragging = true; 
                    dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); 
                }
            }
        }
        if (isScaling) { 
            if (ImGui::IsMouseDown(0)) { 
                ImVec2 target = io.MousePos - scaleDragOffset; 
                float dist = sqrtf(powf(target.x - t_x, 2) + powf(target.y - t_y, 2)); 
                t_scale = std::clamp(dist / h_dx_unscaled, 0.2f, 5.0f); 
            } else isScaling = false; 
        }
        if (isDragging && !isScaling) { 
            if (ImGui::IsMouseDown(0)) { 
                t_x = io.MousePos.x + dragOffset.x; 
                t_y = io.MousePos.y + dragOffset.y; 
            } else isDragging = false; 
        }
    }
    float s = 1.0f - expf(-20.0f * io.DeltaTime); 
    out_x = ImLerp(out_x, t_x, s); out_y = ImLerp(out_y, t_y, s); out_scale = ImLerp(out_scale, t_scale, s);
}

void DrawBoard() {
    if (!g_esp_board) return; 
    ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    
    float baseSz = 38.0f * g_boardScale * g_autoScale; 
    float curXStep = baseSz * 1.732f * g_boardManualScale; 
    float curYStep = baseSz * 1.5f * g_boardManualScale;

    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 7.0f * baseSz, 2.0f * baseSz, -baseSz*2, -baseSz*2, 8.0f*baseSz, 5.0f*baseSz, g_boardLocked, &g_esp_board);

    for(int r = 0; r < 4; r++) { 
        for(int c = 0; c < 7; c++) { 
            float cx = g_startX + c * curXStep + (r % 2 == 1 ? curXStep * 0.5f : 0); 
            float cy = g_startY + r * curYStep; 
            if(g_enemyBoard[r][c]) {
                d->AddCircle(ImVec2(cx, cy), baseSz * g_boardManualScale * 0.8f, IM_COL32(0, 255, 0, 200), 6, 2.0f);
            }
        } 
    }
}

void DrawBench() {
    if (!g_esp_bench) return; 
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float baseSz = 40.0f * g_autoScale;
    for (int i = 0; i < 9; i++) {
        d->AddRect(ImVec2(g_benchX + i * baseSz, g_benchY), ImVec2(g_benchX + (i+1)*baseSz, g_benchY + baseSz), IM_COL32(255, 255, 255, 100));
    }
}

void DrawShop() {
    if (!g_esp_shop) return; 
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float baseSz = 55.0f * g_autoScale;
    for (int i = 0; i < 5; i++) {
        d->AddRect(ImVec2(g_shopX + i * baseSz, g_shopY), ImVec2(g_shopX + (i+1)*baseSz, g_shopY + baseSz), IM_COL32(255, 215, 0, 150));
    }
}

bool ModernToggle(const char* label, bool* v, int idx) {
    ImGuiWindow* window = ImGui::GetCurrentWindow(); 
    const ImGuiStyle& style = ImGui::GetStyle(); 
    const ImGuiID id = window->GetID(label);
    float h = ImGui::GetFrameHeight() * 0.85f; 
    float w = h * 2.1f; 
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x, h));
    ImGui::ItemSize(bb); 
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held; 
    if (ImGui::ButtonBehavior(bb, id, &hovered, &held)) *v = !(*v);
    g_anim[idx] += ((*v ? 1.0f : 0.0f) - g_anim[idx]) * 0.2f;
    window->DrawList->AddRectFilled(bb.Min, bb.Min + ImVec2(w, h), ImGui::GetColorU32(ImLerp(ImVec4(0.2f, 0.22f, 0.27f, 1.0f), ImVec4(0.0f, 0.85f, 0.55f, 1.0f), g_anim[idx])), h*0.5f);
    window->DrawList->AddCircleFilled(bb.Min + ImVec2(h*0.5f + g_anim[idx]*(w-h), h*0.5f), h*0.5f - 2.5f, IM_COL32_WHITE);
    ImGui::RenderText(ImVec2(bb.Min.x + w + style.ItemInnerSpacing.x, bb.Min.y), label); 
    return true;
}

void DrawMenu() {
    ImGui::SetNextWindowPos(ImVec2(g_menuX, g_menuY), ImGuiCond_FirstUseEver); 
    ImGui::SetNextWindowSize(ImVec2(g_menuW, g_menuH), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        g_menuX = ImGui::GetWindowPos().x; g_menuY = ImGui::GetWindowPos().y;
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] 辅助已注入成功");
        ModernToggle((const char*)u8"对手棋盘透视", &g_esp_board, 1); 
        ModernToggle((const char*)u8"备战席透视", &g_esp_bench, 2);
        ModernToggle((const char*)u8"商店透视", &g_esp_shop, 3); 
        ModernToggle((const char*)u8"锁定窗口布局", &g_boardLocked, 4);
        if (ImGui::Button((const char*)u8"保存配置", ImVec2(-1, 40 * g_autoScale))) SaveConfig();
    }
    ImGui::End();
}

// =================================================================
// 4. 渲染主线程
// =================================================================
void MainRenderThread() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    LoadConfig(); 
    UpdateFontHD(true);  
    std::thread it([&] { while(g_game_running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
    
    while (g_game_running) {
        imgui.BeginFrame(); 
        glDisable(GL_SCISSOR_TEST); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        
        DrawBoard(); 
        DrawBench(); 
        DrawShop(); 
        DrawMenu();

        imgui.EndFrame(); 
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (it.joinable()) it.join(); 
    ImGui::DestroyContext();
}

// =================================================================
// 5. 底层 ptrace 注入逻辑
// =================================================================

long get_module_base_remote(pid_t pid, const char* module_name) {
    FILE *fp; long addr = 0; char filename[64], line[1024]; 
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r"); 
    if (fp) { 
        while (fgets(line, sizeof(line), fp)) { 
            if (strstr(line, module_name)) { 
                addr = strtoul(line, NULL, 16); 
                break; 
            } 
        } 
        fclose(fp); 
    } 
    return addr;
}

void* get_remote_func_addr(pid_t pid, const char* module_name, void* local_func) {
    long lb = get_module_base_remote(getpid(), module_name); 
    long rb = get_module_base_remote(pid, module_name);
    if (!lb || !rb) return NULL; 
    return (void *)((uintptr_t)local_func - lb + rb);
}

long ptrace_call_target(pid_t pid, uintptr_t func, long *params, int num) {
    struct user_pt_regs regs, saved; struct iovec iov = {&regs, sizeof(regs)};
    if (ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) < 0) return -1;
    memcpy(&saved, &regs, sizeof(regs)); 
    for (int i = 0; i < num && i < 8; i++) regs.regs[i] = params[i];
    regs.pc = func; regs.regs[30] = 0; 
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); 
    int status = 0; waitpid(pid, &status, WUNTRACED);
    struct user_pt_regs ret_regs; struct iovec ret_iov = {&ret_regs, sizeof(ret_regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &ret_iov); 
    long rv = ret_regs.regs[0];
    struct iovec siov = {&saved, sizeof(saved)}; 
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &siov); 
    return rv;
}

int perform_injection(pid_t pid, const char* drop_path) {
    LOGI("[*] 正在附加游戏进程...");
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; }

    char buf[256] = {0}; strncpy(buf, drop_path, 255);
    for (size_t i = 0; i < sizeof(buf); i += 8) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));

    void* r_dl = get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    long d_p[] = {(long)r_mem, RTLD_NOW}; 
    long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    return (h == 0) ? -1 : 0;
}

// 检查进程是否为活跃状态 (过滤僵尸进程)
bool is_process_active(pid_t pid) {
    char path[128]; snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char state;
    if (fscanf(f, "%*d %*s %c", &state) != 1) { fclose(f); return false; }
    fclose(f);
    return (state != 'Z' && state != 'X');
}

// =================================================================
// 6. 守护进程入口
// =================================================================
int main(int argc, char** argv) {
    LOGI("=============================================");
    LOGI("   JKHelper Daemon 精准监控模式已启动");
    LOGI("=============================================");

    // 释放 SO 载荷
    FILE* f = fopen(DROP_SO_PATH, "wb");
    if(f) {
        fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
        chmod(DROP_SO_PATH, 0777); 
        LOGI("[+] 注入载荷已释放: %s", DROP_SO_PATH);
    }

    while (true) {
        pid_t pid = 0;
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
                            pid_t found_pid = atoi(ptr->d_name);
                            if (is_process_active(found_pid)) { pid = found_pid; break; }
                        }
                    }
                }
            }
            closedir(dir);
        }

        if (pid > 0) {
            // 检查是否已经加载过 SO
            if (get_module_base_remote(pid, "libJKHook.so") != 0) {
                if (!g_game_running) {
                    LOGI("[*] 发现已注入的游戏进程 (%d)，恢复 UI 菜单...", pid);
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false;
                    if (render_thread.joinable()) render_thread.join();
                }
            } else {
                LOGI("[!] 捕捉到新游戏进程 (%d)，等待 libil2cpp 加载...", pid);
                int wait_limit = 0;
                while (get_module_base_remote(pid, "libil2cpp.so") == 0 && wait_limit < 45) {
                    std::this_thread::sleep_for(std::chrono::seconds(1)); wait_limit++;
                }
                if (wait_limit < 45 && perform_injection(pid, DROP_SO_PATH) == 0) {
                    LOGI("[+] 内部 Hook 注入成功，开启 UI 渲染。");
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false;
                    if (render_thread.joinable()) render_thread.join();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
