#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define DNS_PROXY_PORT 53
#define BUF_SIZE 512

// 简单的DNS查询生成函数
int create_dns_query(char* buffer, const char* hostname, unsigned short qtype) {
    int offset = 0;
    
    // DNS头部
    *((unsigned short*)(buffer + offset)) = htons(0x1234); // ID
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(0x0100); // 标准查询，递归
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(1);      // 1个问题
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(0);      // 0个回答
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(0);      // 0个授权
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(0);      // 0个附加
    offset += 2;
    
    // 格式化域名
    const char* start = hostname;
    const char* end;
    while (*start) {
        end = strchr(start, '.');
        if (!end) end = start + strlen(start);
        
        int len = end - start;
        buffer[offset++] = (char)len;
        memcpy(buffer + offset, start, len);
        offset += len;
        
        if (*end) start = end + 1;
        else break;
    }
    buffer[offset++] = 0; // 结束符
    
    // 查询类型和类
    *((unsigned short*)(buffer + offset)) = htons(qtype);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(1); // IN class
    offset += 2;
    
    return offset;
}

int main() {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server_addr;
    char query_buffer[BUF_SIZE];
    char response_buffer[BUF_SIZE];
    int query_len, response_len;
    
    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
    
    // 创建UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Failed to create socket\n");
        WSACleanup();
        return 1;
    }
    
    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DNS_PROXY_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // 创建DNS查询
    printf("Testing DNS Proxy Server on port %d\n", DNS_PROXY_PORT);
    printf("Querying www.baidu.com (A record)...\n");
    
    query_len = create_dns_query(query_buffer, "www.baidu.com", 1); // A记录
    
    // 发送查询
    if (sendto(sock, query_buffer, query_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Failed to send query: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    
    printf("Query sent successfully (%d bytes)\n", query_len);
    
    // 接收响应
    struct sockaddr_in source_addr;
    int source_len = sizeof(source_addr);
    response_len = recvfrom(sock, response_buffer, BUF_SIZE, 0, (struct sockaddr*)&source_addr, &source_len);
    
    if (response_len == SOCKET_ERROR) {
        printf("Failed to receive response: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    
    printf("Response received successfully (%d bytes)\n", response_len);
    
    // 简单解析响应ID
    unsigned short response_id = ntohs(*((unsigned short*)response_buffer));
    unsigned short flags = ntohs(*((unsigned short*)(response_buffer + 2)));
    unsigned short ancount = ntohs(*((unsigned short*)(response_buffer + 6)));
    
    printf("Response ID: 0x%04X\n", response_id);
    printf("Flags: 0x%04X\n", flags);
    printf("Answer count: %d\n", ancount);
    
    if (flags & 0x8000) {
        printf("This is a DNS response\n");
    }
    
    if (ancount > 0) {
        printf("DNS query successful - received %d answer(s)\n", ancount);
    } else {
        printf("No answers in response\n");
    }
    
    closesocket(sock);
    WSACleanup();
    
    return 0;
}
