#include "platform/platform.h"
#include "websocket/websocket.h"
#include "websocket/datagram.h"
#include "websocket/dnsServer.h"
#include "debug/debug.h"
#include "DNScache/relayBuild.h"

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>



/**
 * @brief 打印程序使用帮助信息
 */
void print_usage(const char* program_name) {
    printf("DNS中继服务器 - 计算机网络课程设计\n");
    printf("\n使用方法:\n");
    printf("  %s [选项]\n\n", program_name);
    printf("参数说明:\n");
    printf("  -h, --help      显示此帮助信息\n");
    printf("  -d <级别>       设置日志级别 (error/warn/info/debug，默认: info)\n");
    printf("  -dd             调试级别2 (等价于 -d debug)\n");
    printf("  -c <文件>       指定DNS服务器配置文件 (默认: upstream_dns.conf)\n");
    printf("  -r <文件>       指定域名配置文件路径 (默认: dnsrelay.txt)\n\n");
    printf("日志级别说明:\n");
    printf("  error           只输出错误信息\n");
    printf("  warn            输出警告和错误信息\n");
    printf("  info            输出基本信息、警告和错误信息\n");
    printf("  debug           输出所有调试信息\n\n");
    printf("示例:\n");
    printf("  %s                              # 使用默认配置\n", program_name);
    printf("  %s -h                           # 显示帮助信息\n", program_name);
    printf("  %s -d info                      # 设置日志级别为info\n", program_name);
    printf("  %s -d debug                     # 设置日志级别为debug\n", program_name);
    printf("  %s -dd                          # 调试级别2 (等价于debug)\n", program_name);
    printf("  %s -c dns.conf                  # 指定DNS服务器配置文件\n", program_name);
    printf("  %s -r my_dns.txt               # 指定域名配置文件\n", program_name);
    printf("  %s -d warn -c dns.conf -r my_dns.txt # 警告级别，指定配置文件\n", program_name);
    printf("\n");
}


/**
 * @brief 将字符串转换为日志级别枚举
 */
LogLevel string_to_log_level(const char* level_str) {
    if (!level_str) {
        return LOG_LEVEL_INFO;  // 默认级别
    }

    if (strcmp(level_str, "error") == 0) {
        return LOG_LEVEL_ERROR;
    } else if (strcmp(level_str, "warn") == 0) {
        return LOG_LEVEL_WARN;
    } else if (strcmp(level_str, "info") == 0) {
        return LOG_LEVEL_INFO;
    } else if (strcmp(level_str, "debug") == 0) {
        return LOG_LEVEL_DEBUG;
    } else {
        return LOG_LEVEL_INFO;  // 默认级别
    }
}

/**
 * @brief 程序主入口点
 * 
 * 支持命令行参数：
 * dnsrelay [-h | --help] [-d <level> | -dd] [-c config_file] [-r filename]
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 成功返回 0，失败返回 1
 */
int main(int argc, char* argv[]) {
    
    // === 参数解析变量 ===
    LogLevel debug_level = LOG_LEVEL_INFO;  // 默认日志级别为info
    const char* dns_server_ip_conf = "upstream_dns.conf";  // 默认DNS服务器配置文件
    const char* config_file = "dnsrelay.txt";    // 默认配置文件
    
    // === 解析命令行参数 ===
    int arg_index = 1;
    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-h") == 0 || strcmp(argv[arg_index], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[arg_index], "-d") == 0) {
            if (arg_index + 1 < argc) {
                debug_level = string_to_log_level(argv[arg_index + 1]);
                log_info("设置日志级别为: %s", log_level_to_string(debug_level));
                arg_index++;
            } else {
                
                debug_level = LOG_LEVEL_INFO;
                log_info("使用默认日志级别: %s", log_level_to_string(debug_level));
            }
        }
        else if (strcmp(argv[arg_index], "-dd") == 0) {
            debug_level = LOG_LEVEL_DEBUG;  
            log_info("启用调试级别2：输出详细调试信息");
        }
        else if (strcmp(argv[arg_index], "-c") == 0) {
            if (arg_index + 1 < argc) {
                dns_server_ip_conf = argv[arg_index + 1];
                log_info("指定DNS服务器配置文件: %s", dns_server_ip_conf);
                arg_index++;
            }
        }
        else if (strcmp(argv[arg_index], "-r") == 0) {
            
            config_file = argv[arg_index + 1];
            log_info("指定配置文件: %s", config_file);
        }
        arg_index++;
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
    set_log_level(debug_level); //设置日志等级
    
    log_info("=== DNS中继服务器启动 ===");
    log_info("程序版本: 多线程高性能版本");
    log_info("命令行参数解析完成:");
    log_info("  - 调试级别: %s", log_level_to_string(debug_level));
    log_info("  - DNS服务器: %s", dns_server_ip_conf);
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
    
    // === 初始化本地域名表 ===
    if (dns_relay_init(config_file) != MYSUCCESS) {
        log_error("本地域名表初始化失败");
        platform_cleanup();
        cleanup_log_file();
        return 1;
    }

    // === 初始化上游DNS服务器池 ===
    if (upstream_pool_init(&g_upstream_pool, dns_server_ip_conf) != MYSUCCESS) {
        log_error("上游DNS服务器池初始化失败");
        platform_cleanup();
        cleanup_log_file();
        return 1;
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
