#include "websocket/dnsServer.h"
#include "platform/platform.h"
#include "Thread/thread_pool.h"
#include "DNScache/relayBuild.h"
#include <time.h>  // 添加时间相关的头文件支持
#include <string.h> // 包含 memset 和 strcmp


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

// 线程池实例（用于多线程处理）
static dns_thread_pool_t g_dns_thread_pool;


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
    log_debug("收到来自 %s:%d 的DNS请求 (%d 字节)",
            inet_ntoa(client_addr.sin_addr), 
            ntohs(client_addr.sin_port), request_len);

    log_debug("%s",dns_entity_to_string(dns_entity));
    // 查询本地表
    dns_query_response_t* response = dns_relay_query(dns_entity->questions->qname,dns_entity->questions->qtype);
    DNS_ENTITY* result;
    switch (response->result_type){
        case QUERY_RESULT_BLOCKED:
            log_debug("响应来源: 域名被屏蔽 - 返回域名不存在");
            result = build_response(dns_entity,"0.0.0.0");
            break;
        case QUERY_RESULT_LOCAL_HIT:
            log_debug("响应来源: 本地域名表命中 - IP: %s", response->resolved_ip);
            result = build_response(dns_entity,response->resolved_ip);
            break;
        case QUERY_RESULT_CACHE_HIT:
            log_debug("响应来源: 缓存命中 - 直接返回缓存结果");
            result = response->dns_response;
            break;
        case QUERY_RESULT_CACHE_MISS:
            log_debug("响应来源: 缓存未命中 - 需要向上游DNS服务器查询");
            log_debug("%s",dns_entity_to_string(dns_entity));
            log_debug("原始ID: %d",dns_entity->id);
            // === 提取并验证原始Transaction ID ===
            // DNS头部的前2个字节是Transaction ID（网络字节序）
            unsigned short original_id = dns_entity->id;
            unsigned short new_id;
                
            // === 创建ID映射关系 ===      
            if (thread_pool_add_mapping_safe(original_id, &client_addr, client_addr_len, &new_id) != MYSUCCESS) {
                log_error("为来自 %s:%d 的请求添加映射失败 (原始ID=%d)",
                            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), original_id);
                client_addr_len = sizeof(client_addr);  // 重置地址长度
                return;
            }    
            // === 修改请求的Transaction ID ===
            dns_entity->id = new_id;
            log_debug("修改请求ID: %d -> %d", original_id, new_id);
            
            // === 转轮询请求到随机选择的上游DNS服务器 ===
            if (sendDnsPacketToNextUpstream(server_socket, dns_entity) != MYSUCCESS) {
                log_error("转发请求到上游服务器失败 (新ID=%d)", new_id);
                // 转发失败，清理刚创建的映射
                thread_pool_remove_mapping_safe(new_id);
            } else {
                log_debug("成功转发请求到随机上游服务器，上游ID=%d", new_id);
            }
            break;
    }

    if(response->result_type !=QUERY_RESULT_CACHE_MISS)
    {
        result->id = dns_entity->id;
        if (sendDnsPacket(server_socket, client_addr, result) == MYERROR) 
        {
            int send_error = platform_get_last_error();
            log_error("向客户端 %s:%d 发送响应失败: %d",
            inet_ntoa(client_addr.sin_addr), 
            ntohs(client_addr.sin_port), send_error);
        } 
        else 
        {
            log_info("已向客户端 %s:%d 发送响应 (原始ID=%d)",
            inet_ntoa(client_addr.sin_addr), 
            ntohs(client_addr.sin_port), dns_entity->id);
        }
    }

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
{    // === 批量处理所有等待的上游响应 ===
    /*
     * 与处理客户端请求类似，批量处理所有等待的响应。
     * 这样可以减少select()调用次数，提高处理效率。
     */
    (void)source_addr; // 标记参数已使用，避免编译警告
    (void)source_len;  // 标记参数已使用，避免编译警告=== 提取响应Transaction ID ===
    // 这是我们之前分配给上游请求的新ID
    unsigned short response_id = dns_entity->id;
    log_debug("响应来源: 上游DNS服务器 - 处理响应ID: %d", response_id);
    log_debug("正在处理上游ID为 %d 的响应", response_id);
    log_debug("%s",dns_entity_to_string(dns_entity));
    dns_mapping_entry_t* mapping = thread_pool_find_mapping_safe(response_id);
    if (!mapping) {
        log_warn("未找到响应ID %d 对应的映射，丢弃响应", response_id);
        source_len = sizeof(source_addr);  // 重置地址长度
        return ;
    }    
    // === 恢复原始Transaction ID ===
     unsigned short original_id = mapping->original_id;
    dns_entity->id = original_id;
    
    log_debug("恢复响应ID: %d -> %d，目标客户端 %s:%d", 
             response_id, original_id,
             inet_ntoa(mapping->client_addr.sin_addr), 
             ntohs(mapping->client_addr.sin_port));
    
    if (sendDnsPacket(server_socket, mapping->client_addr, dns_entity) == MYERROR) 
    {
        int send_error = platform_get_last_error();
        log_error("向客户端 %s:%d 发送响应失败: %d",
        inet_ntoa(mapping->client_addr.sin_addr), 
        ntohs(mapping->client_addr.sin_port), send_error);
    } 
    else 
    {
        log_info("已向客户端 %s:%d 发送响应 (%d 字节，原始ID=%d)",
        inet_ntoa(mapping->client_addr.sin_addr), 
        ntohs(mapping->client_addr.sin_port), response_len, original_id);
    }
        
    // === 清理完成的映射关系 ===
    thread_pool_remove_mapping_safe(response_id);
    
    // === 将查询结果插入缓存 ===
    if (dns_relay_cache_response(dns_entity->questions->qname, dns_entity->questions->qtype, dns_entity) != MYSUCCESS) {
        log_warn("将响应缓存失败: %s", dns_entity->questions->qname);
    } else {
        log_debug("已将响应缓存: %s", dns_entity->questions->qname);
    }


}

