#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    // Windows-specific includes
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <process.h>
    #pragma comment(lib, "ws2_32.lib")
      // Windows线程类型定义
    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    typedef CONDITION_VARIABLE pthread_cond_t;
    typedef struct {
        void* unused;
    } pthread_attr_t;
    typedef struct {
        void* unused;
    } pthread_mutexattr_t;
    typedef struct {
        void* unused;
    } pthread_condattr_t;
    
    // Windows读写锁结构体定义
    typedef struct {
        pthread_mutex_t mutex;          // 用于保护内部状态的互斥锁
        pthread_cond_t  readers_cond;   // 等待的读者条件变量
        pthread_cond_t  writer_cond;    // 等待的写者条件变量
        unsigned int    readers_active; // 当前活跃的读者数量
        unsigned int    writers_active; // 当前活跃的写者数量 (0或1)
        unsigned int    writers_waiting;// 正在等待的写者数量
    } pthread_rwlock_t;
    
    typedef struct {
        void* unused;
    } pthread_rwlockattr_t;
    
    #define THREAD_RETURN_TYPE unsigned int __stdcall
    #define THREAD_RETURN_VALUE 0
#else
    // Linux-specific includes
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <pthread.h>
    #include <arpa/inet.h> // 包含 inet_ntoa
    
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
    
    #define THREAD_RETURN_TYPE void*
    #define THREAD_RETURN_VALUE NULL
#endif

// ============================================================================
// 网络相关函数声明
// ============================================================================
void platform_init();
void platform_cleanup();
SOCKET create_socket();
int set_socket_reuseaddr(SOCKET sock);
int set_socket_nonblocking(SOCKET sock);
int platform_get_last_error();

// ============================================================================
// 跨平台线程函数声明
// ============================================================================

/**
 * @brief 跨平台的互斥锁初始化
 * @param mutex 互斥锁指针
 * @param attr 属性（可为NULL）
 * @return 成功返回0，失败返回非0
 */
int platform_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);

/**
 * @brief 跨平台的互斥锁销毁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_mutex_destroy(pthread_mutex_t* mutex);

/**
 * @brief 跨平台的互斥锁加锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_mutex_lock(pthread_mutex_t* mutex);

/**
 * @brief 跨平台的互斥锁解锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_mutex_unlock(pthread_mutex_t* mutex);

/**
 * @brief 跨平台的条件变量初始化
 * @param cond 条件变量指针
 * @param attr 属性（可为NULL）
 * @return 成功返回0，失败返回非0
 */
int platform_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);

/**
 * @brief 跨平台的条件变量销毁
 * @param cond 条件变量指针
 * @return 成功返回0，失败返回非0
 */
int platform_cond_destroy(pthread_cond_t* cond);

/**
 * @brief 跨平台的条件变量等待
 * @param cond 条件变量指针
 * @param mutex 关联的互斥锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);

/**
 * @brief 跨平台的条件变量超时等待
 * @param cond 条件变量指针
 * @param mutex 关联的互斥锁指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回0，超时返回正数，失败返回负数
 */
int platform_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, int timeout_ms);

/**
 * @brief 跨平台的条件变量信号
 * @param cond 条件变量指针
 * @return 成功返回0，失败返回非0
 */
int platform_cond_signal(pthread_cond_t* cond);

/**
 * @brief 跨平台的条件变量广播
 * @param cond 条件变量指针
 * @return 成功返回0，失败返回非0
 */
int platform_cond_broadcast(pthread_cond_t* cond);

/**
 * @brief 跨平台的线程创建
 * @param thread 线程ID指针
 * @param attr 线程属性（可为NULL）
 * @param start_routine 线程函数
 * @param arg 线程参数
 * @return 成功返回0，失败返回非0
 */
int platform_thread_create(pthread_t* thread, const pthread_attr_t* attr, 
                          THREAD_RETURN_TYPE (*start_routine)(void*), void* arg);

/**
 * @brief 跨平台的线程等待
 * @param thread 线程ID
 * @param retval 返回值指针（可为NULL）
 * @return 成功返回0，失败返回非0
 */
int platform_thread_join(pthread_t thread, void** retval);

/**
 * @brief 跨平台的线程分离
 * @param thread 线程ID
 * @return 成功返回0，失败返回非0
 */
int platform_thread_detach(pthread_t thread);

/**
 * @brief 获取当前线程ID
 * @return 当前线程ID
 */
pthread_t platform_thread_self(void);

/**
 * @brief 跨平台的睡眠函数
 * @param ms 睡眠时间（毫秒）
 */
void platform_sleep_ms(int ms);

/**
 * @brief 获取系统CPU核心数
 * @return CPU核心数
 */
int platform_get_cpu_count(void);

// ============================================================================
// 跨平台读写锁函数声明
// ============================================================================

/**
 * @brief 跨平台的读写锁初始化
 * @param rwlock 读写锁指针
 * @param attr 属性（可为NULL）
 * @return 成功返回0，失败返回非0
 */
int platform_rwlock_init(pthread_rwlock_t* rwlock, const pthread_rwlockattr_t* attr);

/**
 * @brief 跨平台的读写锁销毁
 * @param rwlock 读写锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_rwlock_destroy(pthread_rwlock_t* rwlock);

/**
 * @brief 跨平台的读写锁读锁定
 * @param rwlock 读写锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_rwlock_rdlock(pthread_rwlock_t* rwlock);

/**
 * @brief 跨平台的读写锁写锁定
 * @param rwlock 读写锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_rwlock_wrlock(pthread_rwlock_t* rwlock);

/**
 * @brief 跨平台的读写锁解锁
 * @param rwlock 读写锁指针
 * @return 成功返回0，失败返回非0
 */
int platform_rwlock_unlock(pthread_rwlock_t* rwlock);

#endif // PLATFORM_H
