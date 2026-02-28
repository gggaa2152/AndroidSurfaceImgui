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

// 由 build.yml 自动生成的头文件
#include "hook_payload.h"

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

// 增强型日志
#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); printf("[INFO] " __VA_ARGS__); printf("\n"); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); printf("\033[31m[ERROR] " __VA_ARGS__); printf("\033[0m\n"); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
// 尝试切回 tmp 目录，但在 main 中我们会做更强力的赋权
const char* DROP_SO_PATH = "/data/local/tmp/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态 (保持原样)
// =================================================================
bool g_esp_board = true;
bool g_boardLocked = false; 
float g_autoScale = 1.0f;
float g_startX = 400.0f, g_startY = 400.0f;

void DrawMenu() {
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] 正在诊断注入状态...");
        ImGui::Checkbox((const char*)u8"开启棋盘透视", &g_esp_board);
    }
    ImGui::End();
}

void MainRenderThread() {
    ImGui::CreateContext();
    android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
    while (g_game_running) {
        imgui.BeginFrame();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT);
        DrawMenu();
        imgui.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    ImGui::DestroyContext();
}

// =================================================================
// 2. 核心注入引擎 (增加远程错误抓取)
// =================================================================

long get_module_base_remote(pid_t pid, const char* module_name) {
    FILE *fp; long addr = 0; char filename[64], line[1024]; 
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r"); 
    if (fp) { 
        while (fgets(line, sizeof(line), fp)) { 
            if (strstr(line, module_name)) { addr = strtoul(line, NULL, 16); break; } 
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
    struct iovec siov = {&saved, sizeof(saved)}; ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &siov); 
    return rv;
}

// 精准抓取远程进程的 dlerror 消息
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
    LOGI("[*] 正在附加到进程 %d...", pid);
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; }

    char buf[512] = {0}; strncpy(buf, drop_path, 511);
    for (size_t i = 0; i < sizeof(buf); i += 8) {
        ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));
    }

    void* r_dl = get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    
    long d_p[] = {(long)r_mem, RTLD_NOW}; 
    long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    
    if (h == 0) {
        print_remote_dlerror(pid); // 抓取真正的报错原因
        ptrace(PTRACE_DETACH, pid, NULL, 0);
        return -1;
    }
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    LOGI("[+] 注入成功！句柄: 0x%lx", h);
    return 0;
}

bool is_process_active(pid_t pid) {
    char path[128]; snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r"); if (!f) return false;
    char state; fscanf(f, "%*d %*s %c", &state); fclose(f);
    return (state != 'Z' && state != 'X');
}

// =================================================================
// 3. 守护入口
// =================================================================
int main(int argc, char** argv) {
    printf("\n\033[32m=============================================\n");
    printf("   JKHelper 终极守护进程 - 远程诊断模式\n");
    printf("=============================================\033[0m\n");

    system("setenforce 0 > /dev/null 2>&1");

    while (true) {
        pid_t pid = 0;
        uid_t game_uid = 0;
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
                            if (is_process_active(pid)) {
                                struct stat st;
                                snprintf(path, 256, "/proc/%d", pid);
                                if (stat(path, &st) == 0) game_uid = st.st_uid;
                                break; 
                            } else pid = 0;
                        }
                    }
                }
            }
            closedir(dir);
        }

        if (pid > 0) {
            // 部署 SO 并极限赋权
            FILE* f = fopen(DROP_SO_PATH, "wb");
            if(f) {
                fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
                chmod(DROP_SO_PATH, 0777); 
                if (game_uid > 0) chown(DROP_SO_PATH, game_uid, game_uid);
                // 针对不同系统尝试不同的标签修复
                system("chcon u:object_r:apk_data_file:s0 /data/local/tmp/libJKHook.so > /dev/null 2>&1");
                system("chcon u:object_r:system_file:s0 /data/local/tmp/libJKHook.so > /dev/null 2>&1");
            }

            if (get_module_base_remote(pid, "libJKHook.so") != 0) {
                if (!g_game_running) {
                    LOGI("[*] 已在运行中，拉起菜单...");
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            } else {
                LOGI("[!] 发现金铲铲 (%d)，等待初始化...", pid);
                int wait_limit = 0;
                while (get_module_base_remote(pid, "libil2cpp.so") == 0 && wait_limit < 45) {
                    std::this_thread::sleep_for(std::chrono::seconds(1)); wait_limit++;
                }
                
                if (wait_limit < 45 && perform_injection(pid, DROP_SO_PATH) == 0) {
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
