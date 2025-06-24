#include "DNScache/relayBuild.h"
#include "DNScache/free_stack.h"
#include "websocket/websocket.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// 全局变量
// ============================================================================

static domain_table_t g_domain_table;      // 全局本地域名表
static dns_lru_cache_t g_dns_cache;        // 全局LRU缓存

// ============================================================================
// 哈希函数实现
// ============================================================================

/**
 * @brief 域名哈希函数
 * 使用djb2算法计算域名的哈希值
 */
unsigned int hash_domain(const char* domain) {
    if (!domain) return 0;
    
    unsigned int hash = 5381;
    int c;
    
    // 转换为小写并计算哈希
    while ((c = *domain++)) {
        if (c >= 'A' && c <= 'Z') {
            c += 32; // 转换为小写
        }
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash;
}

/**
 * @brief 根据域名获取其所属的缓存段
 */
dns_cache_segment_t* get_cache_segment(dns_lru_cache_t* cache, const char* domain) {
    if (!cache || !domain) return NULL;
    
    unsigned int hash = hash_domain(domain);
    // 使用位运算快速取模，前提是段数量为2的幂
    return &cache->segments[hash & (DNS_CACHE_NUM_SEGMENTS - 1)];
}

// ============================================================================
// 本地域名表实现
// ============================================================================

/**
 * @brief 初始化本地域名表
 */
int domain_table_init(domain_table_t* table) {
    if (!table) return MYERROR;
    
    // 清零哈希表
    for (int i = 0; i < DOMAIN_TABLE_HASH_SIZE; i++) { //ok
        table->hash_table[i] = NULL;
    }
    
    table->entry_count = 0;
    table->last_load_time = 0;
    
    log_info("本地域名表初始化完成");
    return MYSUCCESS;
}

/**
 * @brief 从文件加载域名表
 */
int domain_table_load_from_file(domain_table_t* table, const char* filename) {
    if (!table || !filename) return MYERROR;
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        log_error("无法打开域名表文件: %s", filename);
        return MYERROR;
    }
    
    char line[512];
    int loaded_count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // 移除换行符
        line[strcspn(line, "\r\n")] = '\0';
        
        // 跳过空行和注释
        if (line[0] == '\0' || line[0] == '#') continue;
        
        // 解析 IP 域名 格式
        char ip[MAX_IP_LENGTH];
        char domain[MAX_DOMAIN_LENGTH];
        
        if (sscanf(line, "%s %s", ip, domain) != 2) {
            log_warn("跳过无效行: %s", line);
            continue;
        }
        
        // 创建新条目
        domain_entry_t* entry = (domain_entry_t*)malloc(sizeof(domain_entry_t));
        if (!entry) {
            log_error("内存分配失败");
            continue;
        }
        
        // 复制数据
        strncpy(entry->domain, domain, MAX_DOMAIN_LENGTH - 1);
        entry->domain[MAX_DOMAIN_LENGTH - 1] = '\0';
        strncpy(entry->ip, ip, MAX_IP_LENGTH - 1);
        entry->ip[MAX_IP_LENGTH - 1] = '\0';
        
        // 检查是否为阻止条目
        entry->is_blocked = (strcmp(ip, "0.0.0.0") == 0);
        
        // 插入哈希表
        unsigned int hash_index = hash_domain(domain) % DOMAIN_TABLE_HASH_SIZE;
        entry->next = table->hash_table[hash_index];
        table->hash_table[hash_index] = entry;
        
        loaded_count++;
    }
    
    fclose(file);
    table->entry_count = loaded_count;
    table->last_load_time = time(NULL);
    
    log_info("成功加载 %d 个域名条目", loaded_count);
    return MYSUCCESS;
}

/**
 * @brief 查找域名表条目
 */
