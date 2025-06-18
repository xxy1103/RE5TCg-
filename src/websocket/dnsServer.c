#include "websocket/dnsServer.h"
#include <time.h>  // 添加时间相关的头文件支持

// 全局映射表
static dns_mapping_table_t g_mapping_table;
// 上游DNS套接字（全局，避免重复创建）
static SOCKET g_upstream_socket = INVALID_SOCKET;

/**
 * @brief 初始化映射表
 */
void init_mapping_table(dns_mapping_table_t* table) {
    memset(table, 0, sizeof(dns_mapping_table_t));
    table->next_id = 1;  // 从1开始分配ID
    table->count = 0;
}

/**
 * @brief 添加新的映射关系
 */
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, 
                struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id) {
    // 清理过期映射
    cleanup_expired_mappings(table);
    
    // 查找空闲槽位
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (!table->entries[i].is_active) {
            // 分配新ID，避免0（通常作为无效值）
            *new_id = table->next_id;
            if (table->next_id == 0) table->next_id = 1;  // 跳过0
            table->next_id++;
            
            // 填充映射信息
            table->entries[i].original_id = original_id;
            table->entries[i].new_id = *new_id;
            table->entries[i].client_addr = *client_addr;
            table->entries[i].client_addr_len = client_addr_len;
            table->entries[i].timestamp = time(NULL);
            table->entries[i].is_active = 1;
            
            table->count++;
            log_debug("Added mapping: original_id=%d -> new_id=%d", original_id, *new_id);
            return MYSUCCESS;
        }
    }
    
    log_error("Mapping table is full, cannot add new mapping");
    return MYERROR;
}

/**
 * @brief 根据新ID查找映射
 */
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id) {
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && table->entries[i].new_id == new_id) {
            return &table->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief 移除映射
 */
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id) {
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && table->entries[i].new_id == new_id) {
            table->entries[i].is_active = 0;
            table->count--;
            log_debug("Removed mapping: new_id=%d", new_id);
            return;
        }
    }
}

/**
 * @brief 清理过期映射
 */
void cleanup_expired_mappings(dns_mapping_table_t* table) {
    time_t current_time = time(NULL);
    int cleaned = 0;
    
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && 
            (current_time - table->entries[i].timestamp) > REQUEST_TIMEOUT) {
            table->entries[i].is_active = 0;
            table->count--;
            cleaned++;
        }
    }
    
    if (cleaned > 0) {
        log_info("Cleaned up %d expired mappings", cleaned);
    }
}


/**
 * @brief DNS代理服务器主函数，使用非阻塞I/O处理并发请求
 * 
 * @return int 成功返回 MYSUCCESS，失败返回 MYERROR。
 */
