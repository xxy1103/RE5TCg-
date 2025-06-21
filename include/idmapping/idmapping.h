#ifndef IDMAPPING_H
#define IDMAPPING_H

#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件
#include <time.h>
#include <string.h>


#define MAX_CONCURRENT_REQUESTS 10000  // 最大并发请求数
#define REQUEST_TIMEOUT 3             // 请求超时时间（秒）
#define HASH_TABLE_SIZE 16384         // 哈希表大小（2的幂，用于快速取模）
#define HASH_LOAD_FACTOR 0.75         // 哈希表负载因子

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

// 优化后的映射表管理结构
typedef struct {
    dns_mapping_entry_t* hash_table[HASH_TABLE_SIZE];  // 哈希表（基于new_id）
    dns_mapping_entry_t* entry_pool;                   // 预分配的条目池
    int* free_indices;                                  // 空闲索引栈
    int free_top;                                       // 空闲栈顶指针
    
    // 时间链表头尾（用于快速过期清理）
    dns_mapping_entry_t* time_head;
    dns_mapping_entry_t* time_tail;
    
    id_stack_t id_stack;                                // 空闲ID栈
    unsigned short next_id;                             // 下一个可用的ID
    int count;                                          // 当前活跃映射数量
    time_t last_cleanup;                                // 上次清理时间
} dns_mapping_table_t;


// 新增映射表相关函数
void init_mapping_table(dns_mapping_table_t* table);
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id);
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id);
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id);
void cleanup_expired_mappings(dns_mapping_table_t* table);

// 新增优化相关的内部函数
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