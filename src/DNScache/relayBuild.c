#include "DNScache/relayBuild.h"
#include "DNScache/free_stack.h"
#include "websocket/websocket.h"
#include "platform/platform.h"
#include "websocket/datagram.h"
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
 * @brief 缓存键哈希函数
 * 使用djb2算法计算缓存键的哈希值
 */
unsigned int hash_key(const char* key) {
    if (!key) return 0;
    
    unsigned int hash = 5381;
    int c;
    
    // 转换为小写并计算哈希
    while ((c = *key++)) {
        if (c >= 'A' && c <= 'Z') {
            c += 32; // 转换为小写
        }
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash;
}

/**
 * @brief 域名哈希函数（用于域名表）
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
 * @brief 根据缓存键获取其所属的缓存段
 */
dns_cache_segment_t* get_cache_segment(const char* key) {
    if (!key) return NULL;
    
    unsigned int hash = hash_key(key);
    // 使用位运算快速取模，前提是段数量为2的幂
    return &g_dns_cache.segments[hash & (DNS_CACHE_NUM_SEGMENTS - 1)];
}

/**
 * @brief 根据域名获取其所属的域名表分段
 */
domain_table_segment_t* get_domain_table_segment(const char* domain) {
    if (!domain) return NULL;
    
    unsigned int hash = hash_domain(domain);
    // 使用位运算快速取模，前提是段数量为2的幂
    return &g_domain_table.segments[hash & (DOMAIN_TABLE_NUM_SEGMENTS - 1)];
}

// ============================================================================
// 本地域名表实现
// ============================================================================

/**
 * @brief 初始化本地域名表（分段版本）
 */
int domain_table_init() {
    // 初始化所有分段
    for (int i = 0; i < DOMAIN_TABLE_NUM_SEGMENTS; i++) {
        // 初始化读写锁
        if (platform_rwlock_init(&g_domain_table.segments[i].rwlock, NULL) != 0) {
            log_error("域名表分段读写锁初始化失败: %d", i);
            // 清理已初始化的锁
            for (int j = 0; j < i; j++) {
                platform_rwlock_destroy(&g_domain_table.segments[j].rwlock);
            }
            return MYERROR;
        }
        
        // 清零该分段的哈希桶
        int buckets_per_segment = DOMAIN_TABLE_HASH_SIZE / DOMAIN_TABLE_NUM_SEGMENTS;
        for (int k = 0; k < buckets_per_segment; k++) {
            g_domain_table.segments[i].hash_buckets[k] = NULL;
        }
        
        g_domain_table.segments[i].entry_count = 0;
    }
    
    g_domain_table.total_entry_count = 0;
    g_domain_table.last_load_time = 0;
    
    log_info("本地域名表分段初始化完成，分段数: %d", DOMAIN_TABLE_NUM_SEGMENTS);
    return MYSUCCESS;
}

/**
 * @brief 从文件加载域名表（分段版本，支持IPv4和IPv6）
 */
int domain_table_load_from_file(const char* filename) {
    if (!filename) return MYERROR;
    
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
        
        // 判断IP地址类型
        unsigned short ip_type;
        int is_blocked = 0;
        if (strchr(ip, ':') != NULL) {
            // IPv6地址
            ip_type = AAAA;
            is_blocked = (strcmp(ip, "::") == 0);
        } else {
            // IPv4地址
            ip_type = A;
            is_blocked = (strcmp(ip, "0.0.0.0") == 0);
        }
        
        // 获取对应的分段
        domain_table_segment_t* segment = get_domain_table_segment(domain);
        if (!segment) {
            continue;
        }
        
        // 获取写锁并查找或创建域名条目
        platform_rwlock_wrlock(&segment->rwlock);
        
        // 计算在分段内的哈希索引
        unsigned int global_hash = hash_domain(domain);
        int buckets_per_segment = DOMAIN_TABLE_HASH_SIZE / DOMAIN_TABLE_NUM_SEGMENTS;
        unsigned int bucket_index = (global_hash / DOMAIN_TABLE_NUM_SEGMENTS) % buckets_per_segment;
        
        // 查找现有的域名条目
        domain_entry_t* domain_entry = NULL;
        domain_entry_t* current = segment->hash_buckets[bucket_index];
        while (current) {
            if (strcasecmp(current->domain, domain) == 0) {
                domain_entry = current;
                break;
            }
            current = current->next;
        }
        
        // 如果没有找到域名条目，创建新的
        if (!domain_entry) {
            domain_entry = (domain_entry_t*)malloc(sizeof(domain_entry_t));
            if (!domain_entry) {
                log_error("内存分配失败");
                platform_rwlock_unlock(&segment->rwlock);
                continue;
            }
            
            // 初始化域名条目
            strncpy(domain_entry->domain, domain, MAX_DOMAIN_LENGTH - 1);
            domain_entry->domain[MAX_DOMAIN_LENGTH - 1] = '\0';
            domain_entry->ips = NULL;
            domain_entry->is_blocked = 0;
            
            // 插入到链表头部
            domain_entry->next = segment->hash_buckets[bucket_index];
            segment->hash_buckets[bucket_index] = domain_entry;
            segment->entry_count++;
        }
        
        // 创建IP地址条目
        ip_address_entry_t* ip_entry = (ip_address_entry_t*)malloc(sizeof(ip_address_entry_t));
        if (!ip_entry) {
            log_error("IP地址条目内存分配失败");
            platform_rwlock_unlock(&segment->rwlock);
            continue;
        }
        
        ip_entry->type = ip_type;
        strncpy(ip_entry->ip, ip, MAX_IP_LENGTH - 1);
        ip_entry->ip[MAX_IP_LENGTH - 1] = '\0';
        
        // 将IP条目插入到域名条目的IP链表头部
        ip_entry->next = domain_entry->ips;
        domain_entry->ips = ip_entry;
        
        // 如果有任何一个IP地址被阻止，则整个域名被标记为阻止
        if (is_blocked) {
            domain_entry->is_blocked = 1;
        }
        
        platform_rwlock_unlock(&segment->rwlock);
        loaded_count++;
    }
    
    fclose(file);
    g_domain_table.total_entry_count = loaded_count;
    g_domain_table.last_load_time = time(NULL);
    
    log_info("成功加载 %d 个IP地址条目到分段域名表", loaded_count);
    return MYSUCCESS;
}

