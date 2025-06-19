#include "websocket/dnsServer.h"
#include <time.h>  // 添加时间相关的头文件支持

/*
 * ============================================================================
 * 全局变量声明
 * ============================================================================
 */

// 全局映射表：维护客户端请求ID与上游服务器响应ID的映射关系
// 这是实现并发处理的核心数据结构，确保响应能正确返回给对应的客户端
static dns_mapping_table_t g_mapping_table;

// 上游DNS套接字（全局，避免重复创建和销毁）
// 使用单个全局socket可以减少系统调用开销，提高性能
static SOCKET server_socket = INVALID_SOCKET;

/*
 * ============================================================================
 * 主服务器函数
 * ============================================================================
 */

/**
 * @brief DNS代理服务器主函数，使用非阻塞I/O处理并发请求
 * 
 * 这是DNS代理服务器的核心函数，实现了：
 * 1. 创建和配置服务器socket和上游socket
 * 2. 使用select()实现事件驱动的并发处理
 * 3. 分离处理客户端请求和上游响应
 * 4. 定期清理过期的映射关系
 * 
 * 架构特点：
 * - 单线程事件循环，避免线程同步复杂性
 * - 非阻塞I/O，提高并发性能
 * - ID映射机制，支持多客户端并发请求
 * 
 * @return int 成功返回 MYSUCCESS，失败返回 MYERROR
 */
int start_dns_proxy_server() {

    struct sockaddr_in server_addr; // 服务器地址结构

    // === 第一步：初始化映射表 ===
    init_mapping_table(&g_mapping_table);
    log_info("Initialized DNS mapping table (max concurrent: %d)", MAX_CONCURRENT_REQUESTS);

    // === 第二步：创建服务器socket ===
    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket == INVALID_SOCKET) {
        log_error("Failed to create server socket: %d", WSAGetLastError());
        return MYERROR;
    }
    log_debug("Created server socket successfully");

    // === 第三步：设置服务器socket为非阻塞模式 ===
    // 非阻塞模式使得recvfrom()在没有数据时立即返回WSAEWOULDBLOCK
    // 而不是阻塞等待，这是实现并发处理的关键
    u_long mode = 1;
    if (ioctlsocket(server_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        log_error("Failed to set server socket non-blocking mode: %d", WSAGetLastError());
        closesocket(server_socket);
        return MYERROR;
    }
    log_debug("Set server socket to non-blocking mode");

    // === 第四步：设置socket选项 ===
    // SO_REUSEADDR允许快速重启服务器，避免"Address already in use"错误
    int optval = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) == SOCKET_ERROR) {
        log_warn("setsockopt(SO_REUSEADDR) failed with error: %d", WSAGetLastError());
        // 即使设置失败，也继续尝试绑定，因为这不一定是致命错误
    }

    // === 第五步：配置并绑定服务器地址 ===
    memset(&server_addr, 0, sizeof(server_addr)); // 清零结构体
    server_addr.sin_family = AF_INET;              // IPv4协议族
    server_addr.sin_addr.s_addr = INADDR_ANY;      // 监听所有网络接口
    server_addr.sin_port = htons(DNS_PORT);        // DNS标准端口53

    // 将套接字绑定到指定的IP地址和端口
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("Failed to bind socket to port %d: %d", DNS_PORT, WSAGetLastError());
        closesocket(server_socket);
        return MYERROR;
    }

    log_info("DNS Proxy Server listening on port %d...", DNS_PORT);
    log_info("Ready to handle concurrent DNS requests (using select() event loop)");

    // === 第六步：主事件循环 ===    // === 第八步：主事件循环 ===
    /*
     * 这是一个经典的事件驱动循环，使用select()系统调用来：
     * 1. 同时监听多个socket的可读事件
     * 2. 避免忙等待（busy-waiting），节省CPU资源
     * 3. 在有事件时快速响应
     * 
     * 循环逻辑：
     * - 设置要监听的socket集合
     * - 调用select()等待事件（带超时）
     * - 根据返回的事件类型分别处理
     * - 定期执行维护任务（清理过期映射）
     */
    while (1) {
        fd_set read_fds;        // 可读文件描述符集合
        struct timeval timeout; // select()超时设置
        
        // === 准备监听的socket集合 ===
        FD_ZERO(&read_fds);                    // 清空文件描述符集合
        FD_SET(server_socket, &read_fds);      // 添加服务器socket（监听客户端请求）
        
        // === 设置select超时时间 ===
        // 1秒超时确保：
        // 1. 不会无限期阻塞
        // 2. 可以定期执行维护任务
        // 3. 响应外部中断信号
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        // === 计算最大文件描述符值 ===
        // select()需要知道监听范围的上限
        SOCKET max_fd = server_socket;
        
        // === 调用select()等待事件 ===
        // 这是阻塞调用，直到：
        // 1. 有socket变为可读
        // 2. 达到超时时间
        // 3. 发生错误
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        // === 处理select()返回值 ===
        if (activity == SOCKET_ERROR) {
            log_error("select() failed: %d", WSAGetLastError());
            break; // 发生严重错误，退出主循环
        }
        
        // activity == 0 表示超时，继续下一次循环（会执行清理任务）
        // activity > 0 表示有socket事件，需要处理
        
        // === 处理客户端新请求 ===
        // 检查服务器socket是否有可读数据（新的客户端请求）
        if (FD_ISSET(server_socket, &read_fds)) {
            handle_receive();
        }
        
        // === 定期维护任务 ===
        // 每10秒清理一次过期映射，避免：
        // 1. 内存泄漏
        // 2. 映射表满载
        // 3. 查找性能下降
        static time_t last_cleanup = 0;
        time_t current_time = time(NULL);
        if (current_time - last_cleanup > 10) {  // 清理间隔：10秒
            cleanup_expired_mappings(&g_mapping_table);
            last_cleanup = current_time;
            
            // 记录服务器状态（每10秒一次）
            log_debug("Server status: active mappings=%d, uptime=%ld seconds", 
                     g_mapping_table.count, current_time - last_cleanup + 10);
        }
    }

    // === 清理资源 ===
    // 正常情况下这里不会执行（无限循环），但为了代码完整性保留
    log_info("DNS Proxy Server shutting down...");
    closesocket(server_socket);
    return MYSUCCESS;
}


