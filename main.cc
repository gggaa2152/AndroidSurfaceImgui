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

// 增强型日志：同时输出到 logcat 和 终端屏幕
#define LOG_TAG "JKHelper_Daemon"
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); printf("[INFO] " __VA_ARGS__); printf("\n"); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); printf("\033[31m[ERROR] " __VA_ARGS__); printf("\033[0m\n"); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
// 使用物理路径，避开 /data/data 可能存在的软链接失效问题
const char* DROP_SO_PATH = "/data/user/0/com.tencent.jkchess/cache/libJKHook.so";
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
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.55f, 1.0f), (const char*)u8"[+] 双端权限修复模式");
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
// 2. 核心注入引擎 (ptrace 64-bit)
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
    LOGI("[*] 正在附加到游戏主进程 %d...", pid);
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) {
        LOGE("[-] PTRACE_ATTACH 失败: %s", strerror(errno));
        return -1;
    }
    waitpid(pid, NULL, WUNTRACED);

    void* r_mmap = get_remote_func_addr(pid, "libc.so", (void*)mmap);
    // 在远程申请一块足够存放路径的内存 (1024 字节)
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    long r_mem = ptrace_call_target(pid, (uintptr_t)r_mmap, m_p, 6);
    if (r_mem <= 0 || r_mem == (long)-1) { 
        LOGE("[-] 远程 mmap 内存分配失败"); 
        ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; 
    }

    LOGI("[+] 远程内存空间已分配: 0x%lx", r_mem);
    char buf[512] = {0}; strncpy(buf, drop_path, 511);
    // 写入路径字符串
    for (size_t i = 0; i < sizeof(buf); i += 8) {
        ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));
    }

    // 定位 dlopen
    void* r_dl = get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = get_remote_func_addr(pid, "libc.so", (void*)dlopen);
    
    LOGI("[*] 执行远程 dlopen 指令...");
    long d_p[] = {(long)r_mem, RTLD_NOW}; 
    long h = ptrace_call_target(pid, (uintptr_t)r_dl, d_p, 2);
    
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    if (h == 0) {
        LOGE("[-] dlopen 注入失败！请检查文件是否存在: %s", drop_path);
        return -1;
    }
    LOGI("[+] 注入成功！句柄地址: 0x%lx", h);
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
    printf("   JKHelper 终极守护进程 - 注入追踪模式\n");
    printf("=============================================\033[0m\n");

    // 环境初始化：关闭 SELinux 拦截
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
                                // 关键：获取金铲铲进程的 UID
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
            // 准备文件环境
            FILE* f = fopen(DROP_SO_PATH, "wb");
            if(f) {
                fwrite(libJKHook_so, 1, libJKHook_so_len, f); 
                fclose(f);
                // 强制赋予 777 权限 (所有人可读可执行)
                chmod(DROP_SO_PATH, 0777); 
                // 极其关键：将 SO 的所有权从 Root 转移给游戏用户 UID
                if (game_uid > 0) {
                    chown(DROP_SO_PATH, game_uid, game_uid);
                    LOGI("[*] 已移交 SO 所有权给游戏 UID: %d", game_uid);
                }
                // 修复 SELinux 安全上下文，让系统链接器放行
                system("chcon u:object_r:apk_data_file:s0 /data/user/0/com.tencent.jkchess/cache/libJKHook.so > /dev/null 2>&1");
            }

            // 检查是否已注入
            if (get_module_base_remote(pid, "libJKHook.so") != 0) {
                if (!g_game_running) {
                    LOGI("[*] 检测到已注入进程 (%d)，正在拉起悬浮窗...", pid);
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            } else {
                LOGI("[!] 发现金铲铲运行中 (%d)，正在等待 Unity 初始化...", pid);
                int wait_limit = 0;
                // 等待游戏主逻辑 libil2cpp 加载，通常需要几秒钟
                while (get_module_base_remote(pid, "libil2cpp.so") == 0 && wait_limit < 60) {
                    std::this_thread::sleep_for(std::chrono::seconds(1)); wait_limit++;
                }
                
                if (wait_limit < 60 && perform_injection(pid, DROP_SO_PATH) == 0) {
                    g_game_running = true;
                    std::thread render_thread(MainRenderThread);
                    while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(2));
                    g_game_running = false; if (render_thread.joinable()) render_thread.join();
                }
            }
        }
        // 降低循环频率，减少 CPU 占用
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