/**
 * @brief 查找域名表条目（分段读写锁，支持查询类型）
 */
ip_address_entry_t* domain_table_lookup(const char* domain, unsigned short qtype) {
    if (!domain) return NULL;
    
    // 获取对应的分段
    domain_table_segment_t* segment = get_domain_table_segment(domain);
    if (!segment) return NULL;
    
    ip_address_entry_t* result = NULL;
    
    // 获取读锁（多线程可以并发读取不同分段）
    platform_rwlock_rdlock(&segment->rwlock);
    
    // 计算在分段内的哈希索引
    unsigned int global_hash = hash_domain(domain);
    int buckets_per_segment = DOMAIN_TABLE_HASH_SIZE / DOMAIN_TABLE_NUM_SEGMENTS;
    unsigned int bucket_index = (global_hash / DOMAIN_TABLE_NUM_SEGMENTS) % buckets_per_segment;
    
    // 在分段的哈希桶中查找域名条目
    domain_entry_t* domain_entry = segment->hash_buckets[bucket_index];
    while (domain_entry) {
        // 不区分大小写比较域名
        if (strcasecmp(domain_entry->domain, domain) == 0) {
            // 找到域名条目，现在在其IP链表中查找匹配的查询类型
            ip_address_entry_t* ip_entry = domain_entry->ips;
            while (ip_entry) {
                if (ip_entry->type == qtype) {
                    result = ip_entry;
                    break;
                }
                ip_entry = ip_entry->next;
            }
            break;
        }
        domain_entry = domain_entry->next;
    }
    
    platform_rwlock_unlock(&segment->rwlock);
    return result;
}

/**
 * @brief 销毁域名表（分段，支持IP链表）
 */
void domain_table_destroy() {
    // 释放所有分段的内容并销毁锁
    for (int i = 0; i < DOMAIN_TABLE_NUM_SEGMENTS; i++) {
        domain_table_segment_t* segment = &g_domain_table.segments[i];
        
        // 获取写锁以安全地销毁内容
        platform_rwlock_wrlock(&segment->rwlock);
        
        // 释放该分段所有哈希桶中的条目
        int buckets_per_segment = DOMAIN_TABLE_HASH_SIZE / DOMAIN_TABLE_NUM_SEGMENTS;
        for (int j = 0; j < buckets_per_segment; j++) {
            domain_entry_t* current = segment->hash_buckets[j];
            while (current) {
                domain_entry_t* next_domain = current->next;
                
                // 释放该域名条目的所有IP地址条目
                ip_address_entry_t* ip_entry = current->ips;
                while (ip_entry) {
                    ip_address_entry_t* next_ip = ip_entry->next;
                    free(ip_entry);
                    ip_entry = next_ip;
                }
                
                // 释放域名条目
                free(current);
                current = next_domain;
            }
            segment->hash_buckets[j] = NULL;
        }
        
        segment->entry_count = 0;
        platform_rwlock_unlock(&segment->rwlock);
        
        // 销毁读写锁
        platform_rwlock_destroy(&segment->rwlock);
    }
    
    g_domain_table.total_entry_count = 0;
    log_info("分段本地域名表已销毁");
}

