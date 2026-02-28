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

// 增强终端输出宏：强制刷新缓冲区，确保实时看到
#define LOG_TAG "JKHelper_Daemon"
#define LOG_STEP(step, desc) { printf("\033[33m[Step %d/10] %s\033[0m\n", step, desc); fflush(stdout); }
#define LOGI(...) { printf("[INFO] " __VA_ARGS__); printf("\n"); fflush(stdout); }
#define LOGE(...) { printf("\033[31m[ERROR] " __VA_ARGS__); printf("\033[0m\n"); fflush(stdout); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置
// =================================================================
bool g_esp_board = true;
bool g_boardLocked = false; 
float g_startX = 450.0f, g_startY = 400.0f;    
int g_enemyBoard[4][7] = {{1,0,0,0,1,0,0},{0,1,0,1,0,0,0},{0,0,0,0,0,1,0},{1,0,1,0,1,0,1}};

// =================================================================
// 2. 注入工具
// =================================================================
long get_module_base_remote(pid_t pid, const char* module_name) {
    FILE *fp; long addr = 0; char filename[64], line[1024]; 
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r"); 
    if (fp) { while (fgets(line, sizeof(line), fp)) { if (strstr(line, module_name)) { addr = strtoul(line, NULL, 16); break; } } fclose(fp); } 
    return addr;
}

void* get_remote_func_addr(pid_t pid, const char* module_name, void* local_func_addr) {
    long lb = get_module_base_remote(getpid(), module_name); 
    long rb = get_module_base_remote(pid, module_name);
    if (!lb || !rb) return NULL; 
    return (void *)((uintptr_t)local_func_addr - lb + rb);
}

// =================================================================
// 3. UI 核心 (修复字体加载逻辑)
// =================================================================

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (!force && io.Fonts->IsBuilt()) return;

    LOG_STEP(7, "正在构建 UI 字体图集...");
    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear(); 

    ImFontConfig config;
    config.OversampleH = 1; config.OversampleV = 1; config.PixelSnapH = true;

    // 首先加载默认字体，确保 Build() 永远不会失败
    io.Fonts->AddFontDefault(&config);

    // 针对你的“常用集构建失败”问题，我们改用手动指定极小字符范围
    const char* fontPath = "/system/fonts/NotoSansCJK-Regular.ttc";
    if (access(fontPath, R_OK) == 0) {
        LOGI("  -> 检测到系统字体，执行极速子集化加载...");
        // 只加载最核心的几个字，防止显存爆炸
        static const ImWchar mini_range[] = {
            0x0020, 0x00FF, // ASCII
            0x4E00, 0x9FA5, // 汉字常用区
            0,
        };
        // 限制尺寸到 20.0f
        io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &config, mini_range);
    }

    io.Fonts->TexDesiredWidth = 512; 
    if (!io.Fonts->Build()) {
        LOGE("  -> 字体图集生成失败，强制进入英文模式。");
        io.Fonts->Clear(); io.Fonts->AddFontDefault(); io.Fonts->Build();
    }

    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_CreateFontsTexture();
    LOGI("[+] 字体构建流程结束。");
}

void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), (const char*)u8"状态: 注入成功 (稳定模式)");
        ImGui::Separator();
        ImGui::Checkbox((const char*)u8"显示敌方棋盘", &g_esp_board);
    }
    ImGui::End();
}

