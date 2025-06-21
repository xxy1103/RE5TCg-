# include "idmapping/idmapping.h"
#include <stdlib.h>

dns_mapping_table_t g_mapping_table;

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
 * @brief 将条目添加到时间链表尾部
 * 
 * @param table 映射表指针
 * @param entry 要添加的条目
 */
static void add_to_time_list(dns_mapping_table_t* table, dns_mapping_entry_t* entry) {
    entry->time_next = NULL;
    entry->time_prev = table->time_tail;
    
    if (table->time_tail) {
        table->time_tail->time_next = entry;
    } else {
        table->time_head = entry;
    }
    table->time_tail = entry;
}

/**
 * @brief 从时间链表中移除条目
 * 
 * @param table 映射表指针
 * @param entry 要移除的条目
 */
static void remove_from_time_list(dns_mapping_table_t* table, dns_mapping_entry_t* entry) {
    if (entry->time_prev) {
        entry->time_prev->time_next = entry->time_next;
    } else {
        table->time_head = entry->time_next;
    }
    
    if (entry->time_next) {
        entry->time_next->time_prev = entry->time_prev;
    } else {
        table->time_tail = entry->time_prev;
    }
    
    entry->time_next = NULL;
    entry->time_prev = NULL;
}

/**
 * @brief 初始化映射表
 * 
 * 初始化哈希表、内存池、ID栈等所有数据结构
 * 
 * @param table 指向要初始化的映射表的指针
 */
void init_mapping_table(dns_mapping_table_t* table) {
    // 清零整个结构
    memset(table, 0, sizeof(dns_mapping_table_t));
    
    // 初始化哈希表
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        table->hash_table[i] = NULL;
    }
    
    // 分配条目池
    table->entry_pool = (dns_mapping_entry_t*)malloc(MAX_CONCURRENT_REQUESTS * sizeof(dns_mapping_entry_t));
    memset(table->entry_pool, 0, MAX_CONCURRENT_REQUESTS * sizeof(dns_mapping_entry_t));
    
    // 初始化空闲索引栈
    table->free_indices = (int*)malloc(MAX_CONCURRENT_REQUESTS * sizeof(int));
    table->free_top = MAX_CONCURRENT_REQUESTS - 1;
    
    // 填充空闲索引栈（倒序填充，这样分配时从0开始）
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        table->free_indices[i] = MAX_CONCURRENT_REQUESTS - 1 - i;
    }
    
    // 初始化ID栈
    init_id_stack(&table->id_stack);
    
    // 初始化其他字段
    table->next_id = 1;
    table->count = 0;
    table->time_head = NULL;
    table->time_tail = NULL;
    table->last_cleanup = time(NULL);
    
    log_debug("映射表初始化完成，容量: %d，哈希表大小: %d", 
             MAX_CONCURRENT_REQUESTS, HASH_TABLE_SIZE);
}

/**
 * @brief 添加新的映射关系 - 优化版本
 * 
 * 时间复杂度：O(1) 平均情况
 * 使用哈希表和预分配内存池实现快速插入
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
    // 检查是否需要清理过期映射（每10次插入检查一次，避免过于频繁）
    time_t current_time = time(NULL);
    if (table->count > 0 && 
        (current_time - table->last_cleanup) > REQUEST_TIMEOUT / 2) {
        cleanup_expired_mappings(table);
        table->last_cleanup = current_time;
    }
    
    // 检查映射表是否已满
    if (table->free_top < 0) {
        log_error("映射表已满，无法添加新映射");
        return MYERROR;
    }
    
    // 分配新ID
    unsigned short allocated_id;
    if (!is_id_stack_empty(&table->id_stack)) {
        allocated_id = pop_id(&table->id_stack);
    } else {
        allocated_id = table->next_id++;
        if (table->next_id == 0) table->next_id = 1; // 避免ID为0
    }
    
    // 从空闲池中获取条目
    int entry_index = table->free_indices[table->free_top--];
    dns_mapping_entry_t* entry = &table->entry_pool[entry_index];
    
    // 填充条目信息
    entry->original_id = original_id;
    entry->new_id = allocated_id;
    entry->client_addr = *client_addr;
    entry->client_addr_len = client_addr_len;
    entry->timestamp = current_time;
    entry->is_active = 1;
    entry->next = NULL;
    
    // 插入哈希表
    unsigned int hash_index = hash_function(allocated_id);
    entry->next = table->hash_table[hash_index];
    table->hash_table[hash_index] = entry;
    
    // 添加到时间链表
    add_to_time_list(table, entry);
    
    table->count++;
    *new_id = allocated_id;
    
    log_debug("快速添加映射: 原始ID=%d -> 新ID=%d (哈希槽位 %d, 总数: %d)", 
             original_id, allocated_id, hash_index, table->count);
    return MYSUCCESS;
}



/**
 * @brief 根据新ID查找映射 - 优化版本
 * 
 * 时间复杂度：O(1) 平均情况，O(k) 最坏情况（k为哈希冲突链长度）
 * 使用哈希表实现快速查找
 * 
 * @param table 映射表指针
 * @param new_id 要查找的新ID（来自上游响应）
 * @return dns_mapping_entry_t* 找到的映射条目指针，未找到返回NULL
 */
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id) {
    unsigned int hash_index = hash_function(new_id);
    dns_mapping_entry_t* current = table->hash_table[hash_index];
    
    // 遍历哈希冲突链
    while (current != NULL) {
        if (current->is_active && current->new_id == new_id) {
            log_debug("快速找到新ID=%d 的映射，哈希槽位 %d", new_id, hash_index);
            return current;
        }
        current = current->next;
    }
    
    log_debug("未找到新ID=%d 对应的映射", new_id);
    return NULL;
}