domain_entry_t* domain_table_lookup(domain_table_t* table, const char* domain) {
    if (!table || !domain) return NULL;
    
    unsigned int hash_index = hash_domain(domain) % DOMAIN_TABLE_HASH_SIZE;
    domain_entry_t* current = table->hash_table[hash_index];
    
    while (current) {
        // 不区分大小写比较
        if (strcasecmp(current->domain, domain) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/**
 * @brief 销毁域名表
 */
void domain_table_destroy(domain_table_t* table) {
    if (!table) return;
    
    for (int i = 0; i < DOMAIN_TABLE_HASH_SIZE; i++) { //ok 
        domain_entry_t* current = table->hash_table[i];
        while (current) {
            domain_entry_t* next = current->next;
            free(current);
            current = next;
        }
        table->hash_table[i] = NULL;
    }
    
    table->entry_count = 0;
    log_info("本地域名表已销毁");
}

// ============================================================================
// LRU缓存实现
// ============================================================================

/**
 * @brief 初始化DNS缓存
 */
int dns_cache_init(dns_lru_cache_t* cache, int max_size) {
    if (!cache || max_size <= 0) return MYERROR;
    
    // 清零哈希表
    for (int i = 0; i < DNS_CACHE_HASH_SIZE; i++) { //ok
        cache->hash_table[i] = NULL;
    }
    
    // 预分配条目池
    cache->entry_pool = (dns_cache_entry_t*)calloc(max_size, sizeof(dns_cache_entry_t));
    if (!cache->entry_pool) {
        log_error("DNS缓存条目池内存分配失败");
        return MYERROR;
    }
    
    // 初始化空闲栈
    if (free_stack_init(&cache->free_stack, max_size) != 0) {
        log_error("空闲栈初始化失败");
        free(cache->entry_pool);
        return MYERROR;
    }
    
    // 初始化内存池锁
    if (platform_mutex_init(&cache->pool_lock, NULL) != 0) {
        log_error("内存池锁初始化失败");
        free_stack_destroy(&cache->free_stack);
        free(cache->entry_pool);
        return MYERROR;
    }
    
    // 初始化所有分段
    int segment_max_size = max_size / DNS_CACHE_NUM_SEGMENTS;
    if (segment_max_size <= 0) segment_max_size = 1; // 至少每段一个条目
    
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        if (platform_rwlock_init(&cache->segments[i].rwlock, NULL) != 0) {
            log_error("分段读写锁初始化失败: %d", i);
            // 清理已初始化的锁
            for (int j = 0; j < i; j++) {
                platform_rwlock_destroy(&cache->segments[j].rwlock);
            }
            platform_mutex_destroy(&cache->pool_lock);
            free_stack_destroy(&cache->free_stack);
            free(cache->entry_pool);
            return MYERROR;
        }
        cache->segments[i].lru_head = NULL;
        cache->segments[i].lru_tail = NULL;
        cache->segments[i].current_size = 0;
        cache->segments[i].max_size = segment_max_size;
    }
    
    cache->max_size = max_size;
    cache->cache_hits = 0;
    cache->cache_misses = 0;
    cache->cache_evictions = 0;
    
    log_info("DNS分段式缓存初始化完成，容量: %d, 分段数: %d, 每段容量: %d", 
             max_size, DNS_CACHE_NUM_SEGMENTS, segment_max_size);
    return MYSUCCESS;
}

/**
 * @brief 将缓存条目移动到分段LRU链表头部
 */
void lru_move_to_head_segment(dns_cache_segment_t* segment, dns_cache_entry_t* entry) {
    if (!segment || !entry) return;
    
    // 如果已经是头部，直接返回
    if (segment->lru_head == entry) return;
    
    // 从当前位置移除
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    if (segment->lru_tail == entry) {
        segment->lru_tail = entry->prev;
    }
    
    // 插入到头部
    entry->prev = NULL;
    entry->next = segment->lru_head;
    if (segment->lru_head) {
        segment->lru_head->prev = entry;
    }
    segment->lru_head = entry;
    
    // 如果链表为空，设置尾部
    if (!segment->lru_tail) {
        segment->lru_tail = entry;
    }
}

/**
 * @brief 移除分段LRU链表尾部条目并返回（不释放内存）
 */
dns_cache_entry_t* lru_remove_tail_segment(dns_lru_cache_t* cache, dns_cache_segment_t* segment) {
    if (!cache || !segment || !segment->lru_tail) return NULL;
    
    dns_cache_entry_t* tail = segment->lru_tail;
    
    // 从哈希表中移除
    unsigned int hash_index = hash_domain(tail->domain) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = cache->hash_table[hash_index];
    dns_cache_entry_t* prev = NULL;
    
    while (current) {
        if (current == tail) {
            if (prev) {
                prev->hash_next = current->hash_next;
            } else {
                cache->hash_table[hash_index] = current->hash_next;
            }
            break;
        }
        prev = current;
        current = current->hash_next;
    }
    
    // 从分段LRU链表中移除
    if (tail->prev) {
        tail->prev->next = NULL;
    }
    segment->lru_tail = tail->prev;
    
    if (segment->lru_head == tail) {
        segment->lru_head = NULL;
    }
    
    // 减少分段大小
    segment->current_size--;
    cache->cache_evictions++;
    
    log_debug("分段LRU缓存移除尾部条目: %s, 分段当前大小: %d", tail->domain, segment->current_size);
    return tail;
}

/**
 * @brief 从缓存获取DNS条目（分段读写锁版本）
 */
dns_cache_entry_t* dns_cache_get(dns_lru_cache_t* cache, const char* domain) {
    if (!cache || !domain) return NULL;
    
    // 获取对应的分段
    dns_cache_segment_t* segment = get_cache_segment(cache, domain);
    if (!segment) return NULL;
    
    dns_cache_entry_t* result = NULL;
    
    // 获取读锁
    platform_rwlock_rdlock(&segment->rwlock);
    
    unsigned int hash_index = hash_domain(domain) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = cache->hash_table[hash_index];
    
    while (current) {
        if (strcasecmp(current->domain, domain) == 0) {
            // 检查是否过期
            time_t now = time(NULL);
            if (now > current->expire_time) {
                log_debug("缓存条目已过期: %s", domain);
                cache->cache_misses++;
                break; // 过期了，退出循环
            }
            
            // 命中了，但需要升级为写锁来更新LRU
            platform_rwlock_unlock(&segment->rwlock);
            platform_rwlock_wrlock(&segment->rwlock);
            
            // 再次检查（因为在锁切换期间可能被其他线程修改）
            if (now <= current->expire_time && strcasecmp(current->domain, domain) == 0) {
                // 更新访问时间并移动到头部
                current->access_time = now;
                lru_move_to_head_segment(segment, current);
                cache->cache_hits++;
                result = current;
                log_debug("缓存命中: %s", domain);
            } else {
                cache->cache_misses++;
            }
            break;
        }
        current = current->hash_next;
    }
    
    if (!result) {
        cache->cache_misses++;
        log_debug("缓存未命中: %s", domain);
    }
    
    platform_rwlock_unlock(&segment->rwlock);
    return result;
}

/**
 * @brief 内部函数：查找缓存条目（不检查过期，不移动LRU）
 * 仅用于内部逻辑，根据域名查找对应的缓存条目
 */
static dns_cache_entry_t* dns_cache_find_entry_internal(dns_lru_cache_t* cache, const char* domain) {
    if (!cache || !domain) return NULL;
    
    unsigned int hash_index = hash_domain(domain) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = cache->hash_table[hash_index];
    
    while (current) {
        if (strcasecmp(current->domain, domain) == 0) {
            return current; // 找到了，直接返回，不检查过期
        }
        current = current->hash_next;
    }
    
    return NULL; // 没找到
}

/**
 * @brief 向缓存添加DNS条目（分段读写锁版本）
 */
int dns_cache_put(dns_lru_cache_t* cache, const char* domain, DNS_ENTITY* response, int ttl) {
    if (!cache || !domain || !response) return MYERROR;
    
    // 获取对应的分段
    dns_cache_segment_t* segment = get_cache_segment(cache, domain);
    if (!segment) return MYERROR;
    
    // 获取写锁
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 使用内部函数查找，无论是否过期
    dns_cache_entry_t* entry = dns_cache_find_entry_internal(cache, domain);
    if (entry) {
        // --- 路径A：找到了条目（无论是有效的还是过期的），执行原地更新 ---
        log_debug("复用现有缓存槽位进行更新: %s", domain);
        
        // 1. 释放旧的DNS响应数据
        if (entry->dns_response) {
            free_dns_entity(entry->dns_response);
        }
        // 2. 更新为新的数据和过期时间
        entry->dns_response = response;
        entry->expire_time = time(NULL) + (ttl > 0 ? ttl : DEFAULT_TTL);
        entry->access_time = time(NULL);
        
        // 3. 因为被更新，所以它是最新的，移动到分段LRU头部
        lru_move_to_head_segment(segment, entry);
        
        platform_rwlock_unlock(&segment->rwlock);
        log_debug("原地更新缓存条目: %s", domain);
        return MYSUCCESS;
    }    
    
    // --- 路径B：完全没找到条目，这是一个全新的域名，执行插入 ---
    log_debug("为新域名创建缓存条目: %s", domain);
    
    dns_cache_entry_t* evicted_entry = NULL;
    
    // 如果分段已满，移除最旧的一个条目
    if (segment->current_size >= segment->max_size) {
        evicted_entry = lru_remove_tail_segment(cache, segment);
    }
    
    // 释放分段锁，访问全局内存池
    platform_rwlock_unlock(&segment->rwlock);
    
    // --- 访问全局内存池，需要使用 pool_lock ---
    platform_mutex_lock(&cache->pool_lock);
    
    if (evicted_entry) {
        // 清理被淘汰的条目并释放其DNS响应
        if (evicted_entry->dns_response) {
            free_dns_entity(evicted_entry->dns_response);
            evicted_entry->dns_response = NULL;
        }
        memset(evicted_entry, 0, sizeof(dns_cache_entry_t));
        
        // 将被淘汰条目的索引推回空闲栈
        int evicted_index = evicted_entry - cache->entry_pool;
        free_stack_push(&cache->free_stack, evicted_index);
    }
    
    // 从空闲栈获取新条目
    int free_index = free_stack_pop(&cache->free_stack);
    platform_mutex_unlock(&cache->pool_lock);
    
    if (free_index < 0) {
        log_error("无法从空闲栈获取缓存条目");
        return MYERROR;
    }
    
    dns_cache_entry_t* new_entry = &cache->entry_pool[free_index];
    
    // 填充条目
    strncpy(new_entry->domain, domain, MAX_DOMAIN_LENGTH - 1);
    new_entry->domain[MAX_DOMAIN_LENGTH - 1] = '\0';
    new_entry->dns_response = response;
    new_entry->expire_time = time(NULL) + (ttl > 0 ? ttl : DEFAULT_TTL);
    new_entry->access_time = time(NULL);
    new_entry->prev = NULL;
    new_entry->next = NULL;
    new_entry->hash_next = NULL;
    
    // 重新获取分段写锁来插入新条目
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 插入哈希表
    unsigned int hash_index = hash_domain(domain) % DNS_CACHE_HASH_SIZE;
    new_entry->hash_next = cache->hash_table[hash_index];
    cache->hash_table[hash_index] = new_entry;
    
    // 插入分段LRU链表头部
    lru_move_to_head_segment(segment, new_entry);
    segment->current_size++;
    
    platform_rwlock_unlock(&segment->rwlock);
    
    log_debug("添加新缓存条目: %s, TTL: %d, 分段当前大小: %d", domain, ttl, segment->current_size);
    return MYSUCCESS;
}

/**
 * @brief 清理过期的缓存条目（分段版本）
 */
void dns_cache_cleanup_expired(dns_lru_cache_t* cache) {
    if (!cache) return;
    
    time_t now = time(NULL);
    int total_cleaned = 0;
    
    // 遍历所有分段进行清理
    for (int seg = 0; seg < DNS_CACHE_NUM_SEGMENTS; seg++) {
        dns_cache_segment_t* segment = &cache->segments[seg];
        int cleaned = 0;
        
        platform_rwlock_wrlock(&segment->rwlock); // 需要写锁来清理
        
        // 从尾部开始清理过期条目
        dns_cache_entry_t* current = segment->lru_tail;
        while (current && now > current->expire_time) {
            dns_cache_entry_t* prev = current->prev;
            dns_cache_entry_t* expired = lru_remove_tail_segment(cache, segment);
            
            if (expired) {
                // 释放DNS响应
                if (expired->dns_response) {
                    free_dns_entity(expired->dns_response);
                    expired->dns_response = NULL;
                }
                
                // 清零条目并推回空闲栈
                memset(expired, 0, sizeof(dns_cache_entry_t));
                platform_mutex_lock(&cache->pool_lock);
                int index = expired - cache->entry_pool;
                free_stack_push(&cache->free_stack, index);
                platform_mutex_unlock(&cache->pool_lock);
                
                cleaned++;
            }
            current = prev;
        }
        
        platform_rwlock_unlock(&segment->rwlock);
        total_cleaned += cleaned;
    }
    
    if (total_cleaned > 0) {
        log_info("清理了 %d 个过期缓存条目", total_cleaned);
    }
}

/**
 * @brief 打印缓存统计信息（分段版本）
 */
void dns_cache_print_stats(dns_lru_cache_t* cache) {
    if (!cache) return;
    
    double hit_rate = 0.0;
    unsigned long total_requests = cache->cache_hits + cache->cache_misses;
    if (total_requests > 0) {
        hit_rate = (double)cache->cache_hits / total_requests * 100.0;
    }
    
    // 计算总的缓存使用量
    int total_current_size = 0;
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        total_current_size += cache->segments[i].current_size;
    }
    
    log_info("=== DNS分段式缓存统计 ===");
    log_info("当前大小: %d/%d", total_current_size, cache->max_size);
    log_info("分段数量: %d", DNS_CACHE_NUM_SEGMENTS);
    log_info("缓存命中: %lu", cache->cache_hits);
    log_info("缓存未命中: %lu", cache->cache_misses);
    log_info("缓存驱逐: %lu", cache->cache_evictions);
    log_info("命中率: %.2f%%", hit_rate);
}

/**
 * @brief 销毁DNS缓存（分段版本）
 */
void dns_cache_destroy(dns_lru_cache_t* cache) {
    if (!cache) return;
    
    // 释放所有DNS响应
    for (int i = 0; i < cache->max_size; i++) {
        if (cache->entry_pool[i].dns_response) {
            free_dns_entity(cache->entry_pool[i].dns_response);
        }
    }
    
    // 释放条目池
    if (cache->entry_pool) {
        free(cache->entry_pool);
        cache->entry_pool = NULL;
    }
    
    // 销毁空闲栈
    free_stack_destroy(&cache->free_stack);
    
    // 销毁所有分段的读写锁
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        platform_rwlock_destroy(&cache->segments[i].rwlock);
        cache->segments[i].lru_head = NULL;
        cache->segments[i].lru_tail = NULL;
        cache->segments[i].current_size = 0;
    }
    
    // 销毁内存池锁
    platform_mutex_destroy(&cache->pool_lock);
    
    // 清零哈希表
    for (int i = 0; i < DNS_CACHE_HASH_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }
    
    log_info("DNS分段式缓存已销毁");
}

