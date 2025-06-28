#ifndef IDMAPPING_H
#define IDMAPPING_H

#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件
#include <time.h>
#include <string.h>
#include <pthread.h> // 包含 pthread 库头文件，用于分段锁

#define MAX_CONCURRENT_REQUESTS 50000   // 优化：增加并发容量，适应高负载
#define REQUEST_TIMEOUT 5              // 优化：延长超时，减少频繁清理开销
#define HASH_TABLE_SIZE 32768          // 优化：扩大哈希表，减少冲突链长度
#define HASH_LOAD_FACTOR 0.65          // 优化：降低负载因子，提升查找性能

// ============================================================================
// 分段锁优化相关定义
// ============================================================================

#define ID_MAPPING_NUM_SEGMENTS 64      // 分段数量，必须是2的幂，优化并发性能
#define CLEANUP_BATCH_SIZE 100          // 批量清理大小，减少锁持有时间

// 定义映射表项结构
typedef struct dns_mapping_entry {
    unsigned short original_id;           // 客户端原始请求ID
    unsigned short new_id;               // 分配给上游的新ID
    struct sockaddr_in client_addr;      // 客户端地址
    int client_addr_len;                 // 客户端地址长度
    time_t timestamp;                    // 请求时间戳（用于清理过期请求）
    int is_active;                       // 是否激活状态
    struct dns_mapping_entry* next;      // 哈希冲突链表指针
    struct dns_mapping_entry* time_next; // 时间链表指针（用于快速过期清理）
    struct dns_mapping_entry* time_prev; // 时间链表前驱指针
} dns_mapping_entry_t;

// 空闲ID栈结构（用于快速分配和回收ID）
typedef struct {
    unsigned short* ids;                 // ID数组
    int top;                             // 栈顶指针
    int capacity;                        // 栈容量
} id_stack_t;

// ID映射分段结构（前置定义）
typedef struct {
    pthread_rwlock_t rwlock;            // 分段读写锁
    dns_mapping_entry_t* hash_buckets[HASH_TABLE_SIZE / ID_MAPPING_NUM_SEGMENTS];  // 分段哈希桶
    dns_mapping_entry_t* time_head;     // 分段时间链表头
    dns_mapping_entry_t* time_tail;     // 分段时间链表尾
    int active_count;                   // 分段活跃映射数量
    time_t last_cleanup;                // 分段最后清理时间
} id_mapping_segment_t;

// 优化后的分段式映射表管理结构
typedef struct {
    // 分段锁数组（核心优化）
    id_mapping_segment_t segments[ID_MAPPING_NUM_SEGMENTS];
    
    // 全局共享资源（仅在必要时加锁）
    dns_mapping_entry_t* entry_pool;                   // 预分配的条目池
    int* free_indices;                                  // 空闲索引栈
    int free_top;                                       // 空闲栈顶指针
    pthread_mutex_t pool_lock;                          // 仅保护内存池操作
    
    id_stack_t id_stack;                                // 空闲ID栈
    pthread_mutex_t id_stack_lock;                      // ID栈专用锁
    unsigned short next_id;                             // 下一个可用的ID
    
    int total_count;                                    // 总映射数量（原子操作）
    time_t last_global_cleanup;                         // 全局清理时间
} dns_mapping_table_t;

// 映射表相关函数 - 支持分段锁优化
void init_mapping_table(dns_mapping_table_t* table);
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id);
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id);
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id);
void cleanup_expired_mappings(dns_mapping_table_t* table);

// 分段锁优化相关函数
static id_mapping_segment_t* get_mapping_segment(const dns_mapping_table_t* table, unsigned short id);
static unsigned int calculate_segment_hash_index(unsigned short id, int buckets_per_segment);
static void segment_add_to_time_list(id_mapping_segment_t* segment, dns_mapping_entry_t* entry);
static void segment_remove_from_time_list(id_mapping_segment_t* segment, dns_mapping_entry_t* entry);
static void cleanup_segment_expired_mappings(dns_mapping_table_t* table, id_mapping_segment_t* segment);

// 原有优化相关的内部函数
static unsigned int hash_function(unsigned short id);
static void init_id_stack(id_stack_t* stack);
static void destroy_id_stack(id_stack_t* stack);
static int push_id(id_stack_t* stack, unsigned short id);
static unsigned short pop_id(id_stack_t* stack);
static int is_id_stack_empty(id_stack_t* stack);
static void add_to_time_list(dns_mapping_table_t* table, dns_mapping_entry_t* entry);
static void remove_from_time_list(dns_mapping_table_t* table, dns_mapping_entry_t* entry);
void destroy_mapping_table(dns_mapping_table_t* table);

#endif // IDMAPPING_H