void MainRenderThread() {
    LOG_STEP(6, "UI 渲染线程已拉起。");
    ImGui::CreateContext();
    {
        android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
        UpdateFontHD(true);  
        
        std::thread it([&] { 
            while(g_game_running) { 
                imgui.ProcessInputEvent(); 
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            } 
        });

        LOG_STEP(8, "进入 UI 绘制主循环。");
        while (g_game_running) {
            if (!ImGui::GetIO().Fonts->IsBuilt()) UpdateFontHD(true);
            imgui.BeginFrame(); 
            glDisable(GL_SCISSOR_TEST); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
            
            if (g_esp_board) {
                ImDrawList* d = ImGui::GetForegroundDrawList();
                float b = 38.0f * (ImGui::GetIO().DisplaySize.y / 1080.0f);
                for(int r=0; r<4; r++) for(int c=0; c<7; c++) {
                    float cx = g_startX + (c*b*1.732f) + (r%2==1?b*0.866f:0);
                    float cy = g_startY + (r*b*1.5f);
                    if(g_enemyBoard[r][c]) d->AddCircleFilled(ImVec2(cx, cy), b*0.55f, IM_COL32(255, 0, 0, 160));
                    d->AddCircle(ImVec2(cx, cy), b*0.55f, IM_COL32(255, 255, 255, 100));
                }
            }
            DrawMenu();
            imgui.EndFrame(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        if (it.joinable()) it.join();
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    LOGI("[*] 渲染线程资源已释放。");
}

// =================================================================
// 4. 强力监测逻辑 (修复瞬间退出的关键)
// =================================================================

bool is_process_really_alive(pid_t pid) {
    if (pid <= 0) return false;
    
    // 关键修复：只有连续检查 5 次失败才认为真的关了 (容错 5 秒)
    static int fail_count = 0;
    
    bool alive = (kill(pid, 0) == 0);
    if (!alive) {
        char path[128];
        snprintf(path, 128, "/proc/%d/cmdline", pid);
        alive = (access(path, F_OK) == 0);
    }

    if (alive) {
        fail_count = 0;
        return true;
    } else {
        fail_count++;
        if (fail_count >= 5) {
            fail_count = 0;
            return false;
        }
        return true; // 虽然本次失败，但还没到阈值，先认为活着
    }
}

int perform_injection(pid_t pid, const char* drop_path) {
    LOG_STEP(3, "开始 ptrace 附加...");
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);

    LOG_STEP(4, "执行远程内存分配...");
    void* r_mmap = (void*)get_remote_func_addr(pid, "libc.so", (void*)mmap);
    void* r_dl = (void*)get_remote_func_addr(pid, "libdl.so", (void*)dlopen);
    if (!r_dl) r_dl = (void*)get_remote_func_addr(pid, "libc.so", (void*)dlopen);

    struct user_pt_regs regs; struct iovec iov = {&regs, sizeof(regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    struct user_pt_regs saved = regs;

    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    for (int i = 0; i < 6; i++) regs.regs[i] = m_p[i];
    regs.pc = (uintptr_t)r_mmap; regs.regs[30] = 0;
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    long r_mem = regs.regs[0];
    if (r_mem <= 0 || r_mem == (long)-1) { ptrace(PTRACE_DETACH, pid, NULL, 0); return -1; }

    LOG_STEP(5, "写入路径并执行 dlopen...");
    char buf[256] = {0}; strncpy(buf, drop_path, 255);
    for (size_t i = 0; i < sizeof(buf); i += 8) ptrace(PTRACE_POKETEXT, pid, (void*)(r_mem + i), *(long*)(buf + i));

    regs.regs[0] = r_mem; regs.regs[1] = RTLD_NOW;
    regs.pc = (uintptr_t)r_dl; regs.regs[30] = 0;
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    long h = regs.regs[0];
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, (void*)&saved);
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    
    if (h == 0) return -1;
    LOGI("[+] 注入成功，载荷句柄: 0x%lx", h);
    return 0;
}

int main(int argc, char** argv) {
    printf("\n\033[32m=============================================\n");
    printf("   JKHelper 终极守护进程 - 加载顺序优化版\n");
    printf("=============================================\033[0m\n");

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

        if (pid > 0 && kill(pid, 0) == 0) {
            LOG_STEP(2, "捕获到游戏，准备载入...");
            FILE* f = fopen(DROP_SO_PATH, "wb");
            if(f) {
                fwrite(libJKHook_so, 1, libJKHook_so_len, f); fclose(f);
                chmod(DROP_SO_PATH, 0777); 
                if (game_uid > 0) chown(DROP_SO_PATH, game_uid, game_uid);
                system("chcon u:object_r:apk_data_file:s0 /data/data/com.tencent.jkchess/cache/libJKHook.so > /dev/null 2>&1");
            }
            
            if (get_module_base_remote(pid, "libJKHook.so") == 0) {
                LOGI("[*] 正在等待 libil2cpp 初始化...");
                while (get_module_base_remote(pid, "libil2cpp.so") == 0) usleep(500000);
                
                if (perform_injection(pid, DROP_SO_PATH) == 0) {
                    LOG_STEP(9, "注入完成，启动 UI 链路...");
                    g_game_running = true; 
                    std::thread(MainRenderThread).detach();
                    
                    // 【关键修复点】：注入后强制睡眠 3 秒！
                    // 这 3 秒内主循环不去碰进程，避开 Detach 后的内核抖动期
                    LOGI("[*] 正在进行 3s 稳定性缓冲，请勿操作...");
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }
            } else if (!g_game_running) {
                LOGI("[*] 环境已就绪，恢复 UI 界面...");
                g_game_running = true;
                std::thread(MainRenderThread).detach();
            }

            LOG_STEP(10, "已进入稳定监测循环。");
            while (is_process_really_alive(pid)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            
            LOG_STEP(0, "确认游戏已退出，释放资源...");
            g_game_running = false; 
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
