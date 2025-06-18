#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件，定义了相关函数和结构体
#include "datagram/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数
#include <stdio.h>    // 包含标准输入输出头文件
#include <time.h>     // 包含时间相关的头文件，用于生成随机数种子
#include "debug/debug.h"   // 包含调试相关的头文件
#define DNS_SERVER "8.8.8.8"  // 定义 Google 的公共 DNS 服务器地址
#define DNS_PORT 53           // 定义 DNS 服务使用的标准端口号
#define BUF_SIZE 65536        // 定义缓冲区大小，用于存储发送和接收的数据




int main() {
    set_log_level(LOG_LEVEL_INFO);

    char hostname[] = "www.baidu.com"; // 要查询的域名，Google有IPv6地址

    // 初始化系统
    if (initSystem() != MYSUCCESS) {
        printf("Failed to initialize system.\n");
        return 1;
    }

    // 执行IPv4 DNS查询
    printf("Querying IPv4 DNS (A record) for %s...\n", hostname);
    if (sendDnsQuery(hostname, A) != MYSUCCESS) {
        printf("IPv4 DNS query failed.\n");
    }
    
    printf("\n");
    
    // 执行IPv6 DNS查询
    printf("Querying IPv6 DNS (AAAA record) for %s...\n", hostname);
    if (sendDnsQuery(hostname, AAAA) != MYSUCCESS) {
        printf("IPv6 DNS query failed.\n");
    }

    // 清理
    cleanupSystem();
    return 0; // 程序正常结束
}