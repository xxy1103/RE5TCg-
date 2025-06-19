// 包含自定义的 WebSocket 头文件，定义了相关函数和结构体
#include "websocket/websocket.h" 
// 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数
#include "websocket/datagram.h" 
#include "websocket/dnsServer.h" // 包含 DNS 服务器相关函数声明

#include <stdio.h>    // 包含标准输入输出头文件
#include <time.h>     // 包含时间相关的头文件，用于生成随机数种子
#include <string.h>   // 包含字符串处理函数
#include <stdlib.h>   // 包含内存分配函数
#include "debug/debug.h"   // 包含调试相关的头文件

/**
 * @brief 程序主入口点。
 * 
 * @return int 成功返回 0，失败返回 1。
 */
int main() {    
    // 初始化日志文件
    init_log_file();
      // 设置日志记录级别为 DEBUG 以查看更多信息
    set_log_level(LOG_LEVEL_DEBUG);
    
    log_info("正在启动DNS代理服务器...");

    // 初始化系统资源，例如 Winsock
    if (initSystem() != MYSUCCESS) {
        log_error("系统初始化失败");
        return 1; // 初始化失败，程序退出
    }

    // 启动DNS代理服务器的核心逻辑
    if (start_dns_proxy_server() != MYSUCCESS) {
        log_error("DNS代理服务器启动失败");
        cleanupSystem(); // 清理已初始化的资源
        return 1; // 启动失败，程序退出
    }// 清理系统资源
    cleanupSystem();
    
    // 清理日志文件
    cleanup_log_file();
    
    return 0; // 程序正常结束
}
