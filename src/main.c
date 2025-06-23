#include "platform/platform.h"
// 引入自定义 WebSocket 头文件，包含网络相关函数和结构体
#include "websocket/websocket.h" 
// 引入自定义数据报头文件，包含 DNS 数据报解析结构体和函数
#include "websocket/datagram.h" 
#include "websocket/dnsServer.h" // 包含 DNS 服务器核心处理函数

#include <stdio.h>    // 包含标准输入输出头文件
#include <time.h>     // 包含时间相关的头文件，用于获取当前时间等操作
#include <string.h>   // 包含字符串处理函数
#include <stdlib.h>   // 包含内存管理函数
#include "debug/debug.h"   // 包含调试相关的头文件
#include "websocket/upstream_config.h" // 包含上游DNS配置功能
#include "DNScache/relayBuild.h" // 包含DNS中继核心功能

// 外部全局变量，在websocket.c中定义
extern upstream_dns_pool_t g_upstream_pool;

/**
 * @brief 打印程序使用帮助信息
 */
void print_usage(const char* program_name) {
    printf("DNS中继服务器 - 计算机网络课程设计\n");
    printf("\n使用方法:\n");
    printf("  %s [选项] [DNS服务器IP] [配置文件]\n\n", program_name);
    printf("参数说明:\n");
    printf("  -d              调试级别1 (输出基本信息：时间、序号、客户端IP、查询域名)\n");
    printf("  -dd             调试级别2 (输出详细调试信息)\n");
    printf("  DNS服务器IP     指定上游DNS服务器IP地址 (默认: 202.106.0.20)\n");
    printf("  配置文件        指定域名配置文件路径 (默认: dnsrelay.txt)\n\n");
    printf("示例:\n");
    printf("  %s                              # 使用默认配置\n", program_name);
    printf("  %s -d                           # 调试级别1，使用默认配置\n", program_name);
    printf("  %s -dd 8.8.8.8                  # 调试级别2，指定DNS服务器\n", program_name);
    printf("  %s -d 192.168.1.1 my_dns.txt    # 调试级别1，指定DNS服务器和配置文件\n", program_name);
    printf("\n");
}

/**
 * @brief 验证IP地址格式
 * @param ip_str IP地址字符串
 * @return 有效返回1，无效返回0
 */
int is_valid_ip(const char* ip_str) {
    if (!ip_str) return 0;
    
    int a, b, c, d;
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return 0;
    }
    
    return (a >= 0 && a <= 255) && (b >= 0 && b <= 255) && 
           (c >= 0 && c <= 255) && (d >= 0 && d <= 255);
}

/**
 * @brief 设置默认DNS服务器
 * @param dns_server_ip DNS服务器IP地址
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int set_default_dns_server(const char* dns_server_ip) {
    // 清空当前的服务器池
    g_upstream_pool.server_count = 0;
    g_upstream_pool.current_index = 0;
    
    // 添加指定的DNS服务器
    if (upstream_pool_add_server(&g_upstream_pool, dns_server_ip) != MYSUCCESS) {
        log_error("添加DNS服务器失败: %s", dns_server_ip);
        return MYERROR;
    }
    
    log_info("设置上游DNS服务器: %s", dns_server_ip);
    return MYSUCCESS;
}

/**
 * @brief 程序主入口点
 * 
 * 支持命令行参数：
 * dnsrelay [-d | -dd] [dns-server-ipaddr] [filename]
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 成功返回 0，失败返回 1
 */
int main(int argc, char* argv[]) {
    // === 参数解析变量 ===
    LogLevel debug_level = LOG_LEVEL_INFO;  // 默认日志级别
    const char* dns_server_ip = "202.106.0.20";  // 默认DNS服务器（任务书要求）
    const char* config_file = "dnsrelay.txt";    // 默认配置文件
    int use_custom_dns = 0;  // 是否指定了自定义DNS服务器
    
    // === 解析命令行参数 ===
    int arg_index = 1;
    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-h") == 0 || strcmp(argv[arg_index], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[arg_index], "-d") == 0) {
            debug_level = LOG_LEVEL_DEBUG;
            log_info("启用调试级别1：输出基本调试信息");
            arg_index++;
        }
        else if (strcmp(argv[arg_index], "-dd") == 0) {
            debug_level = LOG_LEVEL_DEBUG;  // 使用相同级别，但可以在日志系统中进一步区分
            log_info("启用调试级别2：输出详细调试信息");
            arg_index++;
        }
        else if (is_valid_ip(argv[arg_index])) {
            dns_server_ip = argv[arg_index];
            use_custom_dns = 1;
            log_info("指定DNS服务器: %s", dns_server_ip);
            arg_index++;
        }
        else {
            // 假设是配置文件路径
            config_file = argv[arg_index];
            log_info("指定配置文件: %s", config_file);
            arg_index++;
        }
    }
    
    // === 验证配置文件是否存在 ===
    FILE* test_file = fopen(config_file, "r");
    if (!test_file) {
        log_warn("警告: 配置文件 %s 不存在或无法访问", config_file);
        log_info("将尝试继续运行，但本地域名解析功能可能受限");
    } else {
        fclose(test_file);
        log_info("配置文件验证成功: %s", config_file);
    }
    
    // === 初始化日志系统 ===
    init_log_file();
    set_log_level(debug_level);
    
    log_info("=== DNS中继服务器启动 ===");
    log_info("程序版本: 多线程高性能版本");
    log_info("命令行参数解析完成:");
    log_info("  - 调试级别: %s", (debug_level == LOG_LEVEL_DEBUG) ? "调试模式" : "普通模式");
    log_info("  - DNS服务器: %s", dns_server_ip);
    log_info("  - 配置文件: %s", config_file);
    
    log_info("本版本特性：");
    log_info("  - 多线程并行处理");
    log_info("  - I/O与计算分离");  
    log_info("  - 线程安全的ID映射");
    log_info("  - 高并发性能");
    log_info("  - LRU缓存机制");
    log_info("  - 不良网站拦截");

    // === 初始化平台资源 ===
    platform_init();
    
    // === 初始化DNS中继服务 ===
    if (dns_relay_init(config_file) != MYSUCCESS) {
        log_error("DNS中继服务初始化失败");
        platform_cleanup();
        cleanup_log_file();
        return 1;
    }

    // === 设置上游DNS服务器 ===
    if (use_custom_dns) {
        // 如果用户指定了DNS服务器，使用指定的服务器
        if (set_default_dns_server(dns_server_ip) != MYSUCCESS) {
            log_error("设置指定的DNS服务器失败");
            platform_cleanup();
            cleanup_log_file();
            return 1;
        }
    } else {
        // 尝试从配置文件加载，失败则使用默认DNS服务器
        if (upstream_pool_load_from_file(&g_upstream_pool, "upstream_dns.conf") != MYSUCCESS) {
            log_info("从配置文件加载上游DNS失败，使用默认DNS服务器: %s", dns_server_ip);
            if (set_default_dns_server(dns_server_ip) != MYSUCCESS) {
                log_error("设置默认DNS服务器失败");
                platform_cleanup();
                cleanup_log_file();
                return 1;
            }
        }
    }

    // === 启动多线程DNS服务器 ===
    if (start_dns_proxy_server_threaded() != MYSUCCESS) {
        log_error("多线程DNS代理服务器启动失败");
        platform_cleanup();
        cleanup_log_file();
        return 1;
    }
    
    // === 清理资源 ===
    platform_cleanup();
    cleanup_log_file();
    
    log_info("多线程DNS代理服务器已安全退出");
    return 0;
}
