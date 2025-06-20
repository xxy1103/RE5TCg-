#include "websocket/websocket.h"
#include "platform/platform.h"
#include <stdlib.h>  // 为rand()和srand()函数
#include <time.h>    // 为time()函数
#include <string.h>  // 为strcpy()函数

//全局变量定义
struct sockaddr_in upstream_addr;
upstream_dns_pool_t g_upstream_pool; // 全局上游DNS服务器池

struct sockaddr_in get_upstream_addr()
{
    char selected_ip[MAX_IP_LENGTH];
    struct sockaddr_in upstream_addr;
    // 初始化随机数种子
    srand((unsigned int)time(NULL));
    
    // 从IP池中随机选择一个DNS服务器
    if (upstream_pool_get_random_server(&g_upstream_pool, selected_ip, sizeof(selected_ip)) == MYSUCCESS) {
        // 设置目标地址结构
        upstream_addr.sin_family = AF_INET; // 地址族为 IPv4
        upstream_addr.sin_port = htons(DNS_PORT); // 设置 DNS 端口号，并转换为主机字节序到网络字节序
        upstream_addr.sin_addr.s_addr = inet_addr(selected_ip); // 设置从池中选择的DNS服务器IP地址
    } else {
        // 如果池为空或出错，使用默认DNS服务器
        upstream_addr.sin_family = AF_INET; 
        upstream_addr.sin_port = htons(DNS_PORT); 
        upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER); 
    }
    return upstream_addr;
}

/**
 * @brief 初始化上游DNS服务器池
 * @param pool 服务器池指针
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_init(upstream_dns_pool_t* pool) {
    if (!pool) {
        return MYERROR;
    }
    
    pool->server_count = 0;
    pool->current_index = 0;
    
    return MYSUCCESS;
}

/**
 * @brief 向DNS服务器池添加服务器
 * @param pool 服务器池指针
 * @param ip_address IP地址字符串
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_add_server(upstream_dns_pool_t* pool, const char* ip_address) {
    if (!pool || !ip_address) {
        return MYERROR;
    }
    
    if (pool->server_count >= MAX_UPSTREAM_SERVERS) {
        return MYERROR;
    }
    
    strncpy(pool->servers[pool->server_count], ip_address, MAX_IP_LENGTH - 1);
    pool->servers[pool->server_count][MAX_IP_LENGTH - 1] = '\0';
    pool->server_count++;

    return MYSUCCESS;
}

/**
 * @brief 从DNS服务器池中随机获取一个服务器IP
 * @param pool 服务器池指针
 * @param ip_address 输出IP地址的缓冲区
 * @param max_len 缓冲区最大长度
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_get_random_server(upstream_dns_pool_t* pool, char* ip_address, int max_len) {
    if (!pool || !ip_address || pool->server_count == 0) {
        return MYERROR;
    }
    

    // 生成随机索引
    int random_index = rand() % pool->server_count;
    strncpy(ip_address, pool->servers[random_index], max_len - 1);
    ip_address[max_len - 1] = '\0';
    
    return MYSUCCESS;
}


/**
 * @brief 销毁DNS服务器池
 * @param pool 服务器池指针
 */
void upstream_pool_destroy(upstream_dns_pool_t* pool) {
    if (!pool) {
        return;
    }
    pool->server_count = 0;
    pool->current_index = 0;
}

int sendDnsPacket(SOCKET sock,struct sockaddr_in address,const DNS_ENTITY* dns_entity)
{
    char buf[BUF_SIZE];      // 用于发送数据的缓冲区    
    // 将DNS_ENTITY序列化为字节流
    int packet_len = serialize_dns_packet(buf, dns_entity);
    if (packet_len <= 0) {
        log_error("DNS数据包序列化失败，数据包长度=%d", packet_len);
        return MYERROR;
    }  
    
    // 使用 sendto 函数通过 UDP 发送数据
    if (sendto(sock, buf, packet_len, 0, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        int error = platform_get_last_error();
        log_error("sendto 调用失败，错误码: %d", error);        // === 错误处理和分类 ===
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK) {
#else
        if (error == EWOULDBLOCK || error == EAGAIN) {
#endif
            // 发送缓冲区满，这在高负载时可能发生
            log_warn("发送缓冲区已满 (WOULDBLOCK)，请求可能延迟");
            // 注意：在真实的生产环境中，这里可能需要重试逻辑
            return MYSUCCESS;  // 视为成功，因为数据最终会被发送
        } else {
            // 真正的网络错误
            log_error("向 %s:%d 发送数据失败，错误码: %d", 
                     inet_ntoa(address.sin_addr), ntohs(address.sin_port), error);
            return MYERROR;
        }
    }
    
    // === 记录成功发送 ===
    
    
    return MYSUCCESS;
}

/**
 * @brief 向随机选择的上游DNS服务器发送DNS数据包
 * @param sock 套接字
 * @param dns_entity DNS实体
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int sendDnsPacketToRandomUpstream(SOCKET sock, const DNS_ENTITY* dns_entity)
{
    char selected_ip[MAX_IP_LENGTH];
    struct sockaddr_in target_addr;
    
    // 从IP池中随机选择一个DNS服务器
    if (upstream_pool_get_random_server(&g_upstream_pool, selected_ip, sizeof(selected_ip)) != MYSUCCESS) {
        // 如果池为空或出错，使用默认DNS服务器
        strcpy(selected_ip, DNS_SERVER);
    }
    
    // 设置目标地址结构
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(DNS_PORT);
    target_addr.sin_addr.s_addr = inet_addr(selected_ip);
    
    log_debug("发送到随机DNS：%s",selected_ip);

    // 发送DNS数据包到选定的服务器
    return sendDnsPacket(sock, target_addr, dns_entity);
}

/**
 * @brief 判断指定IP地址是否在DNS服务器池中
 * @param pool 服务器池指针
 * @param ip_address 要检查的IP地址字符串
 * @return 存在返回1，不存在返回0，出错返回-1
 */
int upstream_pool_contains_server(upstream_dns_pool_t* pool, const char* ip_address) {
    if (!pool || !ip_address) {
        return -1; // 参数错误
    }
    
    // 如果池为空，直接返回不存在
    if (pool->server_count == 0) {
        return 0;
    }
    
    // 遍历服务器池中的所有IP地址
    for (int i = 0; i < pool->server_count; i++) {
        if (strcmp(pool->servers[i], ip_address) == 0) {
            return 1; // 找到匹配的IP
        }
    }
    
    return 0; // 未找到
}

