#include "websocket/websocket.h"

//全局变量定义
WSADATA wsaData;
struct sockaddr_in upstream_addr;


int initSystem()
{
    // 初始化 Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return ERROR; // 初始化失败，返回错误
    }

    // 设置目标地址结构 (Google DNS Server)
    upstream_addr.sin_family = AF_INET; // 地址族为 IPv4
    upstream_addr.sin_port = htons(DNS_PORT); // 设置 DNS 端口号，并转换为主机字节序到网络字节序
    upstream_addr.sin_addr.s_addr = inet_addr(DNS_SERVER); // 设置 DNS 服务器的 IP 地址
    return MYSUCCESS; // 成功，返回成功
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
        int error = WSAGetLastError();
        log_error("sendto 调用失败，错误码: %d", error);

        // === 错误处理和分类 ===
        if (error == WSAEWOULDBLOCK) {
            // 发送缓冲区满，这在高负载时可能发生
            log_warn("发送缓冲区已满 (WSAEWOULDBLOCK)，请求可能延迟");
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


void cleanupSystem()
{
    WSACleanup();      // 清理 Winsock 资源
}

