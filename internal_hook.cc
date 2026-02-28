#include <jni.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

#define LOG_TAG "JKHelper_Internal"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// 安全线程：只打印日志，不触碰任何游戏内存
void* SafeThread(void* arg) {
    LOGI("[*] ========================================");
    LOGI("[*] 内部 SO 已成功加载到金铲铲进程！");
    LOGI("[*] 当前为【纯净测试模式】，没有任何 Hook 动作。");
    LOGI("[*] ========================================");
    
    // 保持 SO 在内部运行，每 10 秒跳动一次心跳日志
    while (true) {
        sleep(10);
        LOGI("[*] 内部 SO 存活心跳... 游戏未发生崩溃。");
    }
    return nullptr;
}

// SO 被 dlopen 时自动执行的入口点
__attribute__((constructor)) void OnPluginLoaded() {
    pthread_t t;
    pthread_create(&t, nullptr, SafeThread, nullptr);
    pthread_detach(t);
}