/**
 * @brief 多线程版本的DNS代理服务器主函数
 * 
 * 这是DNS代理服务器的多线程版本，实现了：
 * 1. I/O线程负责网络数据接收和发送
 * 2. 工作线程池负责DNS数据包解析和处理
 * 3. 线程安全的ID映射表操作
 * 4. 高并发性能和多核CPU利用
 * 
 * 架构特点：
 * - 主线程作为I/O线程，专门处理网络事件
 * - 工作线程池处理CPU密集型任务
 * - 线程安全的资源访问
 * - 优雅的关闭机制
 * 
 * @return int 成功返回 MYSUCCESS，失败返回 MYERROR
 */
int start_dns_proxy_server_threaded() {
    struct sockaddr_in server_addr; // 服务器地址结构    
    log_info("=== 启动多线程DNS代理服务器 ===");    
    

    // === 第一步：初始化映射表 ===
    init_mapping_table(&g_mapping_table);
    log_debug("初始化映射表 (最大并发请求数: %d)", MAX_CONCURRENT_REQUESTS);


    // === 第二步：创建服务器socket ===
    server_socket = create_socket();
    if (server_socket == INVALID_SOCKET) {
        log_error("创建SOCKET失败，错误代码: %d", platform_get_last_error());
        return MYERROR;
    }
    log_debug("SOCKET 创建成功");

    // === 第三步：设置服务器socket为非阻塞模式 ===
    if (set_socket_nonblocking(server_socket) == SOCKET_ERROR) {
        log_error("设置socket非阻塞失败: %d", platform_get_last_error());
        closesocket(server_socket);
        return MYERROR;
    }
    log_debug("设置socket非阻塞成功");

    // === 第四步：设置socket选项 ===
    if (set_socket_reuseaddr(server_socket) == SOCKET_ERROR) {
        log_warn("setsockopt(SO_REUSEADDR) 失败，错误码: %d", platform_get_last_error());
    }

    // === 第五步：配置并绑定服务器地址 ===
    memset(&server_addr, 0, sizeof(server_addr)); // 清零结构体
    server_addr.sin_family = AF_INET;              // IPv4协议族
    server_addr.sin_addr.s_addr = INADDR_ANY;      // 监听所有网络接口
    server_addr.sin_port = htons(DNS_PORT);        // DNS标准端口53

    // 将套接字绑定到指定的IP地址和端口
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("监听端口号 %d 失败，错误码: %d", DNS_PORT, platform_get_last_error());
        closesocket(server_socket);
        return MYERROR;
    }

    log_info("DNS服务成功绑定到端口: %d", DNS_PORT);

    // === 第六步：初始化线程池 ===
    int worker_count = 0; // 使用默认线程数（基于CPU核心数）
    int queue_size = 0;   // 使用默认队列大小
    
    if (thread_pool_init(&g_dns_thread_pool, worker_count, queue_size, 
                        server_socket, &g_mapping_table) != MYSUCCESS) {
        log_error("线程池初始化失败");
        closesocket(server_socket);
        return MYERROR;
    }

    // 设置全局线程池实例，以便其他模块可以访问互斥锁
    thread_pool_set_global_instance(&g_dns_thread_pool);

    // === 第七步：启动线程池 ===
    if (thread_pool_start(&g_dns_thread_pool) != MYSUCCESS) {
        log_error("线程池启动失败");
        thread_pool_destroy(&g_dns_thread_pool);
        closesocket(server_socket);
        return MYERROR;
    }

    // === 第八步：主I/O事件循环 ===
    /*
     * 在多线程架构中，主线程专门负责I/O操作：
     * 1. 监听网络事件（socket可读）
     * 2. 接收UDP数据包
     * 3. 将数据包封装成任务并提交给线程池
     * 4. 定期执行维护任务
     * 
     * 这种设计的优势：
     * - I/O操作不被CPU密集型任务阻塞
     * - 工作线程专注于数据处理
     * - 充分利用多核CPU
     * - 提高系统整体吞吐量
     */
    int server_running = 1;
    time_t last_cleanup = time(NULL);
    time_t last_status_print = time(NULL);
    
    while (server_running) {
        fd_set read_fds;        // 可读文件描述符集合
        struct timeval timeout; // select()超时设置
        
        // === 准备监听的socket集合 ===
        FD_ZERO(&read_fds);                    // 清空文件描述符集合
        FD_SET(server_socket, &read_fds);      // 添加服务器socket
        
        // === 设置select超时时间 ===
        // 较短的超时时间确保及时响应维护任务
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        // === 调用select()等待网络事件 ===
        int activity = select(server_socket + 1, &read_fds, NULL, NULL, &timeout);
        
        // === 处理select()返回值 ===
        if (activity == SOCKET_ERROR) {
            log_error("select() 调用失败，错误码: %d", platform_get_last_error());
            break; // 发生严重错误，退出主循环
        }
        
        // === 处理网络数据接收 ===
        if (activity > 0 && FD_ISSET(server_socket, &read_fds)) {
            handle_receive_threaded();
        }
        
        // === 定期维护任务 ===
        time_t current_time = time(NULL);
        
        // 每10秒清理一次过期映射
        if (current_time - last_cleanup > 10) {
            thread_pool_cleanup_mappings_safe();
            last_cleanup = current_time;
            log_debug("定期清理过期映射完成");
        }
        
        // 每30秒打印一次服务器状态
        if (current_time - last_status_print > 30) {
            thread_pool_print_status(&g_dns_thread_pool);
            last_status_print = current_time;
        }
    }    // === 清理资源 ===
    log_info("正在关闭多线程DNS代理服务器...");
    
    // 停止线程池（给工作线程5秒时间完成当前任务）
    thread_pool_stop(&g_dns_thread_pool, 5000);
    
    // 销毁线程池
    thread_pool_destroy(&g_dns_thread_pool);
    
    // 清除全局线程池实例
    thread_pool_set_global_instance(NULL);
    
    // 清理DNS上游服务器池
    upstream_pool_destroy(&g_upstream_pool);
    log_debug("DNS上游服务器池已清理");

    // 清理DNS缓存
    dns_cache_destroy();

    // 关闭服务器socket
    closesocket(server_socket);
    
    log_info("多线程DNS代理服务器已关闭");
    return MYSUCCESS;
}

