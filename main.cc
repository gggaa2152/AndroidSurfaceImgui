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
#define LOGI(...) { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); printf("[INFO] " __VA_ARGS__); printf("\n"); }
#define LOGE(...) { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); printf("\033[31m[ERROR] " __VA_ARGS__); printf("\033[0m\n"); }

const char* TARGET_PACKAGE = "com.tencent.jkchess";
const char* DROP_SO_PATH = "/data/data/com.tencent.jkchess/cache/libJKHook.so";
const char* g_configPath = "/data/jkchess_config.ini"; 

std::atomic<bool> g_game_running(false);

// =================================================================
// 1. 全局配置与状态
// =================================================================
bool g_esp_board = true;
bool g_boardLocked = false; 
float g_scale = 1.0f;            
float g_autoScale = 1.0f;        
float g_current_rendered_size = 0.0f; 
float g_startX = 400.0f, g_startY = 400.0f;    
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
// 3. UI 核心与字体修复 (精简集模式)
// =================================================================

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenY = io.DisplaySize.y > 100.0f ? io.DisplaySize.y : 1080.0f;
    float targetSize = std::clamp(22.0f * (screenY / 1080.0f), 18.0f, 45.0f);
    
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f && io.Fonts->IsBuilt()) return;

    LOGI("[*] 正在执行字体精简构建 (Size: %.1f)...", targetSize);
    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear(); 

    ImFontConfig config;
    config.OversampleH = 1; config.OversampleV = 1; config.PixelSnapH = true;

    // 【核心修复】：手动定义菜单用到的中文字符，极大地减小纹理压力
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // 基础 ASCII
        0x4E00, 0x9FA5, // 常用汉字范围 (如果这里依然 Build 失败，我们会进一步缩小)
        0,
    };

    const char* fontPaths[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/product/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf"
    };

    bool loaded = false;
    for (const char* path : fontPaths) {
        if (access(path, R_OK) == 0) {
            // 尝试加载，但限制字符范围
            if (io.Fonts->AddFontFromFileTTF(path, targetSize, &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon())) {
                loaded = true; break;
            }
        }
    }

    if (!loaded) io.Fonts->AddFontDefault(&config);

    // 强制设置一个较小的纹理页，增加成功率
    io.Fonts->TexDesiredWidth = 1024;

    if (!io.Fonts->Build()) {
        LOGE("[-] 常用集构建失败，尝试极简模式...");
        io.Fonts->Clear();
        io.Fonts->AddFontDefault(); // 纯英文回退
        io.Fonts->Build();
    }

    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
    LOGI("[+] 字体构建成功。");
}

void DrawBoard() {
    if (!g_esp_board) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    float baseSz = 40.0f * 2.2f * (ImGui::GetIO().DisplaySize.y / 1080.0f); 
    for(int r = 0; r < 4; r++) { for(int c = 0; c < 7; c++) { 
        float cx = g_startX + (c * baseSz * 1.732f) + (r % 2 == 1 ? baseSz * 0.866f : 0);
        float cy = g_startY + (r * baseSz * 1.5f);
        if(g_enemyBoard[r][c]) d->AddCircleFilled(ImVec2(cx, cy), baseSz * 0.5f, IM_COL32(255, 0, 0, 180));
        d->AddCircle(ImVec2(cx, cy), baseSz * 0.5f, IM_COL32(255, 255, 255, 100));
    } }
}

void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(400 * (ImGui::GetIO().DisplaySize.y / 1080.0f), 0));
    if (ImGui::Begin((const char*)u8"金铲铲助手", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), (const char*)u8"状态: 注入成功");
        ImGui::Separator();
        ImGui::Checkbox((const char*)u8"显示敌方棋盘", &g_esp_board);
        ImGui::Checkbox((const char*)u8"锁定窗口", &g_boardLocked);
        if (!g_boardLocked) {
            ImGui::SliderFloat((const char*)u8"棋盘 X", &g_startX, 0, 2000);
            ImGui::SliderFloat((const char*)u8"棋盘 Y", &g_startY, 0, 2000);
        }
    }
    ImGui::End();
}

void MainRenderThread() {
    ImGui::CreateContext();
    {
        android::AImGui imgui({.renderType = android::AImGui::RenderType::RenderNative}); 
        UpdateFontHD(true);  
        std::thread it([&] { while(g_game_running) { imgui.ProcessInputEvent(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });

        while (g_game_running) {
            if (!ImGui::GetIO().Fonts->IsBuilt()) { UpdateFontHD(true); if (!ImGui::GetIO().Fonts->IsBuilt()) continue; }
            imgui.BeginFrame(); 
            glDisable(GL_SCISSOR_TEST); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
            DrawBoard(); DrawMenu();
            imgui.EndFrame(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        if (it.joinable()) it.join();
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    LOGI("[+] 渲染资源已回收。");
}

// =================================================================
// 4. 注入逻辑
// =================================================================
int perform_injection(pid_t pid, const char* drop_path) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) return -1;
    waitpid(pid, NULL, WUNTRACED);
    void* r_mmap = (void*)get_remote_func_addr(pid, "libc.so", (void*)mmap);
    long m_p[] = {0, 1024, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0};
    struct user_pt_regs regs; struct iovec iov = {&regs, sizeof(regs)};
    ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    struct user_pt_regs saved = regs;
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
    regs.regs[0] = r_mem; regs.regs[1] = RTLD_NOW; regs.pc = (uintptr_t)r_dl; regs.regs[30] = 0;
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
    ptrace(PTRACE_CONT, pid, NULL, 0); waitpid(pid, NULL, WUNTRACED);
    ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, (void*)&saved);
    ptrace(PTRACE_DETACH, pid, NULL, 0); 
    return 0;
}

int main(int argc, char** argv) {
    LOGI("JKHelper Daemon Started.");
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
            } else if (!g_game_running) {
                g_game_running = true; std::thread(MainRenderThread).detach();
            }
            while (kill(pid, 0) == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
            g_game_running = false; 
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return 0;
}
