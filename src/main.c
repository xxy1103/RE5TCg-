#include "platform/platform.h"
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
    set_log_level(LOG_LEVEL_INFO);
    
    log_info("正在启动多线程DNS代理服务器...");
    log_info("本版本特性：");
    log_info("  - 多线程并行处理");
    log_info("  - I/O与计算分离");
    log_info("  - 线程安全的ID映射");
    log_info("  - 高并发性能");

    // 初始化平台资源
    platform_init();

    // 启动多线程DNS代理服务器
    if (start_dns_proxy_server_threaded() != MYSUCCESS) {
        log_error("多线程DNS代理服务器启动失败");
        platform_cleanup(); // 清理已初始化的资源
        return 1; // 启动失败，程序退出
    }
    
    // 清理平台资源
    platform_cleanup();
    
    // 清理日志文件
    cleanup_log_file();
    
    log_info("多线程DNS代理服务器已正常退出");
    return 0; // 程序正常结束
}
