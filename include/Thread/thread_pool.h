#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "websocket/websocket.h"
#include "idmapping/idmapping.h"
#include "platform/platform.h"
#include <time.h>

// 线程池配置常量
#define DEFAULT_WORKER_THREADS 31        // 优化：增加默认线程数适应高并发
#define MAX_WORKER_THREADS 31            // 优化：提高最大线程数上限
#define MAX_QUEUE_SIZE 20000             // 优化：增大队列容量，减少任务丢弃
#define QUEUE_TIMEOUT_MS 100             // 优化：减少超时时间，提升响应速度

// 任务类型枚举
typedef enum {
    TASK_CLIENT_REQUEST,    // 客户端DNS请求
    TASK_UPSTREAM_RESPONSE, // 上游服务器响应
    TASK_SHUTDOWN          // 关闭信号
} task_type_t;

// DNS处理任务结构体
typedef struct {
    char buffer[BUF_SIZE];              // DNS数据包缓冲区
    int buffer_len;                     // 数据包长度
    struct sockaddr_in source_addr;     // 源地址信息
    socklen_t source_addr_len;          // 源地址长度
    task_type_t type;                   // 任务类型
    time_t created_time;                // 任务创建时间
} dns_task_t;

// 线程安全的任务队列
typedef struct {
    dns_task_t* tasks;                  // 任务数组（动态分配）
    int capacity;                       // 队列容量
    int head;                           // 队列头索引
    int tail;                           // 队列尾索引
    int count;                          // 当前任务数量
    int shutdown;                       // 关闭标志
    
    pthread_mutex_t mutex;              // 队列访问互斥锁
    pthread_cond_t not_empty;           // 队列非空条件变量
    pthread_cond_t not_full;            // 队列非满条件变量
} task_queue_t;
// 工作线程信息结构
typedef struct {
    pthread_t thread_id;                // 线程ID
    int thread_index;                   // 线程索引
    int is_active;                      // 是否活跃
    unsigned long processed_tasks;      // 已处理任务数
    time_t last_activity;               // 最后活动时间
} worker_thread_t;

// 线程池统计信息
typedef struct {
    unsigned long total_tasks_queued;   // 总入队任务数
    unsigned long total_tasks_processed; // 总处理任务数
    unsigned long total_tasks_dropped;  // 总丢弃任务数
    unsigned long client_requests;      // 客户端请求数
    unsigned long upstream_responses;   // 上游响应数
    time_t start_time;                  // 线程池启动时间
} thread_pool_stats_t;

// DNS线程池主结构
typedef struct {
    worker_thread_t* workers;           // 工作线程数组
    int worker_count;                   // 工作线程数量
    task_queue_t task_queue;            // 任务队列
    
    // 剩余必要的全局锁（ID映射表已使用分段锁，不再需要全局锁）
    pthread_mutex_t socket_mutex;           // Socket操作互斥锁（可选）
    pthread_mutex_t stats_mutex;            // 统计信息互斥锁
    
    // 线程池状态
    int is_initialized;                 // 是否已初始化
    int is_running;                     // 是否正在运行
    int shutdown_requested;             // 是否请求关闭
    
    // 统计信息
    thread_pool_stats_t stats;          // 性能统计
      // 全局资源引用
    SOCKET server_socket;               // 服务器Socket引用    
    dns_mapping_table_t* mapping_table; // ID映射表引用（已使用分段锁优化）
} dns_thread_pool_t;

/**
 * @brief 设置全局线程池实例
 * @param pool 线程池指针
 */
void thread_pool_set_global_instance(dns_thread_pool_t* pool);

/**
 * @brief 获取全局线程池实例
 * @return 线程池指针
 */
dns_thread_pool_t* thread_pool_get_global_instance(void);

// ============================================================================
// 线程池管理函数声明
// ============================================================================

/**
 * @brief 初始化DNS线程池
 * @param pool 线程池指针
 * @param worker_count 工作线程数量（0表示使用默认值）
 * @param queue_size 任务队列大小（0表示使用默认值）
 * @param server_socket 服务器Socket
 * @param mapping_table ID映射表指针
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int thread_pool_init(dns_thread_pool_t* pool, 
                     int worker_count, 
                     int queue_size,
                     SOCKET server_socket,
                     dns_mapping_table_t* mapping_table);

/**
 * @brief 启动线程池（创建工作线程）
 * @param pool 线程池指针
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int thread_pool_start(dns_thread_pool_t* pool);

/**
 * @brief 停止线程池（优雅关闭）
 * @param pool 线程池指针
 * @param timeout_ms 等待超时时间（毫秒）
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int thread_pool_stop(dns_thread_pool_t* pool, int timeout_ms);

/**
 * @brief 销毁线程池并释放资源
 * @param pool 线程池指针
 */