/*
 * ============================================================================
 * 传统DNS处理函数（向后兼容）
 * ============================================================================
 */

/**
 * @brief 处理接收到的DNS请求（传统同步方式）
 * 
 * 注意：这个函数保留用于向后兼容，但在新的并发架构中不再使用。
 * 新的架构使用handle_client_requests()和handle_upstream_responses()
 * 来分别处理客户端请求和上游响应，实现真正的并发处理。
 * 
 * 传统处理流程：请求->解析->转发->等待响应->解析响应->返回（同步阻塞）
 * 新的处理流程：请求->映射->转发 | 响应->查找映射->返回（异步非阻塞）
 * 
 * @param request_buffer 包含DNS请求数据的缓冲区
 * @param request_len 请求数据的长度
 * @param client_addr 指向客户端地址信息的指针
 * @param client_addr_len 客户端地址信息的长度
 * @param server_socket 服务器的套接字，用于向客户端发送响应
 * @return int 成功返回 MYSUCCESS，失败返回 MYERROR
 */
int handle_receive()
{
    char receive_buffer[BUF_SIZE]; // 存储接收数据的缓冲区
    struct sockaddr_in source_addr; // 接收数据的源地址
    int source_addr_len = sizeof(source_addr);
    int receive_len = 0; // 接收到的数据长度
    int receive_processed = 0;     // 本次处理的数据计数
    
    clock_t start_time = clock(); // 记录函数开始执行的时间

    // === 批量处理接收到的数据 ===
    while((receive_len = recvfrom(server_socket, receive_buffer, BUF_SIZE, 0, 
                                 (struct sockaddr*)&source_addr, &source_addr_len)) > 0)
    {
        receive_processed++;
        char* source_ip = inet_ntoa(source_addr.sin_addr);
        // === 验证请求数据完整性 ===
        if (receive_len < 2) {
            log_warn("Request too short (%d bytes), ignoring", receive_len);
            source_addr_len = sizeof(source_addr);  // 重置地址长度
            continue;
        }        DNS_ENTITY* dns_entity = parse_dns_packet(receive_buffer, receive_len);
        if (!dns_entity) {
            log_warn("Failed to parse DNS packet from %s (%d bytes), ignoring", source_ip, receive_len);
            source_addr_len = sizeof(source_addr);  // 重置地址长度
            continue;
        }

        if(strcmp(DNS_SERVER,source_ip))
        {
            handle_client_requests(dns_entity, source_addr, source_addr_len, receive_len);
        }
        else
        {
            handle_upstream_responses(dns_entity, source_addr, source_addr_len,receive_len);
        }
        
        // 处理完成后释放DNS实体内存

        //log_debug("%s",dns_entity_to_string(dns_entity));

        free_dns_entity(dns_entity);
    }    

    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) {
        log_error("Error receiving from client: %d", error);
    } else if (receive_processed > 0) {
        log_debug("Processed %d client requests in this batch", receive_processed);
    }

}

/*
 * ============================================================================
 * 并发请求处理函数
 * ============================================================================
 */

/**
 * @brief 处理客户端请求
 * 
 * 这个函数处理来自客户端的DNS请求，实现了：
 * 1. 批量处理所有等待的请求（非阻塞接收）
 * 2. 为每个请求创建ID映射
 * 3. 修改请求ID避免冲突
 * 4. 转发请求到上游DNS服务器
 * 
 * 处理流程：
 * 客户端请求 -> 接收 -> 创建映射 -> 修改ID -> 转发上游
 * 
 * @param server_socket 服务器socket，用于接收客户端请求
 */
