#include "Thread/thread_pool.h"
#include "debug/debug.h"
#include "websocket/dnsServer.h"
#include "websocket/datagram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// ============================================================================
// 全局变量定义
// ============================================================================
static dns_thread_pool_t* g_thread_pool = NULL;

// ============================================================================
// 内部辅助函数声明
// ============================================================================
static int calculate_optimal_threads(void);
static void update_worker_activity(worker_thread_t* worker);
static void increment_stats_counter(dns_thread_pool_t* pool, const char* counter_type);

// ============================================================================
// 任务队列操作函数实现
// ============================================================================

int task_queue_init(task_queue_t* queue, int capacity) {
    if (!queue || capacity <= 0) {
        log_error("任务队列初始化失败：参数无效");
        return MYERROR;
    }

    // 分配任务数组内存
    queue->tasks = (dns_task_t*)malloc(sizeof(dns_task_t) * capacity);
    if (!queue->tasks) {
        log_error("任务队列初始化失败：内存分配失败");
        return MYERROR;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->shutdown = 0;

    // 初始化同步原语
    if (platform_mutex_init(&queue->mutex, NULL) != 0) {
        log_error("任务队列初始化失败：互斥锁初始化失败");
        free(queue->tasks);
        return MYERROR;
    }

    if (platform_cond_init(&queue->not_empty, NULL) != 0) {
        log_error("任务队列初始化失败：条件变量not_empty初始化失败");
        platform_mutex_destroy(&queue->mutex);
        free(queue->tasks);
        return MYERROR;
    }

    if (platform_cond_init(&queue->not_full, NULL) != 0) {
        log_error("任务队列初始化失败：条件变量not_full初始化失败");
        platform_cond_destroy(&queue->not_empty);
        platform_mutex_destroy(&queue->mutex);
        free(queue->tasks);
        return MYERROR;
    }

    log_info("任务队列初始化成功，容量：%d", capacity);
    return MYSUCCESS;
}

int task_queue_push(task_queue_t* queue, const dns_task_t* task) {
    if (!queue || !task) {
        log_warn("任务入队失败：参数无效");
        return MYERROR;
    }

    platform_mutex_lock(&queue->mutex);

    // 检查队列是否已关闭
    if (queue->shutdown) {
        platform_mutex_unlock(&queue->mutex);
        log_debug("任务入队失败：队列已关闭");
        return MYERROR;
    }

    // 检查队列是否已满
    if (queue->count >= queue->capacity) {
        platform_mutex_unlock(&queue->mutex);
        log_warn("任务入队失败：队列已满（%d/%d）", queue->count, queue->capacity);
        return MYERROR;
    }

    // 添加任务到队列
    memcpy(&queue->tasks[queue->tail], task, sizeof(dns_task_t));
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    // 通知等待的工作线程
    platform_cond_signal(&queue->not_empty);
    platform_mutex_unlock(&queue->mutex);

    log_debug("任务成功入队，当前队列大小：%d/%d", queue->count, queue->capacity);
    return MYSUCCESS;
}

int task_queue_pop(task_queue_t* queue, dns_task_t* task, int timeout_ms) {
    if (!queue || !task) {
        log_warn("任务出队失败：参数无效");
        return MYERROR;
    }

    platform_mutex_lock(&queue->mutex);

    // 等待任务或关闭信号
    while (queue->count == 0 && !queue->shutdown) {
        int wait_result;
        if (timeout_ms < 0) {
            // 无限等待
            wait_result = platform_cond_wait(&queue->not_empty, &queue->mutex);
        } else {
            // 超时等待
            wait_result = platform_cond_timedwait(&queue->not_empty, &queue->mutex, timeout_ms);
        }

        if (wait_result > 0) {
            // 超时
            platform_mutex_unlock(&queue->mutex);
            log_debug("任务出队超时");
            return MYERROR;
        } else if (wait_result < 0) {
            // 其他错误
            platform_mutex_unlock(&queue->mutex);
            log_error("任务出队失败：条件变量等待错误");
            return MYERROR;
        }
    }

    // 检查是否因为关闭而退出等待
    if (queue->shutdown && queue->count == 0) {
        platform_mutex_unlock(&queue->mutex);
        log_debug("任务出队退出：队列已关闭");
        return MYERROR;
    }

    // 从队列中取出任务
    memcpy(task, &queue->tasks[queue->head], sizeof(dns_task_t));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    // 通知可能等待的生产者
    platform_cond_signal(&queue->not_full);
    platform_mutex_unlock(&queue->mutex);

    log_debug("任务成功出队，当前队列大小：%d/%d", queue->count, queue->capacity);
    return MYSUCCESS;
}

int task_queue_size(task_queue_t* queue) {
    if (!queue) return 0;
    
    platform_mutex_lock(&queue->mutex);
    int size = queue->count;
    platform_mutex_unlock(&queue->mutex);
    
    return size;
}

int task_queue_is_empty(task_queue_t* queue) {
    if (!queue) return 1;
    
    platform_mutex_lock(&queue->mutex);
    int empty = (queue->count == 0);
    platform_mutex_unlock(&queue->mutex);
    
    return empty;
}

int task_queue_is_full(task_queue_t* queue) {
    if (!queue) return 0;
    
    platform_mutex_lock(&queue->mutex);
    int full = (queue->count >= queue->capacity);
    platform_mutex_unlock(&queue->mutex);
    
    return full;
}

void task_queue_destroy(task_queue_t* queue) {
    if (!queue) return;

    platform_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    platform_cond_broadcast(&queue->not_empty);
    platform_cond_broadcast(&queue->not_full);
    platform_mutex_unlock(&queue->mutex);

    // 销毁同步原语
    platform_cond_destroy(&queue->not_empty);
    platform_cond_destroy(&queue->not_full);
    platform_mutex_destroy(&queue->mutex);

    // 释放内存
    if (queue->tasks) {
        free(queue->tasks);
        queue->tasks = NULL;
    }

    log_info("任务队列已销毁");
}

// ============================================================================
// 工作线程函数实现
// ============================================================================

THREAD_RETURN_TYPE worker_thread_main(void* arg) {
    dns_thread_pool_t* pool = (dns_thread_pool_t*)arg;
    if (!pool) {
        log_error("工作线程启动失败：线程池指针为空");
        return THREAD_RETURN_VALUE;
    }

    // 找到当前线程的信息结构
    // 由于线程ID比较的复杂性，我们使用一个简单的方法：
    // 在创建线程时就设置好索引，这里通过遍历查找空闲的工作线程槽位
    worker_thread_t* worker = NULL;
    pthread_t self_id = platform_thread_self();
    
    // 简化的线程查找：使用第一个匹配的未活跃线程
    for (int i = 0; i < pool->worker_count; i++) {
        if (!pool->workers[i].is_active) {
            worker = &pool->workers[i];
            worker->thread_id = self_id;
            break;
        }
    }

    if (!worker) {
        log_error("工作线程启动失败：无法分配工作线程槽位");
        return THREAD_RETURN_VALUE;
    }

    log_info("工作线程%d启动成功", worker->thread_index);
    worker->is_active = 1;

    // 主工作循环
    while (!pool->shutdown_requested) {
        dns_task_t task;
        
        // 从队列获取任务
        if (task_queue_pop(&pool->task_queue, &task, QUEUE_TIMEOUT_MS) != MYSUCCESS) {
            continue; // 超时或错误，继续循环
        }

        // 更新工作线程活动信息
        update_worker_activity(worker);

        // 处理关闭任务
        if (task.type == TASK_SHUTDOWN) {
            log_info("工作线程%d收到关闭信号", worker->thread_index);
            break;
        }

        // 处理DNS任务
        log_debug("工作线程%d开始处理任务，类型：%d", worker->thread_index, task.type);

        // 解析DNS数据包
        DNS_ENTITY* dns_entity = parse_dns_packet(task.buffer, task.buffer_len);
        if (!dns_entity) {
            log_warn("工作线程%d：DNS数据包解析失败", worker->thread_index);
            increment_stats_counter(pool, "dropped");
            continue;
        }

        // 根据任务类型分发处理
        if (task.type == TASK_CLIENT_REQUEST) {
            handle_client_requests(dns_entity, task.source_addr, task.source_addr_len, task.buffer_len);
            increment_stats_counter(pool, "client_request");
        } else if (task.type == TASK_UPSTREAM_RESPONSE) {
            handle_upstream_responses(dns_entity, task.source_addr, task.source_addr_len, task.buffer_len);
            increment_stats_counter(pool, "upstream_response");
        }

        // 释放DNS实体
        free_dns_entity(dns_entity);
        
        // 更新统计信息
        worker->processed_tasks++;
        increment_stats_counter(pool, "processed");

        log_debug("工作线程%d完成任务处理", worker->thread_index);
    }

    worker->is_active = 0;
    log_info("工作线程%d退出", worker->thread_index);
    return THREAD_RETURN_VALUE;
}

// ============================================================================
// 线程池管理函数实现
// ============================================================================

int thread_pool_init(dns_thread_pool_t* pool, 
                     int worker_count, 
                     int queue_size,
                     SOCKET server_socket,
                     dns_mapping_table_t* mapping_table) {
    if (!pool || !mapping_table) {
        log_error("线程池初始化失败：参数无效");
        return MYERROR;
    }

    // 清零结构体
    memset(pool, 0, sizeof(dns_thread_pool_t));

    // 设置默认值
    if (worker_count <= 0) {
        worker_count = calculate_optimal_threads();
    }
    if (worker_count > MAX_WORKER_THREADS) {
        worker_count = MAX_WORKER_THREADS;
    }
    if (queue_size <= 0) {
        queue_size = MAX_QUEUE_SIZE;
    }

    pool->worker_count = worker_count;
    pool->server_socket = server_socket;
    pool->mapping_table = mapping_table;

    // 分配工作线程数组
    pool->workers = (worker_thread_t*)malloc(sizeof(worker_thread_t) * worker_count);
    if (!pool->workers) {
        log_error("线程池初始化失败：工作线程数组内存分配失败");
        return MYERROR;
    }

    // 初始化工作线程信息
    for (int i = 0; i < worker_count; i++) {
        pool->workers[i].thread_index = i;
        pool->workers[i].is_active = 0;
        pool->workers[i].processed_tasks = 0;
        pool->workers[i].last_activity = time(NULL);
    }

    // 初始化任务队列
    if (task_queue_init(&pool->task_queue, queue_size) != MYSUCCESS) {
        log_error("线程池初始化失败：任务队列初始化失败");
        free(pool->workers);
        return MYERROR;
    }

    // 初始化互斥锁
    if (platform_mutex_init(&pool->mapping_table_mutex, NULL) != 0) {
        log_error("线程池初始化失败：映射表互斥锁初始化失败");
        task_queue_destroy(&pool->task_queue);
        free(pool->workers);
        return MYERROR;
    }

    if (platform_mutex_init(&pool->socket_mutex, NULL) != 0) {
        log_error("线程池初始化失败：Socket互斥锁初始化失败");
        platform_mutex_destroy(&pool->mapping_table_mutex);
        task_queue_destroy(&pool->task_queue);
        free(pool->workers);
        return MYERROR;
    }

    if (platform_mutex_init(&pool->stats_mutex, NULL) != 0) {
        log_error("线程池初始化失败：统计互斥锁初始化失败");
        platform_mutex_destroy(&pool->socket_mutex);
        platform_mutex_destroy(&pool->mapping_table_mutex);
        task_queue_destroy(&pool->task_queue);
        free(pool->workers);
        return MYERROR;
    }

    // 初始化统计信息
    pool->stats.start_time = time(NULL);

    pool->is_initialized = 1;
    pool->is_running = 0;
    pool->shutdown_requested = 0;

    log_info("线程池初始化成功：%d个工作线程，队列容量：%d", worker_count, queue_size);
    return MYSUCCESS;
}

int thread_pool_start(dns_thread_pool_t* pool) {
    if (!pool || !pool->is_initialized) {
        log_error("线程池启动失败：未初始化");
        return MYERROR;
    }

    if (pool->is_running) {
        log_warn("线程池已在运行中");
        return MYSUCCESS;
    }

    // 创建工作线程
    for (int i = 0; i < pool->worker_count; i++) {
        if (platform_thread_create(&pool->workers[i].thread_id, NULL, 
                                  worker_thread_main, pool) != 0) {
            log_error("线程池启动失败：创建工作线程%d失败", i);
            
            // 清理已创建的线程
            pool->shutdown_requested = 1;
            for (int j = 0; j < i; j++) {
                platform_thread_join(pool->workers[j].thread_id, NULL);
            }
            pool->shutdown_requested = 0;
            return MYERROR;
        }
        log_debug("工作线程%d创建成功", i);
    }

    pool->is_running = 1;
    log_info("线程池启动成功，%d个工作线程已运行", pool->worker_count);
    return MYSUCCESS;
}

int thread_pool_stop(dns_thread_pool_t* pool, int timeout_ms) {
    if (!pool || !pool->is_running) {
        log_warn("线程池停止：未在运行中");
        return MYSUCCESS;
    }

    log_info("开始停止线程池...");
    pool->shutdown_requested = 1;

    // 向队列发送关闭任务
    dns_task_t shutdown_task;
    shutdown_task.type = TASK_SHUTDOWN;
    shutdown_task.created_time = time(NULL);
    
    for (int i = 0; i < pool->worker_count; i++) {
        task_queue_push(&pool->task_queue, &shutdown_task);
    }

    // 关闭任务队列
    platform_mutex_lock(&pool->task_queue.mutex);
    pool->task_queue.shutdown = 1;
    platform_cond_broadcast(&pool->task_queue.not_empty);
    platform_mutex_unlock(&pool->task_queue.mutex);

    // 等待所有线程结束
    int success = 1;
    for (int i = 0; i < pool->worker_count; i++) {
        if (timeout_ms > 0) {
            // TODO: 实现超时等待逻辑
            // 目前简化为普通等待
            if (platform_thread_join(pool->workers[i].thread_id, NULL) != 0) {
                log_error("等待工作线程%d结束失败", i);
                success = 0;
            }
        } else {
            if (platform_thread_join(pool->workers[i].thread_id, NULL) != 0) {
                log_error("等待工作线程%d结束失败", i);
                success = 0;
            }
        }
    }

    pool->is_running = 0;
    log_info("线程池已停止");
    return success ? MYSUCCESS : MYERROR;
}

void thread_pool_destroy(dns_thread_pool_t* pool) {
    if (!pool) return;

    if (pool->is_running) {
        log_warn("销毁线程池前自动停止...");
        thread_pool_stop(pool, 5000); // 5秒超时
    }

    // 销毁任务队列
    task_queue_destroy(&pool->task_queue);

    // 销毁互斥锁
    platform_mutex_destroy(&pool->mapping_table_mutex);
    platform_mutex_destroy(&pool->socket_mutex);
    platform_mutex_destroy(&pool->stats_mutex);

    // 释放工作线程数组
    if (pool->workers) {
        free(pool->workers);
        pool->workers = NULL;
    }

    // 清零结构体
    memset(pool, 0, sizeof(dns_thread_pool_t));
    
    log_info("线程池已销毁");
}

// ============================================================================
// 统计和监控函数实现
// ============================================================================

void thread_pool_get_stats(dns_thread_pool_t* pool, thread_pool_stats_t* stats) {
    if (!pool || !stats) return;

    platform_mutex_lock(&pool->stats_mutex);
    memcpy(stats, &pool->stats, sizeof(thread_pool_stats_t));
    platform_mutex_unlock(&pool->stats_mutex);
}

void thread_pool_print_status(dns_thread_pool_t* pool) {
    if (!pool) return;

    thread_pool_stats_t stats;
    thread_pool_get_stats(pool, &stats);

    time_t now = time(NULL);
    double uptime = difftime(now, stats.start_time);

    printf("\n=== DNS线程池状态 ===\n");
    printf("状态: %s\n", pool->is_running ? "运行中" : "已停止");
    printf("工作线程数: %d\n", pool->worker_count);
    printf("队列大小: %d/%d\n", task_queue_size(&pool->task_queue), pool->task_queue.capacity);
    printf("运行时间: %.0f 秒\n", uptime);
    printf("\n--- 处理统计 ---\n");
    printf("总入队任务: %lu\n", stats.total_tasks_queued);
    printf("总处理任务: %lu\n", stats.total_tasks_processed);
    printf("总丢弃任务: %lu\n", stats.total_tasks_dropped);
    printf("客户端请求: %lu\n", stats.client_requests);
    printf("上游响应: %lu\n", stats.upstream_responses);
    
    if (uptime > 0) {
        printf("处理速率: %.2f 任务/秒\n", stats.total_tasks_processed / uptime);
    }

    printf("\n--- 工作线程状态 ---\n");
    for (int i = 0; i < pool->worker_count; i++) {
        worker_thread_t* worker = &pool->workers[i];
        printf("线程%d: %s, 已处理: %lu\n", 
               i, worker->is_active ? "活跃" : "空闲", worker->processed_tasks);
    }
    printf("===================\n\n");
}

void thread_pool_reset_stats(dns_thread_pool_t* pool) {
    if (!pool) return;

    platform_mutex_lock(&pool->stats_mutex);
    memset(&pool->stats, 0, sizeof(thread_pool_stats_t));
    pool->stats.start_time = time(NULL);
    platform_mutex_unlock(&pool->stats_mutex);

    // 重置工作线程统计
    for (int i = 0; i < pool->worker_count; i++) {
        pool->workers[i].processed_tasks = 0;
    }

    log_info("线程池统计信息已重置");
}

// ============================================================================
// 公共接口函数实现
// ============================================================================

int thread_pool_submit_task(dns_thread_pool_t* pool,
                           const char* buffer,
                           int buffer_len,
                           struct sockaddr_in source_addr,
                           socklen_t source_addr_len,
                           task_type_t task_type) {
    if (!pool || !buffer || buffer_len <= 0 || buffer_len > BUF_SIZE) {
        log_warn("提交任务失败：参数无效");
        return MYERROR;
    }

    if (!pool->is_running) {
        log_warn("提交任务失败：线程池未运行");
        return MYERROR;
    }

    // 创建任务
    dns_task_t task;
    memcpy(task.buffer, buffer, buffer_len);
    task.buffer_len = buffer_len;
    task.source_addr = source_addr;
    task.source_addr_len = source_addr_len;
    task.type = task_type;
    task.created_time = time(NULL);

    // 提交任务到队列
    if (task_queue_push(&pool->task_queue, &task) != MYSUCCESS) {
        log_warn("提交任务失败：队列操作失败");
        increment_stats_counter(pool, "dropped");
        return MYERROR;
    }

    // 更新统计信息
    increment_stats_counter(pool, "queued");
    
    log_debug("任务提交成功，类型：%d，长度：%d", task_type, buffer_len);
    return MYSUCCESS;
}

// ============================================================================
// 全局线程池访问函数实现
// ============================================================================

void thread_pool_set_global_instance(dns_thread_pool_t* pool) {
    g_thread_pool = pool;
    if (pool) {
        log_debug("全局线程池实例已设置");
    } else {
        log_debug("全局线程池实例已清除");
    }
}

dns_thread_pool_t* thread_pool_get_global_instance(void) {
    return g_thread_pool;
}

// ============================================================================
// 内部辅助函数实现
// ============================================================================

static int calculate_optimal_threads(void) {
    int cpu_count = platform_get_cpu_count();
    
    // 对于I/O密集型任务，线程数可以适当超过CPU核心数
    // DNS代理主要是I/O操作，设置为CPU核心数的1.5-2倍
    int optimal = (cpu_count * 3) / 2;
    
    if (optimal < DEFAULT_WORKER_THREADS) {
        optimal = DEFAULT_WORKER_THREADS;
    }
    if (optimal > MAX_WORKER_THREADS) {
        optimal = MAX_WORKER_THREADS;
    }
    
    log_debug("计算最优线程数：CPU核心数=%d，推荐线程数=%d", cpu_count, optimal);
    return optimal;
}

static void update_worker_activity(worker_thread_t* worker) {
    if (!worker) return;
    worker->last_activity = time(NULL);
}

static void increment_stats_counter(dns_thread_pool_t* pool, const char* counter_type) {
    if (!pool || !counter_type) return;

    platform_mutex_lock(&pool->stats_mutex);
    
    if (strcmp(counter_type, "queued") == 0) {
        pool->stats.total_tasks_queued++;
    } else if (strcmp(counter_type, "processed") == 0) {
        pool->stats.total_tasks_processed++;
    } else if (strcmp(counter_type, "dropped") == 0) {
        pool->stats.total_tasks_dropped++;
    } else if (strcmp(counter_type, "client_request") == 0) {
        pool->stats.client_requests++;
    } else if (strcmp(counter_type, "upstream_response") == 0) {
        pool->stats.upstream_responses++;
    }
    
    platform_mutex_unlock(&pool->stats_mutex);
}

// ============================================================================
// 线程安全的映射表操作函数实现
// ============================================================================

int thread_pool_add_mapping_safe(unsigned short original_id, 
                                 struct sockaddr_in* client_addr, 
                                 int client_addr_len, 
                                 unsigned short* new_id) {
    if (!g_thread_pool || !g_thread_pool->mapping_table) {
        log_error("无法执行映射表操作：线程池未初始化");
        return MYERROR;
    }

    platform_mutex_lock(&g_thread_pool->mapping_table_mutex);
    int result = add_mapping(g_thread_pool->mapping_table, original_id, client_addr, client_addr_len, new_id);
    platform_mutex_unlock(&g_thread_pool->mapping_table_mutex);

    return result;
}

dns_mapping_entry_t* thread_pool_find_mapping_safe(unsigned short new_id) {
    if (!g_thread_pool || !g_thread_pool->mapping_table) {
        log_error("无法执行映射表操作：线程池未初始化");
        return NULL;
    }

    platform_mutex_lock(&g_thread_pool->mapping_table_mutex);
    dns_mapping_entry_t* mapping = find_mapping_by_new_id(g_thread_pool->mapping_table, new_id);
    platform_mutex_unlock(&g_thread_pool->mapping_table_mutex);

    return mapping;
}

void thread_pool_remove_mapping_safe(unsigned short new_id) {
    if (!g_thread_pool || !g_thread_pool->mapping_table) {
        log_error("无法执行映射表操作：线程池未初始化");
        return;
    }

    platform_mutex_lock(&g_thread_pool->mapping_table_mutex);
    remove_mapping(g_thread_pool->mapping_table, new_id);
    platform_mutex_unlock(&g_thread_pool->mapping_table_mutex);
}

void thread_pool_cleanup_mappings_safe(void) {
    if (!g_thread_pool || !g_thread_pool->mapping_table) {
        log_debug("跳过映射表清理：线程池未初始化");
        return;
    }

    platform_mutex_lock(&g_thread_pool->mapping_table_mutex);
    cleanup_expired_mappings(g_thread_pool->mapping_table);
    platform_mutex_unlock(&g_thread_pool->mapping_table_mutex);
}