/**
 * @brief 移除映射 - 优化版本
 * 
 * 时间复杂度：O(1) 平均情况，O(k) 最坏情况（k为哈希冲突链长度）
 * 使用哈希表实现快速删除
 * 
 * @param table 映射表指针
 * @param new_id 要移除的映射的新ID
 */
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id) {
    unsigned int hash_index = hash_function(new_id);
    dns_mapping_entry_t* current = table->hash_table[hash_index];
    dns_mapping_entry_t* prev = NULL;
    
    // 在哈希冲突链中查找并删除
    while (current != NULL) {
        if (current->is_active && current->new_id == new_id) {
            // 从哈希表中移除
            if (prev == NULL) {
                table->hash_table[hash_index] = current->next;
            } else {
                prev->next = current->next;
            }
            
            // 从时间链表中移除
            remove_from_time_list(table, current);
            
            // 回收ID到栈中
            push_id(&table->id_stack, new_id);
            
            // 标记为非活跃并回收条目到空闲池
            current->is_active = 0;
            int entry_index = current - table->entry_pool;
            table->free_indices[++table->free_top] = entry_index;
            
            table->count--;
            
            log_debug("快速移除映射: 新ID=%d，哈希槽位 %d (剩余: %d)", 
                     new_id, hash_index, table->count);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    log_warn("尝试移除不存在的映射: 新ID=%d", new_id);
}



/**
 * @brief 清理过期映射 - 优化版本
 * 
 * 时间复杂度：O(expired_count) 而不是 O(n)
 * 使用时间链表实现快速过期清理，只遍历可能过期的条目
 * 
 * @param table 映射表指针
 */
void cleanup_expired_mappings(dns_mapping_table_t* table) {
    time_t current_time = time(NULL);
    int cleaned = 0;
    
    dns_mapping_entry_t* current = table->time_head;
    
    // 由于时间链表是按时间顺序排列的，我们可以从头开始删除过期条目
    // 一旦遇到未过期的条目，就可以停止
    while (current != NULL && 
           (current_time - current->timestamp) > REQUEST_TIMEOUT) {
        
        dns_mapping_entry_t* next = current->time_next;
        
        log_debug("清理过期映射: 新ID=%d, 存活时间=%ld 秒", 
                 current->new_id, (long)(current_time - current->timestamp));
        
        // 移除该映射（这会自动处理哈希表和时间链表的清理）
        remove_mapping(table, current->new_id);
        cleaned++;
        
        current = next;
    }
    
    if (cleaned > 0) {
        log_info("快速清理了 %d 个过期映射 (超时: %d 秒, 剩余: %d)", 
                cleaned, REQUEST_TIMEOUT, table->count);
    }
}

/**
 * @brief 销毁映射表
 * 
 * 释放所有分配的内存
 * 
 * @param table 映射表指针
 */
void destroy_mapping_table(dns_mapping_table_t* table) {
    if (table->entry_pool) {
        free(table->entry_pool);
        table->entry_pool = NULL;
    }
    
    if (table->free_indices) {
        free(table->free_indices);
        table->free_indices = NULL;
    }
    
    destroy_id_stack(&table->id_stack);
    
    // 清零哈希表
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        table->hash_table[i] = NULL;
    }
    
    table->time_head = NULL;
    table->time_tail = NULL;
    table->count = 0;
    
    log_debug("映射表已销毁");
}