void thread_pool_destroy(dns_thread_pool_t* pool);

// ============================================================================
// 任务队列操作函数声明
// ============================================================================

/**
 * @brief 初始化任务队列
 * @param queue 队列指针
 * @param capacity 队列容量
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int task_queue_init(task_queue_t* queue, int capacity);

/**
 * @brief 向队列添加任务（非阻塞）
 * @param queue 队列指针
 * @param task 要添加的任务
 * @return 成功返回MYSUCCESS，队列满返回MYERROR
 */
int task_queue_push(task_queue_t* queue, const dns_task_t* task);

/**
 * @brief 从队列获取任务（阻塞）
 * @param queue 队列指针
 * @param task 输出任务指针
 * @param timeout_ms 超时时间（毫秒，-1表示无限等待）
 * @return 成功返回MYSUCCESS，超时或关闭返回MYERROR
 */
int task_queue_pop(task_queue_t* queue, dns_task_t* task, int timeout_ms);

/**
 * @brief 获取队列当前大小
 * @param queue 队列指针
 * @return 当前队列大小
 */
int task_queue_size(task_queue_t* queue);

/**
 * @brief 检查队列是否为空
 * @param queue 队列指针
 * @return 空返回1，非空返回0
 */
int task_queue_is_empty(task_queue_t* queue);

/**
 * @brief 检查队列是否已满
 * @param queue 队列指针
 * @return 满返回1，未满返回0
 */
int task_queue_is_full(task_queue_t* queue);

/**
 * @brief 销毁任务队列
 * @param queue 队列指针
 */
void task_queue_destroy(task_queue_t* queue);

// ============================================================================
// 工作线程函数声明
// ============================================================================

/**
 * @brief 工作线程主函数
 * @param arg 线程参数（dns_thread_pool_t指针）
 * @return 线程返回值
 */
THREAD_RETURN_TYPE worker_thread_main(void* arg);

// ============================================================================
// 统计和监控函数声明
// ============================================================================

/**
 * @brief 获取线程池统计信息
 * @param pool 线程池指针
 * @param stats 输出统计信息指针
 */
void thread_pool_get_stats(dns_thread_pool_t* pool, thread_pool_stats_t* stats);

/**
 * @brief 打印线程池状态信息
 * @param pool 线程池指针
 */
void thread_pool_print_status(dns_thread_pool_t* pool);

/**
 * @brief 重置统计信息
 * @param pool 线程池指针
 */
void thread_pool_reset_stats(dns_thread_pool_t* pool);

/**
 * @brief 向线程池提交DNS任务
 * @param pool 线程池指针
 * @param buffer DNS数据包缓冲区
 * @param buffer_len 数据包长度
 * @param source_addr 源地址
 * @param source_addr_len 源地址长度
 * @param task_type 任务类型
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int thread_pool_submit_task(dns_thread_pool_t* pool,
                           const char* buffer,
                           int buffer_len,
                           struct sockaddr_in source_addr,
                           socklen_t source_addr_len,
                           task_type_t task_type);

/**
 * @brief 线程安全的映射表操作：添加映射
 * @param original_id 原始ID
 * @param client_addr 客户端地址
 * @param client_addr_len 客户端地址长度
 * @param new_id 输出的新ID
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int thread_pool_add_mapping_safe(unsigned short original_id, 
                                 struct sockaddr_in* client_addr, 
                                 int client_addr_len, 
                                 unsigned short* new_id);

/**
 * @brief 线程安全的映射表操作：查找映射
 * @param new_id 新ID
 * @return 找到返回映射条目指针，未找到返回NULL
 */
dns_mapping_entry_t* thread_pool_find_mapping_safe(unsigned short new_id);

/**
 * @brief 线程安全的映射表操作：删除映射
 * @param new_id 新ID
 */
void thread_pool_remove_mapping_safe(unsigned short new_id);

/**
 * @brief 线程安全的映射表操作：清理过期映射
 */
void thread_pool_cleanup_mappings_safe(void);

#endif // THREAD_POOL_H
