#ifndef RELAYBUILD_H
#define RELAYBUILD_H

#include "websocket/datagram.h"
#include "debug/debug.h"
#include "DNScache/free_stack.h"
#include "platform/platform.h"
#include <time.h>
#include <string.h>

// ============================================================================
// 本地域名表相关定义
// ============================================================================

#define MAX_DOMAIN_LENGTH 256
#define MAX_IP_LENGTH 16
#define DOMAIN_TABLE_HASH_SIZE 4096

// 本地域名表条目
typedef struct domain_entry {
    char domain[MAX_DOMAIN_LENGTH];     // 域名
    char ip[MAX_IP_LENGTH];             // IP地址
    int is_blocked;                     // 是否被阻止（0.0.0.0标记）
    struct domain_entry* next;          // 哈希冲突链表
} domain_entry_t;

// 本地域名表
typedef struct {
    domain_entry_t* hash_table[DOMAIN_TABLE_HASH_SIZE];
    int entry_count;
    time_t last_load_time;
} domain_table_t;

// ============================================================================
// LRU缓存相关定义
// ============================================================================

#define DNS_CACHE_SIZE 1000             // 缓存容量
#define DNS_CACHE_HASH_SIZE 2048        // 哈希表大小
#define DEFAULT_TTL 300                 // 默认TTL（5分钟）
#define DNS_CACHE_NUM_SEGMENTS 64       // 分段数量，必须是2的幂

// DNS缓存条目
typedef struct dns_cache_entry {
    char domain[MAX_DOMAIN_LENGTH];     // 域名
    DNS_ENTITY* dns_response;           // 完整的DNS响应
    time_t expire_time;                 // 过期时间
    time_t access_time;                 // 最后访问时间
    
    // LRU双向链表
    struct dns_cache_entry* prev;
    struct dns_cache_entry* next;
    
    // 哈希表链表
    struct dns_cache_entry* hash_next;
} dns_cache_entry_t;

// DNS缓存分段结构
typedef struct {
    pthread_rwlock_t rwlock;            // 读写锁保护该段
    dns_cache_entry_t* lru_head;        // 该段的LRU链表头（最新）
    dns_cache_entry_t* lru_tail;        // 该段的LRU链表尾（最旧）
    int current_size;                   // 该段当前缓存大小
    int max_size;                       // 该段最大缓存大小
} dns_cache_segment_t;

// LRU缓存管理器
typedef struct {
    dns_cache_entry_t* hash_table[DNS_CACHE_HASH_SIZE];
    dns_cache_entry_t* entry_pool;      // 预分配的条目池
    free_stack_t free_stack;            // 空闲条目栈
    pthread_mutex_t pool_lock;          // 保护内存池的互斥锁
    
    // 分段锁
    dns_cache_segment_t segments[DNS_CACHE_NUM_SEGMENTS];
    
    int max_size;                       // 最大缓存大小
    
    // 统计信息（使用原子操作或单独锁保护）
    unsigned long cache_hits;
    unsigned long cache_misses;
    unsigned long cache_evictions;
} dns_lru_cache_t;

// ============================================================================
// 查询结果枚举
// ============================================================================

typedef enum {
    QUERY_RESULT_BLOCKED,       // 域名被阻止
    QUERY_RESULT_LOCAL_HIT,     // 本地表命中
    QUERY_RESULT_CACHE_HIT,     // 缓存命中
    QUERY_RESULT_CACHE_MISS,    // 缓存未命中，需要上游查询
    QUERY_RESULT_ERROR          // 查询错误
} dns_query_result_t;

// 查询结果结构
typedef struct {
    dns_query_result_t result_type;
    DNS_ENTITY* dns_response;           // DNS响应（如果有）
    char resolved_ip[MAX_IP_LENGTH];    // 解析的IP地址
} dns_query_response_t;

// ============================================================================
// 函数声明
// ============================================================================

// 本地域名表管理
int domain_table_init(domain_table_t* table);
int domain_table_load_from_file(domain_table_t* table, const char* filename);
domain_entry_t* domain_table_lookup(domain_table_t* table, const char* domain);
void domain_table_destroy(domain_table_t* table);

// LRU缓存管理
int dns_cache_init(dns_lru_cache_t* cache, int max_size);
dns_cache_entry_t* dns_cache_get(dns_lru_cache_t* cache, const char* domain);
int dns_cache_put(dns_lru_cache_t* cache, const char* domain, DNS_ENTITY* response, int ttl);
void dns_cache_cleanup_expired(dns_lru_cache_t* cache);
void dns_cache_print_stats(dns_lru_cache_t* cache);
void dns_cache_destroy(dns_lru_cache_t* cache);

// 统一查询接口
dns_query_response_t* dns_relay_query(const char* domain, unsigned short qtype);
int dns_relay_init(const char* domain_file);
void dns_relay_cleanup(void);

// 内部辅助函数声明
unsigned int hash_domain(const char* domain);
dns_cache_segment_t* get_cache_segment(dns_lru_cache_t* cache, const char* domain);
void lru_move_to_head_segment(dns_cache_segment_t* segment, dns_cache_entry_t* entry);
dns_cache_entry_t* lru_remove_tail_segment(dns_lru_cache_t* cache, dns_cache_segment_t* segment);

#endif // RELAYBUILD_H