// ============================================================================
// LRU缓存实现
// ============================================================================

/**
 * @brief 初始化DNS缓存
 */
int dns_cache_init(int max_size) {
    if (max_size <= 0) return MYERROR;
    
    // 清零哈希表
    for (int i = 0; i < DNS_CACHE_HASH_SIZE; i++) { //ok
        g_dns_cache.hash_table[i] = NULL;
    }
    
    // 预分配条目池
    g_dns_cache.entry_pool = (dns_cache_entry_t*)calloc(max_size, sizeof(dns_cache_entry_t));
    if (!g_dns_cache.entry_pool) {
        log_error("DNS缓存条目池内存分配失败");
        return MYERROR;
    }
    
    // 初始化空闲栈
    if (free_stack_init(&g_dns_cache.free_stack, max_size) != 0) {
        log_error("空闲栈初始化失败");
        free(g_dns_cache.entry_pool);
        return MYERROR;
    }
    
    // 初始化内存池锁
    if (platform_mutex_init(&g_dns_cache.pool_lock, NULL) != 0) {
        log_error("内存池锁初始化失败");
        free_stack_destroy(&g_dns_cache.free_stack);
        free(g_dns_cache.entry_pool);
        return MYERROR;
    }
    
    // 初始化所有分段
    int segment_max_size = max_size / DNS_CACHE_NUM_SEGMENTS;
    if (segment_max_size <= 0) segment_max_size = 1; // 至少每段一个条目
    
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        if (platform_rwlock_init(&g_dns_cache.segments[i].rwlock, NULL) != 0) {
            log_error("分段读写锁初始化失败: %d", i);
            // 清理已初始化的锁
            for (int j = 0; j < i; j++) {
                platform_rwlock_destroy(&g_dns_cache.segments[j].rwlock);
            }
            platform_mutex_destroy(&g_dns_cache.pool_lock);
            free_stack_destroy(&g_dns_cache.free_stack);
            free(g_dns_cache.entry_pool);
            return MYERROR;
        }
        g_dns_cache.segments[i].lru_head = NULL;
        g_dns_cache.segments[i].lru_tail = NULL;
        g_dns_cache.segments[i].current_size = 0;
        g_dns_cache.segments[i].max_size = segment_max_size;
    }
    
    g_dns_cache.max_size = max_size;
    g_dns_cache.cache_hits = 0;
    g_dns_cache.cache_misses = 0;
    g_dns_cache.cache_evictions = 0;
    
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
dns_cache_entry_t* lru_remove_tail_segment(dns_cache_segment_t* segment) {
    if (!segment || !segment->lru_tail) return NULL;
    
    dns_cache_entry_t* tail = segment->lru_tail;
    
    // 从哈希表中移除
    unsigned int hash_index = hash_key(tail->key) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = g_dns_cache.hash_table[hash_index];
    dns_cache_entry_t* prev = NULL;
    
    while (current) {
        if (current == tail) {
            if (prev) {
                prev->hash_next = current->hash_next;
            } else {
                g_dns_cache.hash_table[hash_index] = current->hash_next;
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
    g_dns_cache.cache_evictions++;
    
    log_debug("分段LRU缓存移除尾部条目: %s, 分段当前大小: %d", tail->key, segment->current_size);
    return tail;
}

/**
 * @brief 从缓存获取DNS条目（分段读写锁版本，支持查询类型）
 */
dns_cache_entry_t* dns_cache_get(const char* domain, unsigned short qtype) {
    if (!domain) return NULL;
    
    // 生成缓存键
    char cache_key[MAX_CACHE_KEY_LENGTH];
    snprintf(cache_key, sizeof(cache_key), "%s:%u", domain, qtype);
    
    // 获取对应的分段
    dns_cache_segment_t* segment = get_cache_segment(cache_key);
    if (!segment) return NULL;
    
    dns_cache_entry_t* result = NULL;
    
    // 获取读锁
    platform_rwlock_rdlock(&segment->rwlock);
    
    unsigned int hash_index = hash_key(cache_key) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = g_dns_cache.hash_table[hash_index];
    
    while (current) {
        if (strcasecmp(current->key, cache_key) == 0) {
            // 检查是否过期
            time_t now = time(NULL);
            if (now > current->expire_time) {
                log_debug("缓存条目已过期: %s", cache_key);
                g_dns_cache.cache_misses++;
                break; // 过期了，退出循环
            }
            
            // 命中了，但需要升级为写锁来更新LRU
            platform_rwlock_unlock(&segment->rwlock);
            platform_rwlock_wrlock(&segment->rwlock);
            
            // 再次检查（因为在锁切换期间可能被其他线程修改）
            if (now <= current->expire_time && strcasecmp(current->key, cache_key) == 0) {
                // 更新访问时间并移动到头部
                current->access_time = now;
                lru_move_to_head_segment(segment, current);
                g_dns_cache.cache_hits++;
                result = current;
                log_debug("缓存命中: %s", cache_key);
            } else {
                g_dns_cache.cache_misses++;
            }
            break;
        }
        current = current->hash_next;
    }
    
    if (!result) {
        g_dns_cache.cache_misses++;
        log_debug("缓存未命中: %s", cache_key);
    }
    
    platform_rwlock_unlock(&segment->rwlock);
    return result;
}

/**
 * @brief 内部函数：查找缓存条目（不检查过期，不移动LRU）
 * 仅用于内部逻辑，根据缓存键查找对应的缓存条目
 */
static dns_cache_entry_t* dns_cache_find_entry_internal(const char* cache_key) {
    if (!cache_key) return NULL;
    
    unsigned int hash_index = hash_key(cache_key) % DNS_CACHE_HASH_SIZE;
    dns_cache_entry_t* current = g_dns_cache.hash_table[hash_index];
    
    while (current) {
        if (strcasecmp(current->key, cache_key) == 0) {
            return current; // 找到了，直接返回，不检查过期
        }
        current = current->hash_next;
    }
    
    return NULL; // 没找到
}

/**
 * @brief 向缓存添加DNS条目（分段读写锁版本，支持查询类型）
 */
int dns_cache_put(const char* domain, unsigned short qtype, DNS_ENTITY* response, int ttl) {
    if (!domain || !response) return MYERROR;
    
    // 生成缓存键
    char cache_key[MAX_CACHE_KEY_LENGTH];
    snprintf(cache_key, sizeof(cache_key), "%s:%u", domain, qtype);
    
    // 获取对应的分段
    dns_cache_segment_t* segment = get_cache_segment(cache_key);
    if (!segment) return MYERROR;
    
    // 获取写锁
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 使用内部函数查找，无论是否过期
    dns_cache_entry_t* entry = dns_cache_find_entry_internal(cache_key);
    if (entry) {
        // --- 路径A：找到了条目（无论是有效的还是过期的），执行原地更新 ---
        log_debug("复用现有缓存槽位进行更新: %s", cache_key);
        
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
        log_debug("原地更新缓存条目: %s", cache_key);
        return MYSUCCESS;
    }    
    
    // --- 路径B：完全没找到条目，这是一个全新的缓存键，执行插入 ---
    log_debug("为新缓存键创建缓存条目: %s", cache_key);
    
    dns_cache_entry_t* evicted_entry = NULL;
    
    // 如果分段已满，移除最旧的一个条目
    if (segment->current_size >= segment->max_size) {
        evicted_entry = lru_remove_tail_segment(segment);
    }
    
    // 释放分段锁，访问全局内存池
    platform_rwlock_unlock(&segment->rwlock);
    
    // --- 访问全局内存池，需要使用 pool_lock ---
    platform_mutex_lock(&g_dns_cache.pool_lock);
    
    if (evicted_entry) {
        // 清理被淘汰的条目并释放其DNS响应
        if (evicted_entry->dns_response) {
            free_dns_entity(evicted_entry->dns_response);
            evicted_entry->dns_response = NULL;
        }
        memset(evicted_entry, 0, sizeof(dns_cache_entry_t));
        
        // 将被淘汰条目的索引推回空闲栈
        int evicted_index = evicted_entry - g_dns_cache.entry_pool;
        free_stack_push(&g_dns_cache.free_stack, evicted_index);
    }
    
    // 从空闲栈获取新条目
    int free_index = free_stack_pop(&g_dns_cache.free_stack);
    platform_mutex_unlock(&g_dns_cache.pool_lock);
    
    if (free_index < 0) {
        log_error("无法从空闲栈获取缓存条目");
        return MYERROR;
    }
    
    dns_cache_entry_t* new_entry = &g_dns_cache.entry_pool[free_index];
    
    // 填充条目
    strncpy(new_entry->key, cache_key, MAX_CACHE_KEY_LENGTH - 1);
    new_entry->key[MAX_CACHE_KEY_LENGTH - 1] = '\0';
    new_entry->dns_response = response;
    new_entry->expire_time = time(NULL) + (ttl > 0 ? ttl : DEFAULT_TTL);
    new_entry->access_time = time(NULL);
    new_entry->prev = NULL;
    new_entry->next = NULL;
    new_entry->hash_next = NULL;
    
    // 重新获取分段写锁来插入新条目
    platform_rwlock_wrlock(&segment->rwlock);
    
    // 插入哈希表
    unsigned int hash_index = hash_key(cache_key) % DNS_CACHE_HASH_SIZE;
    new_entry->hash_next = g_dns_cache.hash_table[hash_index];
    g_dns_cache.hash_table[hash_index] = new_entry;
    
    // 插入分段LRU链表头部
    lru_move_to_head_segment(segment, new_entry);
    segment->current_size++;
    
    platform_rwlock_unlock(&segment->rwlock);
    
    log_debug("添加新缓存条目: %s, TTL: %d, 分段当前大小: %d", cache_key, ttl, segment->current_size);
    return MYSUCCESS;
}

/**
 * @brief 清理过期的缓存条目（分段版本）
 */
void dns_cache_cleanup_expired() {
    if (!&g_dns_cache) return;
    
    time_t now = time(NULL);
    int total_cleaned = 0;
    
    // 遍历所有分段进行清理
    for (int seg = 0; seg < DNS_CACHE_NUM_SEGMENTS; seg++) {
        dns_cache_segment_t* segment = &g_dns_cache.segments[seg];
        int cleaned = 0;
        
        platform_rwlock_wrlock(&segment->rwlock); // 需要写锁来清理
        
        // 从尾部开始清理过期条目
        dns_cache_entry_t* current = segment->lru_tail;
        while (current && now > current->expire_time) {
            dns_cache_entry_t* prev = current->prev;
            dns_cache_entry_t* expired = lru_remove_tail_segment(segment);
            
            if (expired) {
                // 释放DNS响应
                if (expired->dns_response) {
                    free_dns_entity(expired->dns_response);
                    expired->dns_response = NULL;
                }
                
                // 清零条目并推回空闲栈
                memset(expired, 0, sizeof(dns_cache_entry_t));
                platform_mutex_lock(&g_dns_cache.pool_lock);
                int index = expired - g_dns_cache.entry_pool;
                free_stack_push(&g_dns_cache.free_stack, index);
                platform_mutex_unlock(&g_dns_cache.pool_lock);
                
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
void dns_cache_print_stats() {
    if (!&g_dns_cache) return;
    
    double hit_rate = 0.0;
    unsigned long total_requests = g_dns_cache.cache_hits + g_dns_cache.cache_misses;
    if (total_requests > 0) {
        hit_rate = (double)g_dns_cache.cache_hits / total_requests * 100.0;
    }
    
    // 计算总的缓存使用量
    int total_current_size = 0;
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        total_current_size += g_dns_cache.segments[i].current_size;
    }
    
    log_info("=== DNS分段式缓存统计 ===");
    log_info("当前大小: %d/%d", total_current_size, g_dns_cache.max_size);
    log_info("分段数量: %d", DNS_CACHE_NUM_SEGMENTS);
    log_info("缓存命中: %lu", g_dns_cache.cache_hits);
    log_info("缓存未命中: %lu", g_dns_cache.cache_misses);
    log_info("缓存驱逐: %lu", g_dns_cache.cache_evictions);
    log_info("命中率: %.2f%%", hit_rate);
}

/**
 * @brief 销毁DNS缓存（分段版本）
 */
void dns_cache_destroy() {
    if (!&g_dns_cache) return;
    
    // 释放所有DNS响应
    for (int i = 0; i < g_dns_cache.max_size; i++) {
        if (g_dns_cache.entry_pool[i].dns_response) {
            free_dns_entity(g_dns_cache.entry_pool[i].dns_response);
        }
    }
    
    // 释放条目池
    if (g_dns_cache.entry_pool) {
        free(g_dns_cache.entry_pool);
        g_dns_cache.entry_pool = NULL;
    }
    
    // 销毁空闲栈
    free_stack_destroy(&g_dns_cache.free_stack);
    
    // 销毁所有分段的读写锁
    for (int i = 0; i < DNS_CACHE_NUM_SEGMENTS; i++) {
        platform_rwlock_destroy(&g_dns_cache.segments[i].rwlock);
        g_dns_cache.segments[i].lru_head = NULL;
        g_dns_cache.segments[i].lru_tail = NULL;
        g_dns_cache.segments[i].current_size = 0;
    }
    
    // 销毁内存池锁
    platform_mutex_destroy(&g_dns_cache.pool_lock);
    
    // 清零哈希表
    for (int i = 0; i < DNS_CACHE_HASH_SIZE; i++) {
        g_dns_cache.hash_table[i] = NULL;
    }
    
    log_info("DNS分段式缓存已销毁");
}
// ============================================================================
// 对外接口函数（用于集成到现有系统）
// ============================================================================


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
    ip_address_entry_t* local_entry = domain_table_lookup(domain, qtype);
    if (local_entry) {
        // 检查是否为阻止地址
        int is_blocked = 0;
        if (qtype == A && strcmp(local_entry->ip, "0.0.0.0") == 0) {
            is_blocked = 1;
        } else if (qtype == AAAA && strcmp(local_entry->ip, "::") == 0) {
            is_blocked = 1;
        }
        
        if (is_blocked) {
            response->result_type = QUERY_RESULT_BLOCKED;
            log_info("域名被阻止: %s (type:%u)", domain, qtype);
        } else {
            response->result_type = QUERY_RESULT_LOCAL_HIT;
            strncpy(response->resolved_ip, local_entry->ip, MAX_IP_LENGTH - 1);
            log_info("本地表命中: %s (type:%u) -> %s", domain, qtype, local_entry->ip);
        }
        
        return response;
    }
    
    // 第二步：查询缓存
    dns_cache_entry_t* cache_entry = dns_cache_get(domain, qtype);
    if (cache_entry && cache_entry->dns_response) {
        response->result_type = QUERY_RESULT_CACHE_HIT;
        response->dns_response = cache_entry->dns_response; // 注意：这里只是引用，不要释放
        log_info("缓存命中: %s (type:%u)", domain, qtype);
        return response;
    }
    
    // 第三步：需要查询上游DNS
    response->result_type = QUERY_RESULT_CACHE_MISS;
    log_debug("需要查询上游DNS: %s (type:%u)", domain, qtype);
    return response;
}

/**
 * @brief 初始化DNScache缓存和本地查询表
 */
int dns_relay_init(const char* domain_file) {
    log_info("初始化DNS中继服务...");
    
    // 初始化本地域名表
    if (domain_table_init() != MYSUCCESS) {
        log_error("本地域名表初始化失败");
        return MYERROR;
    }
    
    // 加载域名表文件
    if (domain_file && domain_table_load_from_file(domain_file) != MYSUCCESS) {
        log_warn("加载域名表文件失败，继续运行");
    }
    
    // 初始化DNS缓存
    if (dns_cache_init(DNS_CACHE_SIZE) != MYSUCCESS) {
        log_error("DNS缓存初始化失败");
        domain_table_destroy();
        return MYERROR;
    }
    
    log_info("DNS中继服务初始化完成");
    return MYSUCCESS;
}

/**
 * @brief 清理DNScache缓存和本地查询表
 */
void dns_relay_cleanup(void) {
    log_info("清理DNS中继服务...");
    
    // 打印统计信息
    dns_cache_print_stats();
    
    // 清理资源
    dns_cache_destroy();
    domain_table_destroy();
    
    log_info("DNS中继服务已清理完成");
}


/**
 * @brief 向缓存添加上游DNS响应
 * 这个函数用于在收到上游DNS响应后将其添加到缓存
 */
int dns_relay_cache_response(const char* domain, unsigned short qtype, DNS_ENTITY* response) {
    if (!domain || !response) return MYERROR;
    
    // 从DNS响应中提取TTL
    int ttl = DEFAULT_TTL;
    if (response->ancount > 0 && response->answers) {
        ttl = response->answers[0].ttl;
    }
    
    return dns_cache_put(domain, qtype, response, ttl);
}

/**
 * @brief 获取全局域名表和缓存的引用（用于统计）
 */
void dns_relay_get_stats(int* domain_count, int* cache_size, unsigned long* cache_hits, unsigned long* cache_misses) {
    if (domain_count) *domain_count = g_domain_table.total_entry_count;
    
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