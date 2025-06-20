#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#endif



void platform_init() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("WSAStartup failed");
        exit(EXIT_FAILURE);
    }
#endif
}

void platform_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

SOCKET create_socket() {
    return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

int set_socket_reuseaddr(SOCKET sock) {
    int optval = 1;
#ifdef _WIN32
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    return setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    
#endif
}

int set_socket_nonblocking(SOCKET sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

int platform_get_last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// ============================================================================
// 跨平台线程函数实现
// ============================================================================

int platform_mutex_init(pthread_mutex_t* mutex, const pthread_attr_t* attr) {
    (void)attr; // 忽略属性参数
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, attr);
#endif
}

int platform_mutex_destroy(pthread_mutex_t* mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_destroy(mutex);
#endif
}

int platform_mutex_lock(pthread_mutex_t* mutex) {
#ifdef _WIN32
    EnterCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_lock(mutex);
#endif
}

int platform_mutex_unlock(pthread_mutex_t* mutex) {
#ifdef _WIN32
    LeaveCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_unlock(mutex);
#endif
}

int platform_cond_init(pthread_cond_t* cond, const pthread_attr_t* attr) {
    (void)attr; // 忽略属性参数
#ifdef _WIN32
    InitializeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_init(cond, attr);
#endif
}

int platform_cond_destroy(pthread_cond_t* cond) {
#ifdef _WIN32
    // Windows下ConditionVariable不需要显式销毁
    (void)cond;
    return 0;
#else
    return pthread_cond_destroy(cond);
#endif
}

int platform_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
#ifdef _WIN32
    if (!SleepConditionVariableCS(cond, mutex, INFINITE)) {
        return GetLastError();
    }
    return 0;
#else
    return pthread_cond_wait(cond, mutex);
#endif
}

int platform_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, int timeout_ms) {
#ifdef _WIN32
    DWORD result = SleepConditionVariableCS(cond, mutex, (DWORD)timeout_ms);
    if (!result) {
        DWORD error = GetLastError();
        if (error == ERROR_TIMEOUT) {
            return 1; // 超时
        }
        return -1; // 其他错误
    }
    return 0; // 成功
#else
    struct timespec ts;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    ts.tv_sec = now.tv_sec + timeout_ms / 1000;
    ts.tv_nsec = (now.tv_usec + (timeout_ms % 1000) * 1000) * 1000;
    
    // 处理纳秒溢出
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(cond, mutex, &ts);
    if (result == ETIMEDOUT) {
        return 1; // 超时
    } else if (result != 0) {
        return -1; // 其他错误
    }
    return 0; // 成功
#endif
}

int platform_cond_signal(pthread_cond_t* cond) {
#ifdef _WIN32
    WakeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_signal(cond);
#endif
}

int platform_cond_broadcast(pthread_cond_t* cond) {
#ifdef _WIN32
    WakeAllConditionVariable(cond);
    return 0;
#else
    return pthread_cond_broadcast(cond);
#endif
}

int platform_thread_create(pthread_t* thread, const pthread_attr_t* attr, 
                          THREAD_RETURN_TYPE (*start_routine)(void*), void* arg) {
    (void)attr; // 忽略属性参数
#ifdef _WIN32
    *thread = (HANDLE)_beginthreadex(NULL, 0, start_routine, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
#else
    return pthread_create(thread, attr, start_routine, arg);
#endif
}

int platform_thread_join(pthread_t thread, void** retval) {
#ifdef _WIN32
    (void)retval; // Windows下不支持获取返回值
    DWORD result = WaitForSingleObject(thread, INFINITE);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(thread);
        return 0;
    }
    return -1;
#else
    return pthread_join(thread, retval);
#endif
}

int platform_thread_detach(pthread_t thread) {
#ifdef _WIN32
    return CloseHandle(thread) ? 0 : -1;
#else
    return pthread_detach(thread);
#endif
}

pthread_t platform_thread_self(void) {
#ifdef _WIN32
    return GetCurrentThread();
#else
    return pthread_self();
#endif
}

void platform_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep(ms * 1000);
#endif
}

int platform_get_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
#else
    return get_nprocs();
#endif
}
