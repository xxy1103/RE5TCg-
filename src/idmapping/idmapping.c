# include "idmapping/idmapping.h"
#include "platform/platform.h"
#include "debug/debug.h"
#include <stdlib.h>

dns_mapping_table_t g_mapping_table;

// ============================================================================
// 分段锁优化辅助函数
// ============================================================================

/**
 * @brief 根据ID获取对应的映射分段
 */
static id_mapping_segment_t* get_mapping_segment(const dns_mapping_table_t* table, unsigned short id) {
    if (!table) return NULL;
    
    // 使用位运算快速定位分段（要求分段数为2的幂）
    unsigned int segment_index = id & (ID_MAPPING_NUM_SEGMENTS - 1);
    return (id_mapping_segment_t*)&table->segments[segment_index];
}

/**
 * @brief 计算在分段内的哈希索引
 */
static unsigned int calculate_segment_hash_index(unsigned short id, int buckets_per_segment) {
    // 使用ID的高位bits在分段内定位桶
    return (id >> 6) & (buckets_per_segment - 1);
}

/**
 * @brief 将条目添加到分段时间链表尾部
 */
static void segment_add_to_time_list(id_mapping_segment_t* segment, dns_mapping_entry_t* entry) {
    if (!segment || !entry) return;
    
    entry->time_next = NULL;
    entry->time_prev = segment->time_tail;
    
    if (segment->time_tail) {
        segment->time_tail->time_next = entry;
    } else {
        segment->time_head = entry;
    }
    segment->time_tail = entry;
}

/**
 * @brief 从分段时间链表中移除条目
 */
static void segment_remove_from_time_list(id_mapping_segment_t* segment, dns_mapping_entry_t* entry) {
    if (!segment || !entry) return;
    
    if (entry->time_prev) {
        entry->time_prev->time_next = entry->time_next;
    } else {
        segment->time_head = entry->time_next;
    }
    
    if (entry->time_next) {
        entry->time_next->time_prev = entry->time_prev;
    } else {
        segment->time_tail = entry->time_prev;
    }
    
    entry->time_next = NULL;
    entry->time_prev = NULL;
}

/**
 * @brief 清理单个分段的过期映射
 */
static void cleanup_segment_expired_mappings(dns_mapping_table_t* table, id_mapping_segment_t* segment) {
    if (!table || !segment) return;
    
    time_t current_time = time(NULL);
    int cleaned = 0;
    
    dns_mapping_entry_t* current = segment->time_head;
    
    // 从时间链表头部开始清理过期条目
    while (current != NULL && cleaned < CLEANUP_BATCH_SIZE && 
           (current_time - current->timestamp) > REQUEST_TIMEOUT) {
        
        dns_mapping_entry_t* next = current->time_next;
        
        log_debug("清理分段过期映射: 新ID=%d, 存活时间=%ld 秒", 
                 current->new_id, (long)(current_time - current->timestamp));
        
        // 从哈希表中移除（在分段内）
        int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
        unsigned int bucket_index = calculate_segment_hash_index(current->new_id, buckets_per_segment);
        
        dns_mapping_entry_t* bucket_current = segment->hash_buckets[bucket_index];
        dns_mapping_entry_t* bucket_prev = NULL;
        
        while (bucket_current) {
            if (bucket_current == current) {
                if (bucket_prev) {
                    bucket_prev->next = bucket_current->next;
                } else {
                    segment->hash_buckets[bucket_index] = bucket_current->next;
                }
                break;
            }
            bucket_prev = bucket_current;
            bucket_current = bucket_current->next;
        }
        
        // 从时间链表中移除
        segment_remove_from_time_list(segment, current);
        
        // 回收ID到全局栈中（需要获取ID栈锁）
        platform_mutex_lock(&table->id_stack_lock);
        push_id(&table->id_stack, current->new_id);
        platform_mutex_unlock(&table->id_stack_lock);
        
        // 回收条目到内存池（需要获取内存池锁）
        platform_mutex_lock(&table->pool_lock);
        current->is_active = 0;
        int entry_index = current - table->entry_pool;
        table->free_indices[++table->free_top] = entry_index;
        platform_mutex_unlock(&table->pool_lock);
        
        segment->active_count--;
        table->total_count--;
        cleaned++;
        
        current = next;
    }
    
    if (cleaned > 0) {
        log_debug("分段清理了 %d 个过期映射", cleaned);
    }
    
    segment->last_cleanup = current_time;
}

