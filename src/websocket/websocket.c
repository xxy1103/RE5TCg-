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
    struct sockaddr_in upstream_addr;
    // 初始化随机数种子
    srand((unsigned int)time(NULL));
    
    // 从IP池中随机选择一个DNS服务器
    struct sockaddr_in* selected_server = upstream_pool_get_random_server(&g_upstream_pool);
    if (selected_server != NULL) {
        // 直接返回选中的服务器地址
        return *selected_server;
    } else {
        // 如果池为空或出错，使用默认DNS服务器
        upstream_addr.sin_family = AF_INET; 
        upstream_addr.sin_port = htons(DNS_PORT); 
        upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER); 
        return upstream_addr;
    }
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
 * @brief 向DNS服务器池添加服务器 - 优化版本
 * @param pool 服务器池指针
 * @param ip_address IP地址字符串
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_add_server(upstream_dns_pool_t* pool, const char* ip_address) {
    if (!pool || !ip_address) {
        log_error("upstream_pool_add_server: 无效参数");
        return MYERROR;
    }
    
    if (pool->server_count >= MAX_UPSTREAM_SERVERS) {
        log_error("DNS服务器池已满，无法添加更多服务器 (最大: %d)", MAX_UPSTREAM_SERVERS);
        return MYERROR;
    }
    
    // 检查IP地址是否已存在
    if (upstream_pool_contains_server(pool, ip_address) == 1) {
        log_warn("DNS服务器 %s 已存在于池中", ip_address);
        return MYSUCCESS; // 视为成功，避免重复添加
    }
    
    // 直接在池中设置完整的sockaddr_in结构
    struct sockaddr_in* server_addr = &pool->servers[pool->server_count];
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(DNS_PORT);
    
    // 转换IP地址字符串为网络地址
    if (inet_addr(ip_address) == INADDR_NONE) {
        log_error("无效的IP地址格式: %s", ip_address);
        return MYERROR;
    }
    
    server_addr->sin_addr.s_addr = inet_addr(ip_address);
    pool->server_count++;

    log_info("成功添加DNS服务器: %s (总数: %d)", ip_address, pool->server_count);
    return MYSUCCESS;
}

/**
 * @brief 从DNS服务器池中随机获取一个服务器地址 - 优化版本
 * @param pool 服务器池指针
 * @return 成功返回服务器地址指针，失败返回NULL
 */
struct sockaddr_in* upstream_pool_get_random_server(upstream_dns_pool_t* pool) {
    if (!pool || pool->server_count == 0) {
        return NULL;
    }
    
    // 生成随机索引
    int random_index = rand() % pool->server_count;
    
    log_debug("随机选择DNS服务器: %s (索引: %d/%d)", 
             inet_ntoa(pool->servers[random_index].sin_addr), 
             random_index, pool->server_count - 1);
    
    return &pool->servers[random_index];
}

/**
 * @brief 从DNS服务器池中轮询获取下一个服务器地址
 * @param pool 服务器池指针
 * @return 成功返回服务器地址指针，失败返回NULL
 */
struct sockaddr_in* upstream_pool_get_next_server(upstream_dns_pool_t* pool) {
    if (!pool || pool->server_count == 0) {
        return NULL;
    }
    
    // 轮询获取下一个服务器
    struct sockaddr_in* server_addr = &pool->servers[pool->current_index];
    
    log_debug("轮询选择DNS服务器: %s (索引: %d/%d)", 
             inet_ntoa(server_addr->sin_addr), 
             pool->current_index, pool->server_count - 1);
    
    // 更新索引到下一个服务器
    pool->current_index = (pool->current_index + 1) % pool->server_count;
    
    return server_addr;
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
 * @brief 判断指定IP地址是否在DNS服务器池中 - 优化版本
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
    
    // 将输入的IP地址字符串转换为网络地址，用于比较
    unsigned long target_addr = inet_addr(ip_address);
    if (target_addr == INADDR_NONE) {
        log_error("无效的IP地址格式: %s", ip_address);
        return -1; // IP地址格式错误
    }
    
    // 遍历服务器池中的所有地址结构
    for (int i = 0; i < pool->server_count; i++) {
        if (pool->servers[i].sin_addr.s_addr == target_addr) {
            return 1; // 找到匹配的IP
        }
    }
      return 0; // 未找到
}


/**
 * @brief 向轮询选择的上游DNS服务器发送DNS数据包
 * @param sock 套接字
 * @param dns_entity DNS实体
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int sendDnsPacketToNextUpstream(SOCKET sock, const DNS_ENTITY* dns_entity)
{
    struct sockaddr_in default_addr;
    
    // 从IP池中轮询选择下一个DNS服务器
    struct sockaddr_in* target_addr = upstream_pool_get_next_server(&g_upstream_pool);
    if (target_addr == NULL) {
        // 如果池为空或出错，使用默认DNS服务器
        default_addr.sin_family = AF_INET;
        default_addr.sin_port = htons(DNS_PORT);
        default_addr.sin_addr.s_addr = inet_addr(DNS_SERVER);
        target_addr = &default_addr;
        
        log_warn("DNS服务器池为空，使用默认服务器: %s", DNS_SERVER);
    }
    
    log_debug("发送到轮询DNS：%s", inet_ntoa(target_addr->sin_addr));

    // 发送DNS数据包到选定的服务器
    return sendDnsPacket(sock, *target_addr, dns_entity);
}

