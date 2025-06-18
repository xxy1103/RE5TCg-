#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件，定义了相关函数和结构体
#include "websocket/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数
#include <stdio.h>    // 包含标准输入输出头文件
#include <time.h>     // 包含时间相关的头文件，用于生成随机数种子
#include <string.h>   // 包含字符串处理函数
#include <stdlib.h>   // 包含内存分配函数
#include "debug/debug.h"   // 包含调试相关的头文件
#define DNS_SERVER "8.8.8.8"  // 定义 Google 的公共 DNS 服务器地址
#define LOCAL_DNS_PORT 53     // 定义本地DNS代理监听端口（标准DNS端口）
#define UPSTREAM_DNS_PORT 53  // 定义上游DNS服务器端口
#define BUF_SIZE 65536        // 定义缓冲区大小，用于存储发送和接收的数据

// DNS代理服务器函数
int start_dns_proxy_server();
int handle_dns_request(char* request_buffer, int request_len, struct sockaddr_in* client_addr, int client_addr_len, SOCKET server_socket);
int forward_to_upstream_dns(char* request_buffer, int request_len, char* response_buffer, int* response_len);

int main() {
    set_log_level(LOG_LEVEL_INFO);
    
    log_info("Starting DNS Proxy Server...");

    // 初始化系统
    if (initSystem() != MYSUCCESS) {
        log_error("Failed to initialize system.");
        return 1;
    }

    // 启动DNS代理服务器
    if (start_dns_proxy_server() != MYSUCCESS) {
        log_error("Failed to start DNS proxy server.");
        cleanupSystem();
        return 1;
    }

    // 清理
    cleanupSystem();
    return 0; // 程序正常结束
}

// DNS代理服务器主函数
int start_dns_proxy_server() {
    SOCKET server_socket;
    struct sockaddr_in server_addr, client_addr;
    char request_buffer[BUF_SIZE];
    int client_addr_len;
    int request_len;

    // 创建UDP socket
    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket == INVALID_SOCKET) {
        log_error("Failed to create socket: %d", WSAGetLastError());
        return MYERROR;
    }

    // 设置 SO_REUSEADDR 选项，尝试允许端口重用
    int optval = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) == SOCKET_ERROR) {
        log_warn("setsockopt(SO_REUSEADDR) failed with error: %d", WSAGetLastError());
        // 即使设置失败，也继续尝试绑定
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有接口
    server_addr.sin_port = htons(LOCAL_DNS_PORT);

    // 绑定socket到本地53端口
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_error("Failed to bind socket to port %d: %d", LOCAL_DNS_PORT, WSAGetLastError());
        closesocket(server_socket);
        return MYERROR;
    }

    log_info("DNS Proxy Server listening on port %d...", LOCAL_DNS_PORT);

    // 主循环：接收和处理DNS请求
    while (1) {
        client_addr_len = sizeof(client_addr);
        
        // 接收DNS请求
        request_len = recvfrom(server_socket, request_buffer, BUF_SIZE, 0, 
                              (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (request_len == SOCKET_ERROR) {
            log_error("Failed to receive DNS request: %d", WSAGetLastError());
            continue;
        }

        log_info("Received DNS request from %s:%d (%d bytes)", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), request_len);

        // 处理DNS请求
        if (handle_dns_request(request_buffer, request_len, &client_addr, 
                              client_addr_len, server_socket) != MYSUCCESS) {
            log_warn("Failed to handle DNS request from %s:%d", 
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }
    }

    closesocket(server_socket);
    return MYSUCCESS;
}

// 处理DNS请求
int handle_dns_request(char* request_buffer, int request_len, 
                      struct sockaddr_in* client_addr, int client_addr_len, 
                      SOCKET server_socket) {
    char response_buffer[BUF_SIZE];
    int response_len = 0;

    // 1. 解析收到的DNS请求为DNS_ENTITY
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
    
    if (request_entity->questions && request_entity->qdcount > 0) {
        for (int i = 0; i < request_entity->qdcount; i++) {
            DNS_QUESTION_ENTITY* question = &request_entity->questions[i];
            log_info("Question %d: Name=%s, Type=%d, Class=%d", 
                    i + 1, question->qname, question->qtype, question->qclass);
        }
    }

    // 3. 将请求转发给上游DNS服务器
    if (forward_to_upstream_dns(request_buffer, request_len, 
                               response_buffer, &response_len) != MYSUCCESS) {
        log_error("Failed to forward request to upstream DNS server");
        free_dns_entity(request_entity);
        return MYERROR;
    }

    // 4. 解析收到的响应为DNS_ENTITY
    DNS_ENTITY* response_entity = parse_dns_packet(response_buffer, response_len);
    if (!response_entity) {
        log_error("Failed to parse DNS response from upstream server");
        free_dns_entity(request_entity);
        return MYERROR;
    }

    // 5. 打印响应信息
    log_info("=== Received DNS Response from Upstream ===");
    log_info("Transaction ID: %d", response_entity->id);
    log_info("Flags: 0x%04X", response_entity->flags);
    log_info("Questions: %d, Answers: %d", response_entity->qdcount, response_entity->ancount);
    
    // 显示答案
    if (response_entity->ancount > 0) {
        log_info("--- Answers ---");
        for (int i = 0; i < response_entity->ancount; i++) {
            R_DATA_ENTITY* answer = &response_entity->answers[i];
            
            if (answer->type == A && answer->data_len == 4) {
                unsigned char* ip_addr = (unsigned char*)answer->rdata;
                log_info("Answer %d: %s -> %d.%d.%d.%d (TTL: %lu)", 
                        i + 1, answer->name, ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], 
                        (unsigned long)answer->ttl);            } else if (answer->type == AAAA && answer->data_len == 16) {
                log_info("Answer %d: %s -> IPv6 (TTL: %lu)", i + 1, answer->name, (unsigned long)answer->ttl);
            } else if (answer->type == CNAME) {
                log_info("Answer %d: %s -> CNAME: %s (TTL: %lu)", 
                        i + 1, answer->name, answer->rdata, (unsigned long)answer->ttl);
            } else {
                log_info("Answer %d: %s -> Type %d (TTL: %lu)", 
                        i + 1, answer->name, answer->type, (unsigned long)answer->ttl);
            }
        }
    }

    // 6. 将响应发送给请求的客户端
    if (sendto(server_socket, response_buffer, response_len, 0, 
              (struct sockaddr*)client_addr, client_addr_len) == SOCKET_ERROR) {
        log_error("Failed to send DNS response to client: %d", WSAGetLastError());
        free_dns_entity(request_entity);
        free_dns_entity(response_entity);
        return MYERROR;
    }

    log_info("DNS response sent to client %s:%d (%d bytes)", 
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), response_len);

    // 清理资源
    free_dns_entity(request_entity);
    free_dns_entity(response_entity);
    
    return MYSUCCESS;
}