/**
 * @brief 哈希函数
 * 
 * 使用简单的哈希函数将ID映射到哈希表索引
 * 
 * @param id 要哈希的ID
 * @return unsigned int 哈希值
 */
static unsigned int hash_function(unsigned short id) {
    // 使用位运算实现快速取模（HASH_TABLE_SIZE必须是2的幂）
    return (unsigned int)id & (HASH_TABLE_SIZE - 1);
}

/**
 * @brief 初始化ID栈
 * 
 * @param stack ID栈指针
 */
static void init_id_stack(id_stack_t* stack) {
    stack->capacity = 65536; // 支持所有可能的ID值
    stack->ids = (unsigned short*)malloc(stack->capacity * sizeof(unsigned short));
    stack->top = -1;
    
    // 预填充所有可用ID（从65535到1，这样pop时从1开始）
    for (int i = 65535; i >= 1; i--) {
        stack->ids[++stack->top] = (unsigned short)i;
    }
}

/**
 * @brief 销毁ID栈
 * 
 * @param stack ID栈指针
 */
static void destroy_id_stack(id_stack_t* stack) {
    if (stack->ids) {
        free(stack->ids);
        stack->ids = NULL;
    }
    stack->top = -1;
    stack->capacity = 0;
}

/**
 * @brief 向ID栈推入一个ID
 * 
 * @param stack ID栈指针
 * @param id 要推入的ID
 * @return int 成功返回1，失败返回0
 */
static int push_id(id_stack_t* stack, unsigned short id) {
    if (stack->top >= stack->capacity - 1) {
        return 0; // 栈满
    }
    stack->ids[++stack->top] = id;
    return 1;
}

/**
 * @brief 从ID栈弹出一个ID
 * 
 * @param stack ID栈指针
 * @return unsigned short 弹出的ID，栈空返回0
 */
static unsigned short pop_id(id_stack_t* stack) {
    if (stack->top < 0) {
        return 0; // 栈空
    }
    return stack->ids[stack->top--];
}

/**
 * @brief 检查ID栈是否为空
 * 
 * @param stack ID栈指针
 * @return int 空返回1，非空返回0
 */
static int is_id_stack_empty(id_stack_t* stack) {
    return stack->top < 0;
}

/**
 * @brief 初始化映射表 - 分段锁版本
 * 
 * 初始化所有分段、内存池、ID栈等数据结构
 * 
 * @param table 指向要初始化的映射表的指针
 */
void init_mapping_table(dns_mapping_table_t* table) {
    // 清零整个结构
    memset(table, 0, sizeof(dns_mapping_table_t));
    
    // 初始化所有分段
    int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
    for (int i = 0; i < ID_MAPPING_NUM_SEGMENTS; i++) {
        // 初始化分段读写锁
        if (platform_rwlock_init(&table->segments[i].rwlock, NULL) != 0) {
            log_error("分段读写锁初始化失败: %d", i);
            // 清理已初始化的锁
            for (int j = 0; j < i; j++) {
                platform_rwlock_destroy(&table->segments[j].rwlock);
            }
            return;
        }
        
        // 清零分段哈希桶
        for (int k = 0; k < buckets_per_segment; k++) {
            table->segments[i].hash_buckets[k] = NULL;
        }
        
        table->segments[i].time_head = NULL;
        table->segments[i].time_tail = NULL;
        table->segments[i].active_count = 0;
        table->segments[i].last_cleanup = time(NULL);
    }
    
    // 分配条目池
    table->entry_pool = (dns_mapping_entry_t*)malloc(MAX_CONCURRENT_REQUESTS * sizeof(dns_mapping_entry_t));
    if (!table->entry_pool) {
        log_error("条目池内存分配失败");
        return;
    }
    memset(table->entry_pool, 0, MAX_CONCURRENT_REQUESTS * sizeof(dns_mapping_entry_t));
    
    // 初始化空闲索引栈
    table->free_indices = (int*)malloc(MAX_CONCURRENT_REQUESTS * sizeof(int));
    if (!table->free_indices) {
        log_error("空闲索引栈内存分配失败");
        free(table->entry_pool);
        return;
    }
    table->free_top = MAX_CONCURRENT_REQUESTS - 1;
    
    // 填充空闲索引栈（倒序填充，这样分配时从0开始）
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        table->free_indices[i] = MAX_CONCURRENT_REQUESTS - 1 - i;
    }
    
    // 初始化内存池锁
    if (platform_mutex_init(&table->pool_lock, NULL) != 0) {
        log_error("内存池锁初始化失败");
        free(table->entry_pool);
        free(table->free_indices);
        return;
    }
    
    // 初始化ID栈
    init_id_stack(&table->id_stack);
    
    // 初始化ID栈锁
    if (platform_mutex_init(&table->id_stack_lock, NULL) != 0) {
        log_error("ID栈锁初始化失败");
        platform_mutex_destroy(&table->pool_lock);
        free(table->entry_pool);
        free(table->free_indices);
        return;
    }
    
    // 初始化其他字段
    table->next_id = 1;
    table->total_count = 0;
    table->last_global_cleanup = time(NULL);
    
    log_info("分段式映射表初始化完成，容量: %d，分段数: %d，每段桶数: %d", 
             MAX_CONCURRENT_REQUESTS, ID_MAPPING_NUM_SEGMENTS, buckets_per_segment);
}

