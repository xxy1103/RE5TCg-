#include "websocket/websocket.h"

//全局变量定义
WSADATA wsaData;
struct sockaddr_in dest;


int initSystem()
{
    // 初始化 Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return ERROR; // 初始化失败，返回错误
    }

    // 设置目标地址结构 (Google DNS Server)
    dest.sin_family = AF_INET; // 地址族为 IPv4
    dest.sin_port = htons(DNS_PORT); // 设置 DNS 端口号，并转换为主机字节序到网络字节序
    dest.sin_addr.s_addr = inet_addr(DNS_SERVER); // 设置 DNS 服务器的 IP 地址
    return MYSUCCESS; // 成功，返回成功
}




SOCKET sendDnsRequest(const char* hostname, unsigned short qtype)
{
    char buf[BUF_SIZE];      // 用于发送数据的缓冲区
    
    // 创建 UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET; // 创建失败，返回无效socket
    }
    
    // 使用DNS_ENTITY创建DNS查询
    DNS_ENTITY* dns_query = create_dns_query(hostname, qtype);
    if (!dns_query) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    // 将DNS_ENTITY序列化为字节流
    int query_len = serialize_dns_packet(buf, dns_query);
    if (query_len <= 0) {
        free_dns_entity(dns_query);
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    // 释放DNS查询实体
    free_dns_entity(dns_query);

    // 使用 sendto 函数通过 UDP 发送数据
    if (sendto(sock, buf, query_len, 0, (struct sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR) {
        printf("sendto failed with error: %d\n", WSAGetLastError());
        closesocket(sock); // 关闭 socket
        return INVALID_SOCKET; // 发送失败，返回无效socket
    }

    printf("DNS query sent successfully.\n");
    return sock; // 返回socket用于后续接收响应
}


int parseDnsResponse(SOCKET sock, const char* hostname, int query_len)
{
    char buf[BUF_SIZE];      // 用于接收数据的缓冲区
    
    // 接收 DNS 响应
    struct sockaddr_in source; // 存储响应来源的地址信息
    int source_len = sizeof(source);
    // 使用 recvfrom 函数接收 UDP 数据
    int recv_len = recvfrom(sock, buf, BUF_SIZE, 0, (struct sockaddr *)&source, &source_len);
    if (recv_len == SOCKET_ERROR) {
        printf("recvfrom failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        return MYERROR;
    }
    printf("DNS response received (%d bytes).\n", recv_len);

    // 使用DNS_ENTITY解析DNS响应
    DNS_ENTITY* dns_response = parse_dns_packet(buf, recv_len);
    if (!dns_response) {
        printf("Failed to parse DNS response.\n");
        closesocket(sock);
        return MYERROR;
    }

    // 检查是否是响应报文
    if ((dns_response->flags & 0x8000) == 0) {
        printf("Not a DNS response.\n");
        free_dns_entity(dns_response);
        closesocket(sock);
        return MYERROR;
    }    printf("\n--- DNS Response --- \n");
    printf("Transaction ID: %d\n", dns_response->id);
    printf("Questions: %d\n", dns_response->qdcount);
    printf("Answers: %d\n", dns_response->ancount);
    
    // 显示问题部分
    if (dns_response->questions && dns_response->qdcount > 0) {
        printf("\n--- Questions ---\n");
        for (int i = 0; i < dns_response->qdcount; i++) {
            DNS_QUESTION_ENTITY* question = &dns_response->questions[i];
            printf("Question %d:\n", i + 1);
            printf("  Name: %s\n", question->qname);
            printf("  Type: %d\n", question->qtype);
            printf("  Class: %d\n", question->qclass);
        }
    }
    
    // 遍历所有回答记录
    if (dns_response->ancount > 0) {
        printf("\n--- Answers ---\n");
    }    for (int i = 0; i < dns_response->ancount; i++) {
        R_DATA_ENTITY* answer = &dns_response->answers[i];
        
        printf("\nAnswer %d:\n", i + 1);
        printf("  Name: %s\n", answer->name);
        printf("  Type: %d", answer->type);
        
        // 显示记录类型名称
        if (answer->type == A) {
            printf(" (A)\n");
        } else if (answer->type == CNAME) {
            printf(" (CNAME)\n");
        } else if (answer->type == AAAA) {
            printf(" (AAAA)\n");
        } else if (answer->type == MX) {
            printf(" (MX)\n");
        } else {
            printf("\n");
        }
        
        printf("  TTL: %lu seconds\n", (unsigned long)answer->ttl);
          // 根据记录类型解析数据
        if (answer->type == A && answer->data_len == 4) {
            // A记录 - IPv4地址
            unsigned char* ip_addr = (unsigned char*)answer->rdata;
            printf("  IPv4 Address: %d.%d.%d.%d\n", 
                   ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
        } else if (answer->type == CNAME) {
            // CNAME记录 - 已经解析为字符串
            printf("  CNAME: %s\n", answer->rdata);
        } else if (answer->type == AAAA && answer->data_len == 16) {
            // AAAA记录 - IPv6地址
            unsigned char* ip_addr = (unsigned char*)answer->rdata;
            printf("  IPv6 Address: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
                   ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
                   ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
                   ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
                   ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15]);
        } else {
            printf("  Data Length: %d bytes\n", answer->data_len);
        }
        
    }
    
    // 清理
    free_dns_entity(dns_response);
    closesocket(sock); // 关闭 socket
    return MYSUCCESS; // 返回成功状态
}

int sendDnsQuery(const char* hostname, unsigned short qtype)
{
    // 发送DNS请求
    SOCKET sock = sendDnsRequest(hostname, qtype);
    if (sock == INVALID_SOCKET) {
        return MYERROR;
    }

    // 由于我们现在使用ENTITY结构，不需要重新计算查询长度
    // parseDnsResponse函数现在不依赖query_len参数
    return parseDnsResponse(sock, hostname, 0);
}

void cleanupSystem()
{
    WSACleanup();      // 清理 Winsock 资源
}

