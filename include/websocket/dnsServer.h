#ifndef DNSSERVER_H
#define DNSSERVER_H

#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件
#include "websocket/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数
#include <time.h>

// 定义最大并发请求数和超时时间
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

// DNS代理服务器函数
int start_dns_proxy_server();
int forward_to_upstream_dns(char* request_buffer, int request_len, char* response_buffer, int* response_len);
int handle_dns_request(char* request_buffer, int request_len, struct sockaddr_in* client_addr, int client_addr_len, SOCKET server_socket);

// 新增映射表相关函数
void init_mapping_table(dns_mapping_table_t* table);
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id);
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id);
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id);
void cleanup_expired_mappings(dns_mapping_table_t* table);

// 新增并发处理相关函数
void handle_client_requests(SOCKET server_socket);
void handle_upstream_responses(SOCKET server_socket);
int forward_request_to_upstream(char* request_buffer, int request_len);
#endif // DNSSERVER_H