void handle_client_requests(DNS_ENTITY* dns_entity,struct sockaddr_in client_addr, int client_addr_len,int request_len) {
    
    log_debug("Received DNS request 1 from %s:%d (%d bytes)",
            inet_ntoa(client_addr.sin_addr), 
            ntohs(client_addr.sin_port), request_len);
    log_debug("original_id:%d",dns_entity->id);
        

        
    // === 提取并验证原始Transaction ID ===
    // DNS头部的前2个字节是Transaction ID（网络字节序）
    unsigned short original_id = dns_entity->id;
    unsigned short new_id;
        
    log_debug("Processing request with original_id=%d", original_id);
        
    // === 创建ID映射关系 ===
    /*
    * 为什么需要ID映射？
    * 1. 多个客户端可能使用相同的Transaction ID
    * 2. 我们需要确保每个上游请求都有唯一的ID
    * 3. 响应返回时要恢复原始ID给对应客户端
    */
    if (add_mapping(&g_mapping_table, original_id, &client_addr, client_addr_len, &new_id) != MYSUCCESS) {
        log_error("Failed to add mapping for request from %s:%d (original_id=%d)",
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), original_id);
        client_addr_len = sizeof(client_addr);  // 重置地址长度
        return;
    }
        
    // === 修改请求的Transaction ID ===
    // 将客户端的原始ID替换为我们分配的新ID
    dns_entity->id = new_id;
    log_debug("Modified request ID: %d -> %d", original_id, new_id);
        
        
    // === 转发请求到上游DNS服务器 ===
    if (sendDnsPacket(server_socket,upstream_addr,dns_entity) != MYSUCCESS) {
        log_error("Failed to forward request to upstream (new_id=%d)", new_id);
        // 转发失败，清理刚创建的映射
        remove_mapping(&g_mapping_table, new_id);
    } else {
        log_debug("Successfully forwarded request with new_id=%d", new_id);
    }
        
    // === 重置地址长度供下次使用 ===
    client_addr_len = sizeof(client_addr);
}

/**
 * @brief 处理上游服务器响应
 * 
 * 这个函数处理来自上游DNS服务器的响应，实现了：
 * 1. 批量处理所有等待的响应（非阻塞接收）
 * 2. 根据响应ID查找对应的客户端映射
 * 3. 恢复原始Transaction ID
 * 4. 将响应转发回原始客户端
 * 5. 清理完成的映射关系
 * 
 * 处理流程：
 * 上游响应 -> 接收 -> 查找映射 -> 恢复原始ID -> 转发客户端 -> 清理映射
 * 
 * @param server_socket 服务器socket，用于向客户端发送响应
 */
void handle_upstream_responses(DNS_ENTITY* dns_entity,struct sockaddr_in source_addr,int source_len,int response_len) 
{

    // === 批量处理所有等待的上游响应 ===
    /*
     * 与处理客户端请求类似，批量处理所有等待的响应。
     * 这样可以减少select()调用次数，提高处理效率。
     */
    log_debug("Received DNS response 1 from upstream (%d bytes)", response_len);
        
    
    // === 提取响应Transaction ID ===
    // 这是我们之前分配给上游请求的新ID
    unsigned short response_id = dns_entity->id;
    log_debug("Processing response with ID=%d", response_id);
    
    // === 查找对应的映射关系 ===
    /*
     * 根据响应ID查找原始客户端信息。
     * 如果找不到映射，可能的原因：
     * 1. 响应ID错误或损坏
     * 2. 映射已过期被清理
     * 3. 收到重复响应
     * 4. 恶意或错误的响应
     */
    dns_mapping_entry_t* mapping = find_mapping_by_new_id(&g_mapping_table, response_id);
    if (!mapping) {
        log_warn("No mapping found for response ID %d, discarding response", response_id);
        source_len = sizeof(source_addr);  // 重置地址长度
        return ;
    }
        
    // === 恢复原始Transaction ID ===
    /*
     * 将响应中的ID改回客户端原始的Transaction ID，
     * 这样客户端就能正确识别和处理响应。
     */
    unsigned short original_id = mapping->original_id;
    dns_entity->id = original_id;
    
    log_debug("Restored response ID: %d -> %d for client %s:%d", 
             response_id, original_id,
             inet_ntoa(mapping->client_addr.sin_addr), 
             ntohs(mapping->client_addr.sin_port));
    
    // === 将响应发送给原始客户端 ===
    /*
     * 使用映射中保存的客户端地址信息，
     * 将响应准确发送回原始请求的客户端。
     */
    

if (sendDnsPacket(server_socket, mapping->client_addr, dns_entity) == MYERROR) {
        int send_error = WSAGetLastError();
        log_error("Failed to send response to client %s:%d: %d",
                 inet_ntoa(mapping->client_addr.sin_addr), 
                 ntohs(mapping->client_addr.sin_port), send_error);
    } else {
        log_info("Response sent to client %s:%d (%d bytes, original_id=%d)",
                inet_ntoa(mapping->client_addr.sin_addr), 
                ntohs(mapping->client_addr.sin_port), response_len, original_id);
    }
        
    // === 清理完成的映射关系 ===
    /*
     * DNS请求-响应周期完成，清理映射以释放资源。
     * 这是关键步骤，防止映射表泄漏。
     */
    remove_mapping(&g_mapping_table, response_id);
        
    // === 重置地址长度供下次使用 ===
    source_len = sizeof(source_addr);

}