// 转发请求到上游DNS服务器
int forward_to_upstream_dns(char* request_buffer, int request_len, 
                           char* response_buffer, int* response_len) {
    SOCKET upstream_socket;
    struct sockaddr_in upstream_addr;
    int received_len;

    // 创建socket连接到上游DNS服务器
    upstream_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (upstream_socket == INVALID_SOCKET) {
        log_error("Failed to create upstream socket: %d", WSAGetLastError());
        return MYERROR;
    }

    // 设置上游DNS服务器地址
    memset(&upstream_addr, 0, sizeof(upstream_addr));
    upstream_addr.sin_family = AF_INET;
    upstream_addr.sin_port = htons(UPSTREAM_DNS_PORT);
    upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER);

    // 发送请求到上游DNS服务器
    if (sendto(upstream_socket, request_buffer, request_len, 0, 
              (struct sockaddr*)&upstream_addr, sizeof(upstream_addr)) == SOCKET_ERROR) {
        log_error("Failed to send request to upstream DNS server: %d", WSAGetLastError());
        closesocket(upstream_socket);
        return MYERROR;
    }

    log_info("Request forwarded to upstream DNS server %s:%d", DNS_SERVER, UPSTREAM_DNS_PORT);

    // 接收响应
    struct sockaddr_in source_addr;
    int source_len = sizeof(source_addr);
    received_len = recvfrom(upstream_socket, response_buffer, BUF_SIZE, 0, 
                           (struct sockaddr*)&source_addr, &source_len);
    
    if (received_len == SOCKET_ERROR) {
        log_error("Failed to receive response from upstream DNS server: %d", WSAGetLastError());
        closesocket(upstream_socket);
        return MYERROR;
    }

    *response_len = received_len;
    log_info("Received response from upstream DNS server (%d bytes)", received_len);

    closesocket(upstream_socket);
    return MYSUCCESS;
}