// ============================================================================
// 统一查询接口实现
// ============================================================================

/**
 * @brief 统一DNS查询接口
 * 实现三级查询：本地表 -> 缓存 -> 上游DNS
 */
dns_query_response_t* dns_relay_query(const char* domain, unsigned short qtype) {
    if (!domain) return NULL;
    
    dns_query_response_t* response = (dns_query_response_t*)malloc(sizeof(dns_query_response_t));
    if (!response) return NULL;
    
    memset(response, 0, sizeof(dns_query_response_t));
    
    // 第一步：查询本地域名表
    domain_entry_t* local_entry = domain_table_lookup(&g_domain_table, domain);
    if (local_entry) {
        if (local_entry->is_blocked) {
            response->result_type = QUERY_RESULT_BLOCKED;
            log_info("域名被阻止: %s", domain);
        } else {
            response->result_type = QUERY_RESULT_LOCAL_HIT;
            strncpy(response->resolved_ip, local_entry->ip, MAX_IP_LENGTH - 1);
            log_info("本地表命中: %s -> %s", domain, local_entry->ip);
        }
        return response;
    }
    
    // 第二步：查询缓存
    dns_cache_entry_t* cache_entry = dns_cache_get(&g_dns_cache, domain);
    if (cache_entry && cache_entry->dns_response) {
        response->result_type = QUERY_RESULT_CACHE_HIT;
        response->dns_response = cache_entry->dns_response; // 注意：这里只是引用，不要释放
        log_info("缓存命中: %s", domain);
        return response;
    }
    
    // 第三步：需要查询上游DNS
    response->result_type = QUERY_RESULT_CACHE_MISS;
    log_debug("需要查询上游DNS: %s", domain);
    return response;
}

