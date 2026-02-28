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
// 2. 注入与内存工具
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
// 3. UI 核心逻辑 (修复：自定义精简字符集)
// =================================================================

void UpdateFontHD(bool force = false) {
    ImGuiIO& io = ImGui::GetIO();
    float screenY = io.DisplaySize.y > 100.0f ? io.DisplaySize.y : 1080.0f;
    float targetSize = std::clamp(22.0f * (screenY / 1080.0f), 18.0f, 45.0f);
    
    if (!force && std::abs(targetSize - g_current_rendered_size) < 0.5f && io.Fonts->IsBuilt()) return;

    LOGI("[*] 正在执行极简字体构建 (Size: %.1f)...", targetSize);
    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear(); 

    ImFontConfig config;
    config.OversampleH = 1; config.OversampleV = 1; config.PixelSnapH = true;

    // 【核心修复】：手动指定菜单必须用到的字符，避免加载整个 2 万字的 CJK 库
    // 字符包含：状态注入成功显示敌方棋盘锁定窗口保存配置 X Y
    static const ImWchar custom_ranges[] = {
        0x0020, 0x00FF, // 基础 ASCII (英文/数字/标点)
        0x4E00, 0x4E00, // 一
        0x4E3B, 0x4E3B, // 主
        0x4E50, 0x4E50, // 乐
        0x4FDD, 0x4FDD, // 保
        0x4FE1, 0x4FE1, // 信
        0x5112, 0x5112, // 儒
        0x5145, 0x5145, // 充
        0x51B0, 0x51B0, // 冰
        0x5206, 0x5206, // 分
        0x52A8, 0x52A8, // 动
        0x5305, 0x5305, // 包
        0x533A, 0x533A, // 区
        0x53E3, 0x53E3, // 口
        0x540D, 0x540D, // 名
        0x542F, 0x542F, // 启
        0x5668, 0x5668, // 器
        0x56E0, 0x56E0, // 因
        0x5723, 0x5723, // 圣
        0x5728, 0x5728, // 在
        0x573A, 0x573A, // 场
        0x5904, 0x5904, // 处
        0x5907, 0x5907, // 备
        0x5916, 0x5916, // 外
        0x5934, 0x5934, // 头
        0x5956, 0x5956, // 奖
        0x5B58, 0x5B58, // 存
        0x5B9A, 0x5B9A, // 定
        0x5DDE, 0x5DDE, // 州
        0x5DE5, 0x5DE5, // 工
        0x5DF2, 0x5DF2, // 已
        0x5E2E, 0x5E2E, // 帮
        0x5E73, 0x5E73, // 平
        0x5E97, 0x5E97, // 店
        0x5F00, 0x5F00, // 开
        0x5F15, 0x5F15, // 引
        0x6001, 0x6001, // 态
        0x6210, 0x6210, // 成
        0x6218, 0x6218, // 战
        0x624B, 0x624B, // 手
        0x6253, 0x6253, // 打
        0x626B, 0x626B, // 扫
        0x6301, 0x6301, // 持
        0x63A7, 0x63A7, // 控
        0x64AD, 0x64AD, // 播
        0x653E, 0x653E, // 放
        0x6548, 0x6548, // 效
        0x6570, 0x6570, // 数
        0x65B0, 0x65B0, // 新
        0x663E, 0x663E, // 显
        0x66F4, 0x66F4, // 更
        0x670D, 0x670D, // 服
        0x67E5, 0x67E5, // 查
        0x683C, 0x683C, // 格
        0x68CB, 0x68CB, // 棋
        0x6B63, 0x6B63, // 正
        0x6BD4, 0x6BD4, // 比
        0x6D41, 0x6D41, // 流
        0x6D4B, 0x6D4B, // 测
        0x6E38, 0x6E38, // 游
        0x706F, 0x706F, // 灯
        0x72B6, 0x72B6, // 状
        0x73A9, 0x73A9, // 玩
        0x73B0, 0x73B0, // 现
        0x76D8, 0x76D8, // 盘
        0x770B, 0x770B, // 看
        0x79FB, 0x79FB, // 移
        0x7A0B, 0x7A0B, // 程
        0x7A7A, 0x7A7A, // 空
        0x7B2C, 0x7B2C, // 第
        0x7B49, 0x7B49, // 等
        0x7EA7, 0x7EA7, // 级
        0x7F62, 0x7F62, // 罢
        0x8054, 0x8054, // 联
        0x81EA, 0x81EA, // 自
        0x83DC, 0x83DC, // 菜
        0x8424, 0x8424, // 萤
        0x843D, 0x843D, // 落
        0x84DD, 0x84DD, // 蓝
        0x88AB, 0x88AB, // 被
        0x88C5, 0x88C5, // 装
        0x89D2, 0x89D2, // 角
        0x89E3, 0x89E3, // 解
        0x8BBE, 0x8BBE, // 设
        0x8BD5, 0x8BD5, // 试
        0x8BEF, 0x8BEF, // 误
        0x8BFB, 0x8BFB, // 读
        0x8C03, 0x8C03, // 调
        0x8D2D, 0x8D2D, // 购
        0x8D44, 0x8D44, // 资
        0x8D62, 0x8D62, // 赢
        0x8DEF, 0x8DEF, // 路
        0x8FDE, 0x8FDE, // 连
        0x9000, 0x9000, // 退
        0x901A, 0x901A, // 通
        0x9053, 0x9053, // 道
        0x91D1, 0x91D1, // 金
        0x94F2, 0x94F2, // 铲
        0x9501, 0x9501, // 锁
        0x961F, 0x961F, // 队
        0x9632, 0x9632, // 防
        0x9644, 0x9644, // 附
        0x9690, 0x9690, // 隐
        0x9759, 0x9759, // 静
        0x9875, 0x9875, // 页
        0x98DF, 0x98DF, // 食
        0x9996, 0x9996, // 首
        0x9A71, 0x9A71, // 驱
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
            // 使用自定义范围加载
            if (io.Fonts->AddFontFromFileTTF(path, targetSize, &config, custom_ranges)) {
                loaded = true; break;
            }
        }
    }

    if (!loaded) io.Fonts->AddFontDefault(&config);

    // 限制纹理宽度为 1024，在绝大多数手机上都能 Build 成功
    io.Fonts->TexDesiredWidth = 1024;

    if (!io.Fonts->Build()) {
        LOGE("[-] 字体库构建失败。");
        io.Fonts->Clear();
        io.Fonts->AddFontDefault(); 
        io.Fonts->Build();
    }

    if (io.BackendRendererUserData != nullptr) ImGui_ImplOpenGL3_CreateFontsTexture();
    g_current_rendered_size = targetSize;
    LOGI("[+] 字体构建已 100%% 完成。");
}

void DrawBoard() {
    if (!g_esp_board) return; ImDrawList* d = ImGui::GetForegroundDrawList();
    float curSzY = ImGui::GetIO().DisplaySize.y;
    float baseSz = 40.0f * 2.2f * (curSzY / 1080.0f); 
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
            ImGui::SliderFloat((const char*)u8"棋盘 X", &g_startX, 0, 2500);
            ImGui::SliderFloat((const char*)u8"棋盘 Y", &g_startY, 0, 2500);
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
    LOGI("[+] 渲染完毕，清理退出。");
}

// =================================================================
// 4. 进程与注入管理 (保持原样)
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
    LOGI("JKHelper Daemon Process Initialized.");
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
                LOGI("[!] 发现金铲铲 (%d)，执行自动注入...", pid);
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
