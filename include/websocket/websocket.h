#ifndef WEBSOCKET_H
#define WEBSOCKET_H

//头文件
#include "platform/platform.h" // 包含平台抽象层头文件
#include "debug/debug.h"   // 包含调试相关的头文件
#include "websocket/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数

//定义宏
#define MYSUCCESS 1
#define MYERROR 0
#define DNS_SERVER "10.3.9.6"  // 定义 Google 的公共 DNS 服务器地址
#define DNS_PORT 53           // 定义 DNS 服务使用的标准端口号
#define BUF_SIZE 65536        // 定义缓冲区大小，用于存储发送和接收的数据

// DNS上游服务器IP池相关定义
#define MAX_UPSTREAM_SERVERS 10    // 最大上游服务器数量

// 优化后的DNS上游服务器池结构体 - 直接存储sockaddr_in
typedef struct {
    struct sockaddr_in servers[MAX_UPSTREAM_SERVERS];  // 直接存储完整的地址结构
    int server_count;                                   // 当前服务器数量
    int current_index;                                  // 当前使用的服务器索引（用于轮询）
} upstream_dns_pool_t;

//全局变量声明
extern struct sockaddr_in upstream_addr;
extern upstream_dns_pool_t g_upstream_pool; // 上游DNS服务器池



// DNS上游服务器池管理函数
int upstream_pool_init(upstream_dns_pool_t* pool);
int upstream_pool_add_server(upstream_dns_pool_t* pool, const char* ip_address);
struct sockaddr_in* upstream_pool_get_random_server(upstream_dns_pool_t* pool);
struct sockaddr_in* upstream_pool_get_next_server(upstream_dns_pool_t* pool);  // 新增：轮询获取服务器
void upstream_pool_destroy(upstream_dns_pool_t* pool);
int upstream_pool_contains_server(upstream_dns_pool_t* pool, const char* ip_address);
int upstream_pool_get_server_count(upstream_dns_pool_t* pool);  // 新增：获取服务器数量
void upstream_pool_print_status(upstream_dns_pool_t* pool);     // 新增：打印池状态

int sendDnsPacket(SOCKET sock,struct sockaddr_in address,const DNS_ENTITY* dns_entity);
int sendDnsPacketToRandomUpstream(SOCKET sock, const DNS_ENTITY* dns_entity);
int sendDnsPacketToNextUpstream(SOCKET sock, const DNS_ENTITY* dns_entity);  // 新增：轮询发送
#endif // WEBSOCKET_H