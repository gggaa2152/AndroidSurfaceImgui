#include <jni.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "includes/dobby.h" // 确保你的工程里有 dobby.h 和对应的静态库

#define LOG_TAG "JKHelper_Internal"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// =================================================================
// 1. 定义你要 Hook 的函数指针与旧函数保存地址
// =================================================================
typedef void (*RequestBuy_t)(void* instance, void* method);
RequestBuy_t old_RequestBuy = nullptr;

// 这是我们自己写的函数，游戏执行买牌时会先跑到这里！
void my_hook_RequestBuy(void* instance, void* method) {
    LOGI("[+] 拦截到游戏内部买牌 Call!");
    
    // 【在这里写你的神仙逻辑】
    // 例如：判断金币够不够，或者拦截特定卡牌
    
    // 执行完你的逻辑后，放行原本的买牌函数
    if (old_RequestBuy != nullptr) {
        old_RequestBuy(instance, method);
    }
}

// =================================================================
// 2. 核心 Hook 线程
// =================================================================
void* HookThread(void* arg) {
    LOGI("[*] 内部 Hook 线程已启动，等待 libil2cpp.so...");
    
    // 1. 循环等待游戏引擎完全加载
    uintptr_t il2cpp_base = 0;
    while (il2cpp_base == 0) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
                    il2cpp_base = strtoul(line, NULL, 16);
                    break;
                }
            }
            fclose(fp);
        }
        usleep(500000); // 0.5 秒查一次
    }
    
    LOGI("[+] 找到引擎基址: 0x%lx，开始下发 Dobby Hook", il2cpp_base);
    
    // 2. 假设你在 dump.cs 里查到的 Buy 函数偏移是 0x1234560
    uintptr_t target_func = il2cpp_base + 0x1234560;
    
    // 3. 执行 Dobby Hook
    int hook_status = DobbyHook((void*)target_func, (void*)my_hook_RequestBuy, (void**)&old_RequestBuy);
    
    if (hook_status == 0) {
        LOGI("[+] 核心函数 Hook 成功！内部逻辑已接管。");
    } else {
        LOGI("[-] Hook 失败，状态码: %d", hook_status);
    }
    
    return nullptr;
}

// =================================================================
// 3. SO 加载入口
// =================================================================
__attribute__((constructor)) void OnPluginLoaded() {
    LOGI("[+] 纯净内部 Hook 库被系统成功加载！");
    
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, HookThread, nullptr);
    pthread_attr_destroy(&attr);
}
