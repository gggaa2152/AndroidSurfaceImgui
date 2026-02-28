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

int g_enemyBoard[4][7] = {
    {1, 0, 0, 0, 1, 0, 0}, 
    {0, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0}, 
    {1, 0, 1, 0, 1, 0, 1}
};

// =================================================================
// 2. 注入工具函数 (必须放在调用者上方)
// =================================================================

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
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    struct user_pt_regs ret_regs; struct iovec ret_iov = {&ret_regs, sizeof(ret_regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &ret_iov); 
    long rv = ret_regs.regs[0];
    struct iovec siov = {&saved, sizeof(saved)}; ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &siov); 
    return rv;
}

void print_remote_dlerror(pid_t pid) {
    void* r_dlerror = get_remote_func_addr(pid, "libdl.so", (void*)dlerror);
    if (!r_dlerror) r_dlerror = get_remote_func_addr(pid, "libc.so", (void*)dlerror);
    if (!r_dlerror) return;
    long err_ptr = ptrace_call_target(pid, (uintptr_t)r_dlerror, nullptr, 0);
    if (err_ptr == 0) return;
    char buf[256] = {0};
    for (int i = 0; i < 256 / 8; i++) {
        long data = ptrace(PTRACE_PEEKTEXT, pid, (void*)(err_ptr + i * 8), NULL);
        memcpy(buf + i * 8, &data, 8);
    }
    LOGE("[诊断信息] 游戏内部报错: %s", buf);
}

int perform_injection(pid_t pid, const char* drop_path) {
    LOGI("[*] 正在执行 Ptrace 注入流程...");
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = (void*)get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; }

    char buf[512] = {0}; strncpy(buf, drop_path, 511);
    for (size_t i = 0; i < sizeof(buf); i += 8) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));

    void* r_dl = (void*)get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = (void*)get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    
    long d_p[] = {(long)r_mem, RTLD_NOW}; 
    long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    
    if (h == 0) {
        print_remote_dlerror(pid);
        ptrace(PTRACE_DETACH, pid, NULL, 0);
        return -1;
    }
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    LOGI("[+] 注入成功！句柄: 0x%lx", h);
    return 0;
}

// =================================================================
// 3. UI 辅助函数
// =================================================================

void SaveConfig() {
    std::ofstream out(g_configPath);
    if (out.is_open()) {
        out << "espBoard=" << g_esp_board << "\n";
        out << "boardLocked=" << g_boardLocked << "\n";
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
        }
        in.close();
    }
}

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float targetSize = std::clamp(20.0f * (io.DisplaySize.y / 1080.0f), 15.0f, 50.0f);
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f) return;
    ImGui_ImplOpenGL3_DestroyFontsTexture(); io.Fonts->Clear(); 
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) io.Fonts->AddFontFromFileTTF(fontPath, targetSize, NULL, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    else io.Fonts->AddFontDefault();
    io.Fonts->Build(); ImGui_ImplOpenGL3_CreateFontsTexture(); g_current_rendered_size = targetSize;
}

void HandleGridInteraction(float& out_x, float& out_y, float& out_scale, float& t_x, float& t_y, float& t_scale, bool& isDragging, bool& isScaling, ImVec2& dragOffset, ImVec2& scaleDragOffset, float h_dx, float h_dy, bool locked) {
    ImGuiIO& io = ImGui::GetIO();
    if (!locked) {
        ImVec2 p_scale(out_x + h_dx * out_scale, out_y + h_dy * out_scale);
        if (!ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0)) {
            if (ImLengthSqr(io.MousePos - p_scale) < 5000.0f) { isScaling = true; scaleDragOffset = io.MousePos - p_scale; }
            else { isDragging = true; dragOffset = ImVec2(t_x - io.MousePos.x, t_y - io.MousePos.y); }
        }
        if (isScaling && ImGui::IsMouseDown(0)) { 
            // 修复：使用 sqrtf + ImLengthSqr 替代未定义的 ImLength
            ImVec2 delta = io.MousePos - ImVec2(t_x, t_y);
            t_scale = std::clamp(sqrtf(ImLengthSqr(delta)) / h_dx, 0.5f, 5.0f); 
        }
        else isScaling = false;
        if (isDragging && !isScaling && ImGui::IsMouseDown(0)) { t_x = io.MousePos.x + dragOffset.x; t_y = io.MousePos.y + dragOffset.y; }
        else isDragging = false;
    }
    float s = 1.0f - expf(-20.0f * io.DeltaTime); out_x = ImLerp(out_x, t_x, s); out_y = ImLerp(out_y, t_y, s); out_scale = ImLerp(out_scale, t_scale, s);
}

void DrawBoard() {
    if (!g_esp_board) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    static float t_x = g_startX, t_y = g_startY, t_scale = g_boardManualScale;
    static bool isDragging = false, isScaling = false; static ImVec2 dragOffset, scaleDragOffset;
    float baseSz = 40.0f * g_boardScale * g_autoScale; 
    HandleGridInteraction(g_startX, g_startY, g_boardManualScale, t_x, t_y, t_scale, isDragging, isScaling, dragOffset, scaleDragOffset, 400.0f, 200.0f, g_boardLocked);
    for(int r = 0; r < 4; r++) { for(int c = 0; c < 7; c++) { 
        float cx = g_startX + c * baseSz * 1.732f * g_boardManualScale + (r % 2 == 1 ? baseSz * 0.866f * g_boardManualScale : 0);
        float cy = g_startY + r * baseSz * 1.5f * g_boardManualScale;
        if(g_enemyBoard[r][c]) d->AddCircleFilled(ImVec2(cx, cy), baseSz * 0.6f * g_boardManualScale, IM_COL32(255, 0, 0, 150));
    } }
}

void DrawMenu() {
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetWindowFontScale(g_scale);
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] Dobby 静态注入成功");
        ImGui::Checkbox((const char*)u8"敌方棋盘显示", &g_esp_board);
        ImGui::Checkbox((const char*)u8"锁定位置", &g_boardLocked);
        if (ImGui::Button((const char*)u8"保存配置")) SaveConfig();
    }
    ImGui::End();
}

// =================================================================
// 4. 线程控制与入口
// =================================================================

void MainRenderThread() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    LoadConfig(); UpdateFontHD(true);  
    std::thread it([&] { while(g_game_running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
    while (g_game_running) {
        imgui.BeginFrame(); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        DrawBoard(); DrawMenu();
        imgui.EndFrame(); std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (it.joinable()) it.join(); ImGui::DestroyContext();
}

bool is_process_active(pid_t pid) {
    char path[128]; snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r"); if (!f) return false;
    char state; fscanf(f, "%*d %*s %c", &state); fclose(f);
    return (state != 'Z' && state != 'X');
}

int main(int argc, char** argv) {
    LOGI("=============================================");
    LOGI("   JKHelper Daemon 稳定版启动成功");
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
            if (get_module_base_remote(pid, "libJKHook.so") == 0) {
                LOGI("[!] 发现游戏 (%d)，正在注入...", pid);
                while (get_module_base_remote(pid, "libil2cpp.so") == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
                if (perform_injection(pid, DROP_SO_PATH) == 0) {
                    g_game_running = true; std::thread(MainRenderThread).detach();
                }
            }
            while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
            g_game_running = false; LOGI("[*] 游戏退出。");
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