/**
 * @brief 添加新的映射关系 - 分段锁优化版本
 * 
 * 时间复杂度：O(1) 平均情况
 * 使用分段锁和预分配内存池实现高并发插入
 * 
 * @param table 映射表指针
 * @param original_id 客户端原始请求ID
 * @param client_addr 客户端地址信息
 * @param client_addr_len 客户端地址长度
 * @param new_id 输出参数，返回分配的新ID
 * @return int 成功返回MYSUCCESS，失败返回MYERROR
 */
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, 
                struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id) {
    
    // 检查映射表是否已满
    platform_mutex_lock(&table->pool_lock);
    if (table->free_top < 0) {
        platform_mutex_unlock(&table->pool_lock);
        log_error("映射表已满，无法添加新映射");
        return MYERROR;
    }
    
    // 从空闲池中获取条目
    int entry_index = table->free_indices[table->free_top--];
    platform_mutex_unlock(&table->pool_lock);
    
    dns_mapping_entry_t* entry = &table->entry_pool[entry_index];
    
    // 分配新ID
    platform_mutex_lock(&table->id_stack_lock);
    unsigned short allocated_id;
    if (!is_id_stack_empty(&table->id_stack)) {
        allocated_id = pop_id(&table->id_stack);
        platform_mutex_unlock(&table->id_stack_lock);
    } else {
        platform_mutex_unlock(&table->id_stack_lock);
        
        // 回收条目到内存池
        platform_mutex_lock(&table->pool_lock);
        table->free_indices[++table->free_top] = entry_index;
        platform_mutex_unlock(&table->pool_lock);
        
        log_error("ID栈已空，无法分配新ID");
        return MYERROR;
    }
    
    // 获取对应的分段
    id_mapping_segment_t* segment = get_mapping_segment(table, allocated_id);
    if (!segment) {
        // 回收资源
        platform_mutex_lock(&table->id_stack_lock);
        push_id(&table->id_stack, allocated_id);
        platform_mutex_unlock(&table->id_stack_lock);
        
        platform_mutex_lock(&table->pool_lock);
        table->free_indices[++table->free_top] = entry_index;
        platform_mutex_unlock(&table->pool_lock);
        return MYERROR;
    }
    
    // 填充条目信息
    time_t current_time = time(NULL);
    entry->original_id = original_id;
    entry->new_id = allocated_id;
    entry->client_addr = *client_addr;
    entry->client_addr_len = client_addr_len;
    entry->timestamp = current_time;
    entry->is_active = 1;
    entry->next = NULL;
    
    // 获取分段写锁并插入
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 计算在分段内的哈希索引
    int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
    unsigned int bucket_index = calculate_segment_hash_index(allocated_id, buckets_per_segment);
    
    // 插入分段哈希表
    entry->next = segment->hash_buckets[bucket_index];
    segment->hash_buckets[bucket_index] = entry;
    
    // 添加到分段时间链表
    segment_add_to_time_list(segment, entry);
    
    segment->active_count++;
    table->total_count++;
    
    platform_rwlock_unlock(&segment->rwlock);
    
    *new_id = allocated_id;
    
    log_debug("分段添加映射: 原始ID=%d -> 新ID=%d (分段%d, 桶%d, 总数: %d)", 
             original_id, allocated_id, allocated_id & (ID_MAPPING_NUM_SEGMENTS - 1), 
             bucket_index, table->total_count);
    return MYSUCCESS;
}