/**
 * @brief 多线程版本的网络数据接收处理函数
 * 
 * 与单线程版本的主要区别：
 * 1. 不直接解析和处理DNS数据包
 * 2. 将接收到的原始数据提交给线程池
 * 3. 由工作线程异步处理具体的DNS逻辑
 * 4. 提高I/O处理的响应速度
 */
int handle_receive_threaded() {
    char receive_buffer[BUF_SIZE]; // 存储接收数据的缓冲区
    struct sockaddr_in source_addr; // 接收数据的源地址
    socklen_t source_addr_len = sizeof(source_addr);
    int receive_len = 0; // 接收到的数据长度
    int receive_processed = 0;     // 本次处理的数据计数

    // === 批量处理接收到的数据 ===
    // 使用while循环确保一次select事件中处理所有可用数据
    while((receive_len = recvfrom(server_socket, receive_buffer, BUF_SIZE, 0, 
                                 (struct sockaddr*)&source_addr, &source_addr_len)) > 0) {
        receive_processed++;
        char* source_ip = inet_ntoa(source_addr.sin_addr);
        
        // === 验证请求数据完整性 ===
        if (receive_len < 2) {
            log_warn("请求数据过短 (%d 字节)，来源: %s，忽略处理", receive_len, source_ip);
            source_addr_len = sizeof(source_addr);  // 重置地址长度
            continue;
        }

        // === 确定任务类型 ===
        task_type_t task_type;
        if (upstream_pool_contains_server(&g_upstream_pool, source_ip) == 0) {
            // 来自客户端的请求
            task_type = TASK_CLIENT_REQUEST;
            log_debug("收到客户端请求: %s, 长度: %d 字节", source_ip, receive_len);
        } else {
            // 来自上游DNS服务器的响应
            task_type = TASK_UPSTREAM_RESPONSE;
            log_debug("收到上游响应: %s, 长度: %d 字节", source_ip, receive_len);
        }

        // === 提交任务到线程池 ===
        if (thread_pool_submit_task(&g_dns_thread_pool, receive_buffer, receive_len,
                                   source_addr, source_addr_len, task_type) != MYSUCCESS) {
            log_warn("任务提交失败，可能是队列已满，来源: %s", source_ip);
        }

        // 重置地址长度，为下一次接收准备
        source_addr_len = sizeof(source_addr);
    }

    // 记录批量处理结果
    if (receive_processed > 0) {
        log_debug("批量接收处理完成，本次处理: %d 个数据包", receive_processed);
    }

    // 检查是否是由于错误而退出循环
    if (receive_len < 0) {
        int error = platform_get_last_error();
        // 对于非阻塞socket，EAGAIN/EWOULDBLOCK是正常的
#ifdef _WIN32
        if (error != WSAEWOULDBLOCK) {
#else
        if (error != EAGAIN && error != EWOULDBLOCK) {
#endif
            log_error("recvfrom() 失败，错误码: %d", error);
            return MYERROR;
        }
    }
    
    return MYSUCCESS;
}

