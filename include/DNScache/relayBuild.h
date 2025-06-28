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
#define MAX_IP_LENGTH 46                // 扩展以支持IPv6地址
#define DOMAIN_TABLE_HASH_SIZE 16384     // 优化：16K哈希桶，平衡内存和性能
#define DOMAIN_TABLE_NUM_SEGMENTS 64     // 优化：64个分段，适合多核CPU

// IP地址条目，支持IPv4和IPv6
typedef struct ip_address_entry {
    unsigned short type;                // 查询类型 (e.g., T_A, T_AAAA)
    char ip[MAX_IP_LENGTH];             // IP地址字符串
    struct ip_address_entry* next;      // 指向下一个IP地址
} ip_address_entry_t;

// 本地域名表条目
typedef struct domain_entry {
    char domain[MAX_DOMAIN_LENGTH];     // 域名
    ip_address_entry_t* ips;            // IP地址链表
    int is_blocked;                     // 是否被阻止（0.0.0.0或::标记）
    struct domain_entry* next;          // 哈希冲突链表
} domain_entry_t;

// 域名表分段结构
typedef struct {
    pthread_rwlock_t rwlock;            // 保护该段的读写锁
    domain_entry_t* hash_buckets[DOMAIN_TABLE_HASH_SIZE / DOMAIN_TABLE_NUM_SEGMENTS]; // 该段的哈希桶
    int entry_count;                    // 该段的条目数量
} domain_table_segment_t;

// 本地域名表
typedef struct {
    domain_table_segment_t segments[DOMAIN_TABLE_NUM_SEGMENTS]; // 分段数组
    int total_entry_count;              // 总条目数量
    time_t last_load_time;              // 最后加载时间
} domain_table_t;

// ============================================================================
// LRU缓存相关定义
// ============================================================================

#define DNS_CACHE_SIZE 20000             // 缓存容量（优化：增加到2万，提升命中率）
#define DNS_CACHE_HASH_SIZE 32768        // 哈希表大小（优化：32K，平衡内存和冲突率）
#define DEFAULT_TTL 300                 // 默认TTL（5分钟）
#define DNS_CACHE_NUM_SEGMENTS 128       // 分段数量，必须是2的幂（优化：128段，减少锁争用）
#define MAX_CACHE_KEY_LENGTH (MAX_DOMAIN_LENGTH + 10) // 缓存键最大长度 "domain:TYPE"

// DNS缓存条目
typedef struct dns_cache_entry {
    char key[MAX_CACHE_KEY_LENGTH];     // 缓存键 (例如 "example.com:A")
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
    DNS_ENTITY* dns_response;           // cache缓存找到返回DNS响应
    char resolved_ip[MAX_IP_LENGTH];    // 本地表查找时返回ip
} dns_query_response_t;

// ============================================================================
// 函数声明
// ============================================================================

// 本地域名表管理
int domain_table_init();
int domain_table_load_from_file(const char* filename);
ip_address_entry_t* domain_table_lookup(const char* domain, unsigned short qtype);
void domain_table_destroy();

// LRU缓存管理
int dns_cache_init(int max_size);
dns_cache_entry_t* dns_cache_get(const char* domain, unsigned short qtype);
int dns_cache_put(const char* domain, unsigned short qtype, DNS_ENTITY* response, int ttl);
void dns_cache_cleanup_expired();
void dns_cache_print_stats();
void dns_cache_destroy();

// 统一查询接口
dns_query_response_t* dns_relay_query(const char* domain, unsigned short qtype);
int dns_relay_init(const char* domain_file);
void dns_relay_cleanup(void);
int dns_relay_cache_response(const char* domain, unsigned short qtype, DNS_ENTITY* response);
void dns_relay_get_stats(int* domain_count, int* cache_size, unsigned long* cache_hits, unsigned long* cache_misses);

// 内部辅助函数声明
unsigned int hash_key(const char* key);
dns_cache_segment_t* get_cache_segment(const char* key);
domain_table_segment_t* get_domain_table_segment(const char* domain);
void lru_move_to_head_segment(dns_cache_segment_t* segment, dns_cache_entry_t* entry);
dns_cache_entry_t* lru_remove_tail_segment(dns_cache_segment_t* segment);

#endif // RELAYBUILD_H