/**
 * @brief 初始化DNS中继服务
 */
int dns_relay_init(const char* domain_file) {
    log_info("初始化DNS中继服务...");
    
    // 初始化本地域名表
    if (domain_table_init(&g_domain_table) != MYSUCCESS) {
        log_error("本地域名表初始化失败");
        return MYERROR;
    }
    
    // 加载域名表文件
    if (domain_file && domain_table_load_from_file(&g_domain_table, domain_file) != MYSUCCESS) {
        log_warn("加载域名表文件失败，继续运行");
    }
    
    // 初始化DNS缓存
    if (dns_cache_init(&g_dns_cache, DNS_CACHE_SIZE) != MYSUCCESS) {
        log_error("DNS缓存初始化失败");
        domain_table_destroy(&g_domain_table);
        return MYERROR;
    }
    
    log_info("DNS中继服务初始化完成");
    return MYSUCCESS;
}

/**
 * @brief 清理DNS中继服务
 */
void dns_relay_cleanup(void) {
    log_info("清理DNS中继服务...");
    
    // 打印统计信息
    dns_cache_print_stats(&g_dns_cache);
    
    // 清理资源
    dns_cache_destroy(&g_dns_cache);
    domain_table_destroy(&g_domain_table);
    
    log_info("DNS中继服务已清理完成");
}

