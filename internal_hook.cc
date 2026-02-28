#include <jni.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "dobby.h" // 引用你放在项目根目录的头文件

#define LOG_TAG "JKHelper_Internal"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// =================================================================
// 1. 定义 Hook 目标与原始函数指针
// =================================================================
// 保持函数名 RequestBuy 不变
typedef void (*RequestBuy_t)(void* instance, void* method);
RequestBuy_t old_RequestBuy = nullptr;

// 我们的 Hook 拦截函数
void my_hook_RequestBuy(void* instance, void* method) {
    LOGI("[+] 拦截到游戏买牌函数调用！");
    
    // 执行原本的逻辑（如果需要的话）
    if (old_RequestBuy != nullptr) {
        old_RequestBuy(instance, method);
    }
}

// =================================================================
// 2. 核心寻址与 Hook 线程
// =================================================================
void* HookThread(void* arg) {
    LOGI("[*] 内部 SO 已启动，正在等待游戏引擎 (libil2cpp.so) 加载...");
    
    uintptr_t il2cpp_base = 0;
    // 循环读取 maps 寻找基址
    while (il2cpp_base == 0) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
                    il2cpp_base = (uintptr_t)strtoul(line, NULL, 16);
                    break;
                }
            }
            fclose(fp);
        }
        if (il2cpp_base == 0) usleep(500000); // 没找到就歇 0.5 秒继续
    }
    
    LOGI("[+] 找到引擎基址: 0x%lx", il2cpp_base);
    
    // 【关键】：请在这里填入你从 dump.cs 找到的 RequestBuy 实际偏移量
    // 示例：如果偏移是 0x123456
    uintptr_t target_func = il2cpp_base + 0x123456; 
    
    // 执行 Dobby Hook
    DobbyHook((void*)target_func, (void*)my_hook_RequestBuy, (void**)&old_RequestBuy);
    
    LOGI("[+] RequestBuy 函数 Hook 成功！");
    
    return nullptr;
}

// =================================================================
// 3. SO 加载入口 (Constructor)
// =================================================================
__attribute__((constructor)) void OnPluginLoaded() {
    // 启动一个脱离主线程的后台线程执行 Hook 逻辑，防止阻塞游戏启动
    pthread_t t;
    pthread_create(&t, nullptr, HookThread, nullptr);
    pthread_detach(t);
}
