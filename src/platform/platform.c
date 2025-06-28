#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <time.h>
#include <sys/timeb.h>  // 为_ftime函数添加头文件
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
// 跨平台线程函数实现 - 现在Windows下也使用mingw64 pthread
// ============================================================================

int platform_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    return pthread_mutex_init(mutex, attr);
}

int platform_mutex_destroy(pthread_mutex_t* mutex) {
    return pthread_mutex_destroy(mutex);
}

int platform_mutex_lock(pthread_mutex_t* mutex) {
    return pthread_mutex_lock(mutex);
}

int platform_mutex_unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

// ============================================================================
// 跨平台读写锁函数实现 - 现在Windows下也使用mingw64 pthread
// ============================================================================

int platform_rwlock_init(pthread_rwlock_t* rwlock, const pthread_rwlockattr_t* attr) {
    return pthread_rwlock_init(rwlock, attr);
}

int platform_rwlock_destroy(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_destroy(rwlock);
}

int platform_rwlock_rdlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_rdlock(rwlock);
}

int platform_rwlock_wrlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_wrlock(rwlock);
}

int platform_rwlock_unlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_unlock(rwlock);
}

int platform_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
    return pthread_cond_init(cond, attr);
}

int platform_cond_destroy(pthread_cond_t* cond) {
    return pthread_cond_destroy(cond);
}

int platform_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    return pthread_cond_wait(cond, mutex);
}

int platform_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, int timeout_ms) {
#ifdef _WIN32
    // Windows下使用mingw64的pthread实现
    struct timespec ts;
    struct _timeb tb;
    _ftime(&tb);
    
    ts.tv_sec = tb.time + timeout_ms / 1000;
    ts.tv_nsec = (tb.millitm + (timeout_ms % 1000)) * 1000000;
    
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
    return pthread_cond_signal(cond);
}

int platform_cond_broadcast(pthread_cond_t* cond) {
    return pthread_cond_broadcast(cond);
}

int platform_thread_create(pthread_t* thread, const pthread_attr_t* attr, 
                          THREAD_RETURN_TYPE (*start_routine)(void*), void* arg) {
    return pthread_create(thread, attr, (void*(*)(void*))start_routine, arg);
}

int platform_thread_join(pthread_t thread, void** retval) {
    return pthread_join(thread, retval);
}

int platform_thread_detach(pthread_t thread) {
    return pthread_detach(thread);
}

pthread_t platform_thread_self(void) {
    return pthread_self();
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