/**
 * @brief 根据新ID查找映射 - 分段锁优化版本
 * 
 * 时间复杂度：O(1) 平均情况，O(k) 最坏情况（k为分段内哈希冲突链长度）
 * 使用分段读锁实现高并发查找
 * 
 * @param table 映射表指针
 * @param new_id 要查找的新ID（来自上游响应）
 * @return dns_mapping_entry_t* 找到的映射条目指针，未找到返回NULL
 */
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id) {
    // 获取对应的分段
    id_mapping_segment_t* segment = get_mapping_segment(table, new_id);
    if (!segment) return NULL;
    
    dns_mapping_entry_t* result = NULL;
    
    // 获取分段读锁（允许多线程并发读取）
    platform_rwlock_rdlock(&segment->rwlock);
    
    // 计算在分段内的哈希索引
    int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
    unsigned int bucket_index = calculate_segment_hash_index(new_id, buckets_per_segment);
    
    // 在分段哈希桶中查找
    dns_mapping_entry_t* current = segment->hash_buckets[bucket_index];
    while (current != NULL) {
        if (current->is_active && current->new_id == new_id) {
            result = current;
            log_debug("分段查找命中: 新ID=%d, 分段%d, 桶%d", 
                     new_id, new_id & (ID_MAPPING_NUM_SEGMENTS - 1), bucket_index);
            break;
        }
        current = current->next;
    }
    
    platform_rwlock_unlock(&segment->rwlock);
    
    if (!result) {
        log_debug("分段查找未命中: 新ID=%d", new_id);
    }
    
    return result;
}



