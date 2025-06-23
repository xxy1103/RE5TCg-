#include "DNScache/relayBuild.h"
#include "DNScache/free_stack.h"
#include "websocket/websocket.h"
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
    
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->current_size = 0;
    cache->max_size = max_size;
    cache->cache_hits = 0;
    cache->cache_misses = 0;
    cache->cache_evictions = 0;
    
    log_info("DNS缓存初始化完成，容量: %d", max_size);
    return MYSUCCESS;
}

/**
 * @brief 将缓存条目移动到LRU链表头部
 */
void lru_move_to_head(dns_lru_cache_t* cache, dns_cache_entry_t* entry) {
    if (!cache || !entry) return;
    
    // 如果已经是头部，直接返回
    if (cache->lru_head == entry) return;
    
    // 从当前位置移除
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    if (cache->lru_tail == entry) {
        cache->lru_tail = entry->prev;
    }
    
    // 插入到头部
    entry->prev = NULL;
    entry->next = cache->lru_head;
    if (cache->lru_head) {
        cache->lru_head->prev = entry;
    }
    cache->lru_head = entry;
    
    // 如果链表为空，设置尾部
    if (!cache->lru_tail) {
        cache->lru_tail = entry;
    }
}

/**
 * @brief 移除LRU链表尾部条目
 */
void lru_remove_tail(dns_lru_cache_t* cache) {
    if (!cache || !cache->lru_tail) return;
    
    dns_cache_entry_t* tail = cache->lru_tail;
    
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
    
    // 从LRU链表中移除
    if (tail->prev) {
        tail->prev->next = NULL;
    }
    cache->lru_tail = tail->prev;
    
    if (cache->lru_head == tail) {
        cache->lru_head = NULL;
    }
    
    // 释放DNS响应
    if (tail->dns_response) {
        free_dns_entity(tail->dns_response);
        tail->dns_response = NULL;
    }
      // 清零条目（条目池中的内存不需要释放）
    memset(tail, 0, sizeof(dns_cache_entry_t));
    
    // 将索引推回空闲栈
    int index = tail - cache->entry_pool;
    if (free_stack_push(&cache->free_stack, index) != 0) {
        log_error("将条目索引推回空闲栈失败: %d", index);
    }
    
    cache->current_size--;
    cache->cache_evictions++;
    
    log_debug("LRU缓存移除尾部条目，当前大小: %d", cache->current_size);
}

/**
 * @brief 从缓存获取DNS条目
 */
dns_cache_entry_t* dns_cache_get(dns_lru_cache_t* cache, const char* domain) {
    if (!cache || !domain) return NULL;
    
    unsigned int hash_index = hash_domain(domain) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = cache->hash_table[hash_index];
    
    while (current) {
        if (strcasecmp(current->domain, domain) == 0) {
            // 检查是否过期
            time_t now = time(NULL);
            if (now > current->expire_time) {
                log_debug("缓存条目已过期: %s", domain);
                cache->cache_misses++;
                return NULL;
            }
            
            // 更新访问时间并移动到头部
            current->access_time = now;
            lru_move_to_head(cache, current);
            cache->cache_hits++;
            
            log_debug("缓存命中: %s", domain);
            return current;
        }
        current = current->hash_next;
    }
    
    cache->cache_misses++;
    log_debug("缓存未命中: %s", domain);
    return NULL;
}

/**
 * @brief 向缓存添加DNS条目
 */
int dns_cache_put(dns_lru_cache_t* cache, const char* domain, DNS_ENTITY* response, int ttl) {
    if (!cache || !domain || !response) return MYERROR;
    
    // 检查是否已存在
    dns_cache_entry_t* existing = dns_cache_get(cache, domain);
    if (existing) {
        // 更新现有条目
        if (existing->dns_response) {
            free_dns_entity(existing->dns_response);
        }
        existing->dns_response = response; // 直接使用传入的响应
        existing->expire_time = time(NULL) + (ttl > 0 ? ttl : DEFAULT_TTL);
        existing->access_time = time(NULL);
        
        log_debug("更新缓存条目: %s", domain);
        return MYSUCCESS;
    }
    
    // 如果缓存已满，移除最旧的一个条目
    if (cache->current_size >= cache->max_size) {
        lru_remove_tail(cache);
    }
      // 查找空闲条目
    dns_cache_entry_t* entry = NULL;
    int free_index = free_stack_pop(&cache->free_stack);
    if (free_index >= 0) {
        entry = &cache->entry_pool[free_index];
    }
    
    if (!entry) {
        log_error("无法从空闲栈获取缓存条目");
        return MYERROR;
    }
    
    // 填充条目
    strncpy(entry->domain, domain, MAX_DOMAIN_LENGTH - 1);
    entry->domain[MAX_DOMAIN_LENGTH - 1] = '\0';
    entry->dns_response = response; // 直接使用传入的响应
    entry->expire_time = time(NULL) + (ttl > 0 ? ttl : DEFAULT_TTL);
    entry->access_time = time(NULL);
    
    // 插入哈希表
    unsigned int hash_index = hash_domain(domain) % DNS_CACHE_HASH_SIZE;
    entry->hash_next = cache->hash_table[hash_index];
    cache->hash_table[hash_index] = entry;
    
    // 插入LRU链表头部
    lru_move_to_head(cache, entry);
    cache->current_size++;
    
    log_debug("添加缓存条目: %s, TTL: %d, 当前大小: %d", domain, ttl, cache->current_size);
    return MYSUCCESS;
}

/**
 * @brief 清理过期的缓存条目
 */
void dns_cache_cleanup_expired(dns_lru_cache_t* cache) {
    if (!cache) return;
    
    time_t now = time(NULL);
    int cleaned = 0;
    
    // 从尾部开始清理过期条目
    dns_cache_entry_t* current = cache->lru_tail;
    while (current && now > current->expire_time) {
        dns_cache_entry_t* prev = current->prev;
        lru_remove_tail(cache);
        cleaned++;
        current = prev;
    }
    
    if (cleaned > 0) {
        log_info("清理了 %d 个过期缓存条目", cleaned);
    }
}

/**
 * @brief 打印缓存统计信息
 */
void dns_cache_print_stats(dns_lru_cache_t* cache) {
    if (!cache) return;
    
    double hit_rate = 0.0;
    unsigned long total_requests = cache->cache_hits + cache->cache_misses;
    if (total_requests > 0) {
        hit_rate = (double)cache->cache_hits / total_requests * 100.0;
    }
    
    log_info("=== DNS缓存统计 ===");
    log_info("当前大小: %d/%d", cache->current_size, cache->max_size);
    log_info("缓存命中: %lu", cache->cache_hits);
    log_info("缓存未命中: %lu", cache->cache_misses);
    log_info("缓存驱逐: %lu", cache->cache_evictions);
    log_info("命中率: %.2f%%", hit_rate);
}

/**
 * @brief 销毁DNS缓存
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
    
    // 清零哈希表
    for (int i = 0; i < DNS_CACHE_HASH_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }
    
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->current_size = 0;
    
    log_info("DNS缓存已销毁");
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
    if (cache_size) *cache_size = g_dns_cache.current_size;
    if (cache_hits) *cache_hits = g_dns_cache.cache_hits;
    if (cache_misses) *cache_misses = g_dns_cache.cache_misses;
}