// ============================================================================
// 对外接口函数（用于集成到现有系统）
// ============================================================================

/**
 * @brief 向缓存添加上游DNS响应
 * 这个函数用于在收到上游DNS响应后将其添加到缓存
 */
int dns_relay_cache_response(const char* domain, DNS_ENTITY* response) {
    if (!domain || !response) return MYERROR;
    
    // 从DNS响应中提取TTL
    int ttl = DEFAULT_TTL;
    if (response->ancount > 0 && response->answers) {
        ttl = response->answers[0].ttl;
    }
    
    // 创建DNS响应的副本（因为原始响应可能会被释放）
    // 注意：这里需要深拷贝DNS_ENTITY
    // 为了简化，我们直接使用传入的response，调用者需要确保不会释放它
    
    return dns_cache_put(&g_dns_cache, domain, response, ttl);
}

/**
 * @brief 获取全局域名表和缓存的引用（用于统计）
 */
void dns_relay_get_stats(int* domain_count, int* cache_size, unsigned long* cache_hits, unsigned long* cache_misses) {
    if (domain_count) *domain_count = g_domain_table.entry_count;
    
    if (cache_size) {
        // 计算所有分段的总缓存大小
        int total_size = 0;
        for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
            total_size += g_dns_cache.segments[i].current_size;
        }
        *cache_size = total_size;
    }
    
    if (cache_hits) *cache_hits = g_dns_cache.cache_hits;
    if (cache_misses) *cache_misses = g_dns_cache.cache_misses;
}