/**
 * @brief 移除映射 - 分段锁优化版本
 * 
 * 时间复杂度：O(1) 平均情况，O(k) 最坏情况（k为分段内哈希冲突链长度）
 * 使用分段写锁实现安全删除
 * 
 * @param table 映射表指针
 * @param new_id 要移除的映射的新ID
 */
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id) {
    // 获取对应的分段
    id_mapping_segment_t* segment = get_mapping_segment(table, new_id);
    if (!segment) return;
    
    // 获取分段写锁
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 计算在分段内的哈希索引
    int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
    unsigned int bucket_index = calculate_segment_hash_index(new_id, buckets_per_segment);
    
    dns_mapping_entry_t* current = segment->hash_buckets[bucket_index];
    dns_mapping_entry_t* prev = NULL;
    
    // 在分段哈希冲突链中查找并删除
    while (current != NULL) {
        if (current->is_active && current->new_id == new_id) {
            // 从分段哈希表中移除
            if (prev == NULL) {
                segment->hash_buckets[bucket_index] = current->next;
            } else {
                prev->next = current->next;
            }
            
            // 从分段时间链表中移除
            segment_remove_from_time_list(segment, current);
            
            segment->active_count--;
            table->total_count--;
            
            platform_rwlock_unlock(&segment->rwlock);
            
            // 回收ID到全局栈中（需要获取ID栈锁）
            platform_mutex_lock(&table->id_stack_lock);
            push_id(&table->id_stack, new_id);
            platform_mutex_unlock(&table->id_stack_lock);
            
            // 回收条目到内存池（需要获取内存池锁）
            platform_mutex_lock(&table->pool_lock);
            current->is_active = 0;
            int entry_index = current - table->entry_pool;
            table->free_indices[++table->free_top] = entry_index;
            platform_mutex_unlock(&table->pool_lock);
            
            log_debug("分段移除映射: 新ID=%d, 分段%d, 桶%d (剩余: %d)", 
                     new_id, new_id & (ID_MAPPING_NUM_SEGMENTS - 1), bucket_index, table->total_count);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    platform_rwlock_unlock(&segment->rwlock);
    log_warn("尝试移除不存在的映射: 新ID=%d", new_id);
}



/**
 * @brief 清理过期映射 - 分段锁优化版本
 * 
 * 时间复杂度：O(expired_count) 而不是 O(n)
 * 使用分段并行清理，提升清理效率
 * 
 * @param table 映射表指针
 */
void cleanup_expired_mappings(dns_mapping_table_t* table) {
    if (!table) return;
    
    time_t current_time = time(NULL);
    int total_cleaned = 0;
    
    // 并行清理所有分段
    for (int i = 0; i < ID_MAPPING_NUM_SEGMENTS; i++) {
        id_mapping_segment_t* segment = &table->segments[i];
        
        // 如果分段最近清理过，跳过
        if ((current_time - segment->last_cleanup) < REQUEST_TIMEOUT / 3) {
            continue;
        }
        
        // 获取分段写锁进行清理
        platform_rwlock_wrlock(&segment->rwlock);
        
        int segment_cleaned = 0;
        dns_mapping_entry_t* current = segment->time_head;
        
        // 清理分段内的过期条目
        while (current != NULL && segment_cleaned < CLEANUP_BATCH_SIZE && 
               (current_time - current->timestamp) > REQUEST_TIMEOUT) {
            
            dns_mapping_entry_t* next = current->time_next;
            
            // 从分段哈希表中移除
            int buckets_per_segment = HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS;
            unsigned int bucket_index = calculate_segment_hash_index(current->new_id, buckets_per_segment);
            
            dns_mapping_entry_t* bucket_current = segment->hash_buckets[bucket_index];
            dns_mapping_entry_t* bucket_prev = NULL;
            
            while (bucket_current) {
                if (bucket_current == current) {
                    if (bucket_prev) {
                        bucket_prev->next = bucket_current->next;
                    } else {
                        segment->hash_buckets[bucket_index] = bucket_current->next;
                    }
                    break;
                }
                bucket_prev = bucket_current;
                bucket_current = bucket_current->next;
            }
            
            // 从分段时间链表中移除
            segment_remove_from_time_list(segment, current);
            
            segment->active_count--;
            table->total_count--;
            segment_cleaned++;
            
            log_debug("分段清理过期映射: 新ID=%d, 存活时间=%ld 秒", 
                     current->new_id, (long)(current_time - current->timestamp));
            
            // 释放分段锁，回收资源到全局池
            platform_rwlock_unlock(&segment->rwlock);
            
            // 回收ID到全局栈中
            platform_mutex_lock(&table->id_stack_lock);
            push_id(&table->id_stack, current->new_id);
            platform_mutex_unlock(&table->id_stack_lock);
            
            // 回收条目到内存池
            platform_mutex_lock(&table->pool_lock);
            current->is_active = 0;
            int entry_index = current - table->entry_pool;
            table->free_indices[++table->free_top] = entry_index;
            platform_mutex_unlock(&table->pool_lock);
            
            // 重新获取分段锁继续清理
            platform_rwlock_wrlock(&segment->rwlock);
            current = next;
        }
        
        segment->last_cleanup = current_time;
        platform_rwlock_unlock(&segment->rwlock);
        
        total_cleaned += segment_cleaned;
    }
    
    if (total_cleaned > 0) {
        log_info("分段并行清理了 %d 个过期映射 (超时: %d 秒, 剩余: %d)", 
                total_cleaned, REQUEST_TIMEOUT, table->total_count);
    }
    
    table->last_global_cleanup = current_time;
}

/**
 * @brief 销毁映射表 - 分段锁版本
 * 
 * 释放所有分配的内存和锁资源
 * 
 * @param table 映射表指针
 */
void destroy_mapping_table(dns_mapping_table_t* table) {
    if (!table) return;
    
    // 销毁所有分段的读写锁
    for (int i = 0; i < ID_MAPPING_NUM_SEGMENTS; i++) {
        platform_rwlock_destroy(&table->segments[i].rwlock);
        
        // 清零分段结构
        table->segments[i].time_head = NULL;
        table->segments[i].time_tail = NULL;
        table->segments[i].active_count = 0;
    }
    
    // 释放内存池
    if (table->entry_pool) {
        free(table->entry_pool);
        table->entry_pool = NULL;
    }
    
    // 释放空闲索引栈
    if (table->free_indices) {
        free(table->free_indices);
        table->free_indices = NULL;
    }
    
    // 销毁锁
    platform_mutex_destroy(&table->pool_lock);
    platform_mutex_destroy(&table->id_stack_lock);
    
    // 销毁ID栈
    destroy_id_stack(&table->id_stack);
    
    table->total_count = 0;
    
    log_info("分段式映射表已销毁");
}