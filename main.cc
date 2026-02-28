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

#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); printf(__VA_ARGS__); printf("\n"); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
// 修改路径到 App 内部目录，这是 dlopen 成功的关键
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态 (保持你原有的逻辑)
// =================================================================
bool g_esp_board = true;
bool g_boardLocked = false; 
float g_autoScale = 1.0f;
float g_current_rendered_size = 0.0f; 
float g_startX = 400.0f, g_startY = 400.0f;
float g_menuX = 100.0f, g_menuY = 100.0f, g_menuW = 350.0f, g_menuH = 550.0f;

void DrawMenu() {
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] 双端注入模式已激活");
        ImGui::Checkbox((const char*)u8"显示棋盘透视", &g_esp_board);
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
// 4. 核心注入引擎
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

int perform_injection(pid_t pid, const char* drop_path) {
    LOGI("[*] 准备 ptrace 附加到进程 %d...", pid);
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) {
        LOGE("[-] 附加失败: %s", strerror(errno));
        return -1;
    }
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { 
        LOGE("[-] 远程 mmap 失败"); 
        ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; 
    }

    LOGI("[+] 远程内存就绪: 0x%lx", r_mem);
    char buf[256] = {0}; strncpy(buf, drop_path, 255);
    for (size_t i = 0; i < sizeof(buf); i += 8) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));

    // 适配 Android 10+ 的 dlopen 寻找逻辑
    void* r_dl = get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    
    LOGI("[*] 正在执行远程载入命令...");
    long d_p[] = {(long)r_mem, RTLD_NOW}; 
    long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    if (h == 0) {
        LOGE("[-] dlopen 注入失败。检查路径: %s", drop_path);
        return -1;
    }
    LOGI("[+] 恭喜！内部 Hook 已成功注进游戏，句柄: 0x%lx", h);
    return 0;
}

bool is_process_active(pid_t pid) {
    char path[128]; snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r"); if (!f) return false;
    char state; fscanf(f, "%*d %*s %c", &state); fclose(f);
    return (state != 'Z' && state != 'X');
}

// =================================================================
// 5. 守护入口
// =================================================================
int main(int argc, char** argv) {
    printf("\n=============================================\n");
    printf("   JKHelper 终极守护进程 - 注入追踪模式\n");
    printf("=============================================\n");

    // 全局权限环境初始化
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
                            pid_t found_pid = atoi(ptr->d_name);
                            if (is_process_active(found_pid)) {
                                pid = found_pid;
                                // 关键：获取游戏进程的 UID，用于后续修改 SO 权限
                                struct stat st;
                                snprintf(path, 256, "/proc/%d", pid);
                                if (stat(path, &st) == 0) game_uid = st.st_uid;
                                break;
                            }
                        }
                    }
                }
            }
            closedir(dir);
        }

        if (pid > 0) {
            // 每次检测到进程都释放一次 SO，确保路径存在
            FILE* f = fopen(DROP_SO_PATH, "wb");
            if(f) {
                fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
                chmod(DROP_SO_PATH, 0777); 
                // 关键：将 SO 的拥有者改为游戏进程的 UID
                if (game_uid > 0) chown(DROP_SO_PATH, game_uid, game_uid);
                system("chcon u:object_r:apk_data_file:s0 /data/data/com.tencent.jkchess/cache/libJKHook.so > /dev/null 2>&1");
            }

            if (get_module_base_remote(pid, "libJKHook.so") != 0) {
                if (!g_game_running) {
                    LOGI("[*] 游戏已在运行且已注入，拉起菜单...");
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            } else {
                LOGI("[!] 发现金铲铲进程 (%d)，等待引擎初始化...", pid);
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
