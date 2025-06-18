#ifndef WEBSOCKET_H
#define WEBSOCKET_H

//头文件
#include <winsock2.h> // 包含 Winsock2 头文件，用于 Windows Sockets API
#include "debug/debug.h"   // 包含调试相关的头文件
#include "datagram/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数

//定义宏
#define MYSUCCESS 1
#define MYERROR 0
#define DNS_SERVER "8.8.8.8"  // 定义 Google 的公共 DNS 服务器地址
#define DNS_PORT 53           // 定义 DNS 服务使用的标准端口号
#define BUF_SIZE 65536        // 定义缓冲区大小，用于存储发送和接收的数据


//全局变量声明
extern WSADATA wsaData;
extern struct sockaddr_in dest;

// 函数声明
int initSystem();
SOCKET sendDnsRequest(const char* hostname, unsigned short qtype);
int sendDnsQuery(const char* hostname, unsigned short qtype);
int parseDnsResponse(SOCKET sock, const char* hostname, int query_len);
void cleanupSystem();

#endif // WEBSOCKET_H