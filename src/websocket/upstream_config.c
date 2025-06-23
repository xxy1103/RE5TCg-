#include "websocket/upstream_config.h"
#include "debug/debug.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 最大IP地址长度（IPv6最大长度为45字符，加1为46）
#define MAX_IP_LENGTH 46

/**
 * @brief 从配置文件加载DNS服务器列表
 * @param pool DNS服务器池指针
 * @param config_file 配置文件路径
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_load_from_file(upstream_dns_pool_t* pool, const char* config_file) {
    if (!pool || !config_file) {
        return MYERROR;
    }
    
    FILE* file = fopen(config_file, "r");
    if (!file) {
        log_warn("无法打开配置文件 %s，将使用默认DNS服务器", config_file);
        return MYERROR;
    }
    
    char line[256];
    int loaded_count = 0;
    
    while (fgets(line, sizeof(line), file) && loaded_count < MAX_UPSTREAM_SERVERS) {
        // 移除行尾的换行符
        char* newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';
        }
        
        // 跳过空行和注释行
        if (strlen(line) == 0 || line[0] == '#') {
            continue;
        }
        
        // 简单的IP地址格式验证
        if (strlen(line) >= 7 && strlen(line) < MAX_IP_LENGTH) {
            if (upstream_pool_add_server(pool, line) == MYSUCCESS) {
                loaded_count++;
                log_debug("从配置文件加载DNS服务器: %s", line);
            }
        }
    }
    
    fclose(file);
    
    if (loaded_count > 0) {
        log_info("从配置文件成功加载 %d 个DNS服务器", loaded_count);
        return MYSUCCESS;
    } else {
        log_warn("配置文件中未找到有效的DNS服务器");
        return MYERROR;
    }
}


/**
 * @brief 打印当前DNS服务器池状态
 * @param pool DNS服务器池指针
 */
void upstream_pool_print_status(upstream_dns_pool_t* pool) {
    if (!pool) {
        printf("DNS Server Pool: Not initialized\n");
        return;
    }
    
    printf("=== DNS Upstream Server Pool Status ===\n");
    printf("Server Count: %d/%d\n", pool->server_count, MAX_UPSTREAM_SERVERS);
    printf("Current Index: %d\n", pool->current_index);
    printf("Server List:\n");
    
    for (int i = 0; i < pool->server_count; i++) {
        printf("  [%d] %s\n", i, pool->servers[i]);
    }
    
    printf("=========================\n");
}