int start_dns_proxy_server() {
    SOCKET server_socket; // 服务器套接字
    struct sockaddr_in server_addr; // 服务器地址结构

    // 初始化映射表
    init_mapping_table(&g_mapping_table);

    // 创建一个UDP套接字
    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket == INVALID_SOCKET) {
        log_error("Failed to create socket: %d", WSAGetLastError());
        return MYERROR;
    }

    // 设置为非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(server_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        log_error("Failed to set non-blocking mode: %d", WSAGetLastError());
        closesocket(server_socket);
        return MYERROR;
    }

    // 创建上游socket
    g_upstream_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_upstream_socket == INVALID_SOCKET) {
        log_error("Failed to create upstream socket: %d", WSAGetLastError());
        closesocket(server_socket);
        return MYERROR;
    }

    // 设置上游socket为非阻塞
    if (ioctlsocket(g_upstream_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        log_error("Failed to set upstream socket non-blocking: %d", WSAGetLastError());
        closesocket(server_socket);
        closesocket(g_upstream_socket);
        return MYERROR;
    }

    // 设置 SO_REUSEADDR 选项，允许服务器绑定到最近关闭的端口
    int optval = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) == SOCKET_ERROR) {
        log_warn("setsockopt(SO_REUSEADDR) failed with error: %d", WSAGetLastError());
        // 即使设置失败，也继续尝试绑定，因为这不一定是致命错误
    }

    // 配置服务器地址信息
    memset(&server_addr, 0, sizeof(server_addr)); // 初始化结构体
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听来自任何网络接口的连接
    server_addr.sin_port = htons(DNS_PORT); // 监听标准DNS端口53

    // 将套接字绑定到指定的IP地址和端口
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("Failed to bind socket to port %d: %d", DNS_PORT, WSAGetLastError());
        closesocket(server_socket); // 绑定失败，关闭套接字
        closesocket(g_upstream_socket);
        return MYERROR;
    }

    log_info("DNS Proxy Server listening on port %d...", DNS_PORT);

    // 主事件循环
    while (1) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        FD_SET(g_upstream_socket, &read_fds);
        
        // 设置select超时时间
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        SOCKET max_fd = (server_socket > g_upstream_socket) ? server_socket : g_upstream_socket;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity == SOCKET_ERROR) {
            log_error("select() failed: %d", WSAGetLastError());
            break;
        }
        
        // 处理客户端新请求
        if (FD_ISSET(server_socket, &read_fds)) {
            handle_client_requests(server_socket);
        }
        
        // 处理上游服务器响应
        if (FD_ISSET(g_upstream_socket, &read_fds)) {
            handle_upstream_responses(server_socket);
        }
        
        // 定期清理过期映射
        static time_t last_cleanup = 0;
        time_t current_time = time(NULL);
        if (current_time - last_cleanup > 10) {  // 每10秒清理一次
            cleanup_expired_mappings(&g_mapping_table);
            last_cleanup = current_time;
        }
    }

    // 清理资源
    closesocket(server_socket);
    closesocket(g_upstream_socket);
    return MYSUCCESS;
}


/**
 * @brief 处理接收到的DNS请求，包括解析、转发、接收响应和回复客户端。
 * 
 * @param request_buffer 包含DNS请求数据的缓冲区。
 * @param request_len 请求数据的长度。
 * @param client_addr 指向客户端地址信息的指针。
 * @param client_addr_len 客户端地址信息的长度。
 * @param server_socket 服务器的套接字，用于向客户端发送响应。
 * @return int 成功返回 MYSUCCESS，失败返回 MYERROR。
 */
