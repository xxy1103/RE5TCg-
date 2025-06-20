#ifndef IDMAPPING_H
#define IDMAPPING_H

#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件
#include <time.h>
#include <string.h>


#define MAX_CONCURRENT_REQUESTS 1000  // 最大并发请求数
#define REQUEST_TIMEOUT 5             // 请求超时时间（秒）

// 定义映射表项结构
typedef struct {
    unsigned short original_id;      // 客户端原始请求ID
    unsigned short new_id;          // 分配给上游的新ID
    struct sockaddr_in client_addr; // 客户端地址
    int client_addr_len;            // 客户端地址长度
    time_t timestamp;               // 请求时间戳（用于清理过期请求）
    int is_active;                  // 是否激活状态
} dns_mapping_entry_t;

// 映射表管理结构
typedef struct {
    dns_mapping_entry_t entries[MAX_CONCURRENT_REQUESTS];
    unsigned short next_id;         // 下一个可用的ID
    int count;                      // 当前活跃映射数量
} dns_mapping_table_t;


// 新增映射表相关函数
void init_mapping_table(dns_mapping_table_t* table);
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id);
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id);
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id);
void cleanup_expired_mappings(dns_mapping_table_t* table);

#endif // IDMAPPING_H