int handle_dns_request(char* request_buffer, int request_len, struct sockaddr_in* client_addr, int client_addr_len, SOCKET server_socket)
{
    char response_buffer[BUF_SIZE]; // 存储上游DNS服务器响应的缓冲区
    int response_len = 0; // 响应数据的长度
    clock_t start_time = clock(); // 记录函数开始执行的时间
    DNS_ENTITY* request_entity = parse_dns_packet(request_buffer, request_len);
    if (!request_entity) {
        log_error("Failed to parse DNS request");
        return MYERROR;
    }

    // 2. 打印接收请求的基本信息
    log_info("=== Received DNS Request ===");
    log_info("Transaction ID: %d", request_entity->id);
    log_info("Flags: 0x%04X", request_entity->flags);
    log_info("Questions: %d", request_entity->qdcount);
    // 遍历并打印所有问题
    if (request_entity->questions && request_entity->qdcount > 0) {
        for (int i = 0; i < request_entity->qdcount; i++) {
            DNS_QUESTION_ENTITY* question = &request_entity->questions[i];
            log_info("Question %d: Name=%s, Type=%d, Class=%d", 
                    i + 1, question->qname, question->qtype, question->qclass);
        }    }
    // 3. 将原始请求数据包转发给上游DNS服务器
    
    if (forward_to_upstream_dns(request_buffer, request_len, 
                               response_buffer, &response_len) != MYSUCCESS) {
        log_error("Failed to forward request to upstream DNS server");
        free_dns_entity(request_entity); // 释放请求实体内存
        return MYERROR;
    }

    // 4. 将从上游服务器收到的响应数据解析为 DNS_ENTITY 结构体
    DNS_ENTITY* response_entity = parse_dns_packet(response_buffer, response_len);
    if (!response_entity) {
        log_error("Failed to parse DNS response from upstream server");
        free_dns_entity(request_entity); // 释放请求实体内存
        return MYERROR;
    }

     // 5. 打印从上游DNS服务器收到的响应的关键信息
    log_info("=== Received DNS Response from Upstream ===");
    log_info("Transaction ID: %u", response_entity->id);
    log_info("Flags: 0x%04X", response_entity->flags);
    log_info("Questions: %d, Answers: %d", response_entity->qdcount, response_entity->ancount);

     // 遍历并打印所有答案记录
    if (response_entity->ancount > 0) {
        log_info("--- Answers ---");
        for (int i = 0; i < response_entity->ancount; i++) {
            R_DATA_ENTITY* answer = &response_entity->answers[i];
            
            // 根据记录类型格式化输出
            if (answer->type == A && answer->data_len == 4) { // A记录 (IPv4)
                unsigned char* ip_addr = (unsigned char*)answer->rdata;
                log_info("Answer %d: %s -> %d.%d.%d.%d (TTL: %lu)", 
                        i + 1, answer->name, ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], 
                        (unsigned long)answer->ttl);
            } else if (answer->type == AAAA && answer->data_len == 16) { // AAAA记录 (IPv6)
                log_info("Answer %d: %s -> IPv6 (TTL: %lu)", i + 1, answer->name, (unsigned long)answer->ttl);
            } else if (answer->type == CNAME) { // CNAME记录
                log_info("Answer %d: %s -> CNAME: %s (TTL: %lu)", 
                        i + 1, answer->name, answer->rdata, (unsigned long)answer->ttl);
            } else { // 其他类型的记录
                log_info("Answer %d: %s -> Type %d (TTL: %lu)", 
                        i + 1, answer->name, answer->type, (unsigned long)answer->ttl);
            }
        }    }

    // 6. 将上游服务器的响应数据包发送回原始客户端   
    if (sendto(server_socket, response_buffer, response_len, 0, 
              (struct sockaddr*)client_addr, client_addr_len) == SOCKET_ERROR) {
        log_error("Failed to send DNS response to client: %d", WSAGetLastError());
        free_dns_entity(request_entity);
        free_dns_entity(response_entity);
        return MYERROR;
    }

    // 记录总执行时间
    double total_time = (double)(clock() - start_time) / CLOCKS_PER_SEC;
    log_info("DNS response sent to client %s:%d (%d bytes) - Total processing time: %.3f seconds", 
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), response_len, total_time);

    // 清理本次请求/响应所分配的内存
    free_dns_entity(request_entity);
    free_dns_entity(response_entity);
    
    return MYSUCCESS;
}

/**
 * @brief 处理客户端请求
 */
void handle_client_requests(SOCKET server_socket) {
    char request_buffer[BUF_SIZE];
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int request_len;
    
    // 循环处理所有待处理的客户端请求
    while ((request_len = recvfrom(server_socket, request_buffer, BUF_SIZE, 0,
                                  (struct sockaddr*)&client_addr, &client_addr_len)) > 0) {
        
        log_info("Received DNS request from %s:%d (%d bytes)",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), request_len);
        
        // 解析请求获取原始ID
        if (request_len < 2) {
            log_warn("Request too short, ignoring");
            continue;
        }
        
        unsigned short original_id = ntohs(*(unsigned short*)request_buffer);
        unsigned short new_id;
        
        // 添加映射
        if (add_mapping(&g_mapping_table, original_id, &client_addr, client_addr_len, &new_id) != MYSUCCESS) {
            log_error("Failed to add mapping for request from %s:%d",
                     inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            continue;
        }
        
        // 修改请求ID
        *(unsigned short*)request_buffer = htons(new_id);
        
        // 转发到上游DNS服务器
        if (forward_request_to_upstream(request_buffer, request_len) != MYSUCCESS) {
            log_error("Failed to forward request to upstream");
            remove_mapping(&g_mapping_table, new_id);
        }
        
        client_addr_len = sizeof(client_addr);  // 重置长度
    }
    
    // 检查是否是因为WSAEWOULDBLOCK（没有更多数据）而退出循环
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) {
        log_error("Error receiving from client: %d", error);
    }
}

/**
 * @brief 处理上游服务器响应
 */
void handle_upstream_responses(SOCKET server_socket) {
    char response_buffer[BUF_SIZE];
    struct sockaddr_in source_addr;
    int source_len = sizeof(source_addr);
    int response_len;
    
    // 循环处理所有待处理的上游响应
    while ((response_len = recvfrom(g_upstream_socket, response_buffer, BUF_SIZE, 0,
                                   (struct sockaddr*)&source_addr, &source_len)) > 0) {
        
        if (response_len < 2) {
            log_warn("Response too short, ignoring");
            continue;
        }
        
        // 获取响应ID
        unsigned short response_id = ntohs(*(unsigned short*)response_buffer);
        
        // 查找对应的映射
        dns_mapping_entry_t* mapping = find_mapping_by_new_id(&g_mapping_table, response_id);
        if (!mapping) {
            log_warn("No mapping found for response ID %d", response_id);
            continue;
        }
        
        // 恢复原始ID
        *(unsigned short*)response_buffer = htons(mapping->original_id);
        
        // 发送响应给客户端
        if (sendto(server_socket, response_buffer, response_len, 0,
                  (struct sockaddr*)&mapping->client_addr, mapping->client_addr_len) == SOCKET_ERROR) {
            log_error("Failed to send response to client: %d", WSAGetLastError());
        } else {
            log_info("Response sent to client %s:%d (%d bytes)",
                    inet_ntoa(mapping->client_addr.sin_addr), 
                    ntohs(mapping->client_addr.sin_port), response_len);
        }
        
        // 移除映射
        remove_mapping(&g_mapping_table, response_id);
        source_len = sizeof(source_addr);  // 重置长度
    }
    
    // 检查错误
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) {
        log_error("Error receiving from upstream: %d", error);
    }
}

/**
 * @brief 转发请求到上游DNS服务器
 */
int forward_request_to_upstream(char* request_buffer, int request_len) {
    struct sockaddr_in upstream_addr;
    
    memset(&upstream_addr, 0, sizeof(upstream_addr));
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(DNS_PORT);
    upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER);
    
    if (sendto(g_upstream_socket, request_buffer, request_len, 0,
              (struct sockaddr*)&upstream_addr, sizeof(upstream_addr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            log_error("Failed to send to upstream DNS: %d", error);
            return MYERROR;
        }
    }
    
    return MYSUCCESS;
}

// 转发请求到上游DNS服务器
int forward_to_upstream_dns(char* request_buffer, int request_len, 
                           char* response_buffer, int* response_len) {
    SOCKET upstream_socket;
    struct sockaddr_in upstream_addr;
    int received_len;    // 创建socket连接到上游DNS服务器
    upstream_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (upstream_socket == INVALID_SOCKET) {
        log_error("Failed to create upstream socket: %d", WSAGetLastError());
        return MYERROR;
    }

    // 设置接收超时为500毫秒
    DWORD timeout_ms = 500;
    if (setsockopt(upstream_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
        log_warn("Failed to set socket receive timeout: %d", WSAGetLastError());
    }

    // 设置上游DNS服务器地址
    memset(&upstream_addr, 0, sizeof(upstream_addr));
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(DNS_PORT);
    upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER);

    // 发送请求到上游DNS服务器
    if (sendto(upstream_socket, request_buffer, request_len, 0, 
              (struct sockaddr*)&upstream_addr, sizeof(upstream_addr)) == SOCKET_ERROR) {
        log_error("Failed to send request to upstream DNS server: %d", WSAGetLastError());
        closesocket(upstream_socket);
        return MYERROR;
    }

    log_info("Request forwarded to upstream DNS server %s:%d", DNS_SERVER, DNS_PORT);

    // 接收响应
    struct sockaddr_in source_addr;
    int source_len = sizeof(source_addr);
    received_len = recvfrom(upstream_socket, response_buffer, BUF_SIZE, 0, 
                           (struct sockaddr*)&source_addr, &source_len);
      if (received_len == SOCKET_ERROR) {
        int error_code = WSAGetLastError();
        if (error_code == WSAETIMEDOUT) {
            log_warn("Timeout waiting for response from upstream DNS server (500ms)");
        } else if (error_code == WSAECONNRESET) {
            log_warn("Connection reset by upstream DNS server");
        } else {
            log_error("Failed to receive response from upstream DNS server: %d", error_code);
        }
        closesocket(upstream_socket);
        return MYERROR;
    }

    *response_len = received_len;
    log_info("Received response from upstream DNS server (%d bytes)", received_len);

    closesocket(upstream_socket);
    return MYSUCCESS;
}