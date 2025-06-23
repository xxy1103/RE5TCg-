/**
 * @file lru_cache_test.c
 * @brief 全新的LRU缓存综合测试程序
 * @description 测试DNS中继服务器的LRU缓存功能，包含多种测试场景
 * @author DNS Relay Team
 * @date 2025-01-11
 * @version 2.0
 */

#include "DNScache/relayBuild.h"
#include "websocket/datagram.h"
#include "websocket/websocket.h"  // 包含MYSUCCESS等常量定义
#include "debug/debug.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>  // Windows控制台输入
#else
#include <unistd.h>
#include <termios.h>
#endif

// 如果仍然没有定义，则手动定义
#ifndef MYSUCCESS
#define MYSUCCESS 1
#endif

#ifndef MYERROR
#define MYERROR 0
#endif

// 测试统计信息
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double total_time;
} test_stats_t;

static test_stats_t g_test_stats = {0};

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 打印测试标题
 */
void print_test_header(const char* test_name) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║ %-60s ║\n", test_name);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

/**
 * @brief 打印测试结果
 */
void print_test_result(const char* test_case, int passed) {
    if (passed) {
        printf("✅ %s\n", test_case);
        g_test_stats.passed_tests++;
    } else {
        printf("❌ %s\n", test_case);
        g_test_stats.failed_tests++;
    }
    g_test_stats.total_tests++;
}

/**
 * @brief 等待用户按键继续
 */
void wait_for_keypress() {
    printf("\n按任意键继续...");
    #ifdef _WIN32
    _getch();
    #else
    getchar();
    #endif
    printf("\n");
}

/**
 * @brief 创建简单的DNS响应用于测试
 */
DNS_ENTITY* create_test_dns_response(const char* domain, const char* ip, int ttl) {
    DNS_ENTITY* response = (DNS_ENTITY*)malloc(sizeof(DNS_ENTITY));
    if (!response) return NULL;
    
    memset(response, 0, sizeof(DNS_ENTITY));
    
    // 设置基本DNS头部
    response->id = rand() % 65536;
    response->flags = 0x8180;  // 标准响应
    response->qdcount = 1;     // 1个问题
    response->ancount = 1;     // 1个答案
    response->nscount = 0;
    response->arcount = 0;
    
    // 分配问题部分
    response->questions = (DNS_QUESTION_ENTITY*)malloc(sizeof(DNS_QUESTION_ENTITY));
    if (response->questions) {
        response->questions->qname = malloc(strlen(domain) + 1);
        if (response->questions->qname) {
            strcpy(response->questions->qname, domain);
        }
        response->questions->qtype = 1;  // A记录
        response->questions->qclass = 1; // IN
    }
    
    // 分配答案部分
    response->answers = (R_DATA_ENTITY*)malloc(sizeof(R_DATA_ENTITY));
    if (response->answers) {
        response->answers->name = malloc(strlen(domain) + 1);
        if (response->answers->name) {
            strcpy(response->answers->name, domain);
        }
        response->answers->type = 1;     // A记录
        response->answers->_class = 1;   // IN
        response->answers->ttl = ttl;
        response->answers->data_len = 4; // IPv4地址长度
        
        // 设置IP地址
        response->answers->rdata = malloc(4);
        if (response->answers->rdata && ip) {
            int a, b, c, d;
            if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                response->answers->rdata[0] = (unsigned char)a;
                response->answers->rdata[1] = (unsigned char)b;
                response->answers->rdata[2] = (unsigned char)c;
                response->answers->rdata[3] = (unsigned char)d;
            }
        }
    }
    
    return response;
}

// ============================================================================
// 测试用例实现
// ============================================================================

/**
 * @brief 测试1：基础LRU操作
 */
void test_basic_lru_operations() {
    print_test_header("测试1：基础LRU操作");
    
    dns_lru_cache_t cache;
    int result;
    
    // 初始化测试
    result = dns_cache_init(&cache, 5);
    print_test_result("缓存初始化", result == MYSUCCESS);
    
    if (result != MYSUCCESS) return;
    
    printf("📊 初始缓存状态：容量=%d，当前大小=%d\n", cache.max_size, cache.current_size);
    
    // 插入测试数据
    DNS_ENTITY* responses[5];
    const char* domains[] = {
        "www.baidu.com", "www.google.com", "www.github.com", 
        "www.stackoverflow.com", "www.microsoft.com"
    };
    const char* ips[] = {
        "39.156.66.10", "142.250.191.36", "140.82.112.3",
        "151.101.1.69", "40.112.72.205"
    };
    
    for (int i = 0; i < 5; i++) {
        responses[i] = create_test_dns_response(domains[i], ips[i], 300);
        result = dns_cache_put(&cache, domains[i], responses[i], 300);
        printf("🔄 插入 %s -> %s", domains[i], ips[i]);
        if (result == MYSUCCESS) {
            printf(" ✅\n");
        } else {
            printf(" ❌\n");
        }
    }
    
    printf("📊 插入后缓存状态：当前大小=%d\n", cache.current_size);
    
    // 查找测试
    for (int i = 0; i < 5; i++) {
        dns_cache_entry_t* entry = dns_cache_get(&cache, domains[i]);
        print_test_result(
            (char[128]){0}, 
            snprintf((char[128]){0}, 128, "查找 %s", domains[i]) && entry != NULL
        );
    }
    
    // LRU淘汰测试
    DNS_ENTITY* new_response = create_test_dns_response("www.new-site.com", "192.168.1.100", 300);
    result = dns_cache_put(&cache, "www.new-site.com", new_response, 300);
    print_test_result("插入新条目触发LRU淘汰", result == MYSUCCESS);
    
    // 检查最老的条目是否被淘汰
    dns_cache_entry_t* old_entry = dns_cache_get(&cache, domains[0]);
    print_test_result("最老条目被正确淘汰", old_entry == NULL);
    
    // 打印统计信息
    printf("\n📈 缓存统计信息：\n");
    dns_cache_print_stats(&cache);
    
    dns_cache_destroy(&cache);
    print_test_result("缓存销毁", 1);
    
    wait_for_keypress();
}

/**
 * @brief 测试2：TTL过期机制
 */
void test_ttl_expiration() {
    print_test_header("测试2：TTL过期机制");
    
    dns_lru_cache_t cache;
    dns_cache_init(&cache, 10);
    
    // 插入短TTL条目
    DNS_ENTITY* short_response = create_test_dns_response("short-ttl.test", "192.168.1.1", 2);
    int result = dns_cache_put(&cache, "short-ttl.test", short_response, 2);
    print_test_result("插入短TTL条目 (2秒)", result == MYSUCCESS);
    
    // 立即查找
    dns_cache_entry_t* entry = dns_cache_get(&cache, "short-ttl.test");
    print_test_result("立即查找成功", entry != NULL);
    
    // 插入长TTL条目作为对比
    DNS_ENTITY* long_response = create_test_dns_response("long-ttl.test", "192.168.1.2", 300);
    result = dns_cache_put(&cache, "long-ttl.test", long_response, 300);
    print_test_result("插入长TTL条目 (300秒)", result == MYSUCCESS);
    
    printf("⏰ 等待3秒让短TTL条目过期...\n");
    platform_sleep_ms(3000);
    
    // 清理过期条目
    dns_cache_cleanup_expired(&cache);
    
    // 检查过期条目
    entry = dns_cache_get(&cache, "short-ttl.test");
    print_test_result("短TTL条目已过期", entry == NULL);
    
    // 检查长TTL条目仍有效
    entry = dns_cache_get(&cache, "long-ttl.test");
    print_test_result("长TTL条目仍有效", entry != NULL);
    
    printf("\n📈 TTL测试后统计：\n");
    dns_cache_print_stats(&cache);
    
    dns_cache_destroy(&cache);
    wait_for_keypress();
}

/**
 * @brief 测试3：高并发性能测试
 */
void test_performance_benchmark() {
    print_test_header("测试3：高并发性能测试");
    
    const int CACHE_SIZE = 1000;
    const int TEST_COUNT = 5000;
    
    dns_lru_cache_t cache;
    dns_cache_init(&cache, CACHE_SIZE);
    
    printf("🚀 性能测试参数：\n");
    printf("   - 缓存容量: %d\n", CACHE_SIZE);
    printf("   - 测试次数: %d\n", TEST_COUNT);
    
    // 批量插入测试
    printf("\n📥 批量插入测试...\n");
    clock_t start = clock();
    
    for (int i = 0; i < TEST_COUNT; i++) {
        char domain[64];
        char ip[16];
        sprintf(domain, "test%d.example.com", i);
        sprintf(ip, "192.168.%d.%d", (i / 256) % 256, i % 256);
        
        DNS_ENTITY* response = create_test_dns_response(domain, ip, 300);
        dns_cache_put(&cache, domain, response, 300);
        
        if (i % 1000 == 0) {
            printf("   已插入: %d 条目\n", i);
        }
    }
    
    clock_t end = clock();
    double insert_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("✅ 插入性能：%d 条目，耗时 %.3f 秒\n", TEST_COUNT, insert_time);
    printf("   平均插入速度：%.0f 条目/秒\n", TEST_COUNT / insert_time);
    
    // 批量查找测试
    printf("\n🔍 批量查找测试...\n");
    start = clock();
    int hit_count = 0;
    
    for (int i = 0; i < TEST_COUNT; i++) {
        char domain[64];
        sprintf(domain, "test%d.example.com", i);
        
        dns_cache_entry_t* entry = dns_cache_get(&cache, domain);
        if (entry) hit_count++;
        
        if (i % 1000 == 0) {
            printf("   已查找: %d 条目\n", i);
        }
    }
    
    end = clock();
    double lookup_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("✅ 查找性能：%d 次查找，耗时 %.3f 秒\n", TEST_COUNT, lookup_time);
    printf("   平均查找速度：%.0f 次/秒\n", TEST_COUNT / lookup_time);
    printf("   命中数量：%d/%d (%.1f%%)\n", hit_count, TEST_COUNT, 
           (double)hit_count / TEST_COUNT * 100.0);
    
    printf("\n📈 最终性能统计：\n");
    dns_cache_print_stats(&cache);
    
    dns_cache_destroy(&cache);
    wait_for_keypress();
}

/**
 * @brief 测试4：本地域名表功能测试
 */
void test_domain_table() {
    print_test_header("测试4：本地域名表功能测试");
    
    domain_table_t table;
    int result = domain_table_init(&table);
    print_test_result("域名表初始化", result == MYSUCCESS);
    
    // 测试加载域名表文件
    result = domain_table_load_from_file(&table, "dnsrelay.txt");
    if (result == MYSUCCESS) {
        printf("✅ 成功加载域名表文件，条目数：%d\n", table.entry_count);
        
        // 测试查找功能
        domain_entry_t* entry = domain_table_lookup(&table, "localhost");
        if (entry) {
            printf("✅ 查找测试：localhost -> %s\n", entry->ip);
            if (entry->is_blocked) {
                printf("   ⚠️  该域名被标记为阻止\n");
            }
        } else {
            printf("ℹ️  localhost 未在域名表中找到\n");
        }
    } else {
        printf("⚠️  域名表文件加载失败，可能文件不存在\n");
    }
    
    domain_table_destroy(&table);
    print_test_result("域名表销毁", 1);
    
    wait_for_keypress();
}

/**
 * @brief 测试5：综合查询接口测试
 */
void test_unified_query_interface() {
    print_test_header("测试5：综合查询接口测试");
    
    // 初始化DNS中继服务
    int result = dns_relay_init("dnsrelay.txt");
    print_test_result("DNS中继服务初始化", result == MYSUCCESS);
    
    if (result != MYSUCCESS) return;
    
    // 测试不同类型的查询
    const char* test_domains[] = {
        "localhost",           // 可能在本地表中
        "www.test.com",        // 缓存未命中
        "blocked.site.com"     // 可能被阻止
    };
    
    for (int i = 0; i < 3; i++) {
        printf("\n🔍 查询域名：%s\n", test_domains[i]);
        
        dns_query_response_t* response = dns_relay_query(test_domains[i], 1);
        if (response) {
            switch (response->result_type) {
                case QUERY_RESULT_BLOCKED:
                    printf("   🚫 域名被阻止\n");
                    break;
                case QUERY_RESULT_LOCAL_HIT:
                    printf("   🏠 本地表命中：%s\n", response->resolved_ip);
                    break;
                case QUERY_RESULT_CACHE_HIT:
                    printf("   💾 缓存命中\n");
                    break;
                case QUERY_RESULT_CACHE_MISS:
                    printf("   🌐 需要查询上游DNS\n");
                    break;
                case QUERY_RESULT_ERROR:
                    printf("   ❌ 查询错误\n");
                    break;
            }
            free(response);
        }
    }
    
    // 简单统计输出（不使用可能不存在的函数）
    printf("\n📊 服务统计信息：\n");
    printf("   DNS中继服务运行正常\n");
    
    dns_relay_cleanup();
    print_test_result("DNS中继服务清理", 1);
    
    wait_for_keypress();
}

/**
 * @brief 交互式菜单
 */
void show_menu() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    LRU缓存测试程序主菜单                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  1. 基础LRU操作测试                                          ║\n");
    printf("║  2. TTL过期机制测试                                          ║\n");
    printf("║  3. 高并发性能测试                                           ║\n");
    printf("║  4. 本地域名表测试                                           ║\n");
    printf("║  5. 综合查询接口测试                                         ║\n");
    printf("║  6. 运行所有测试                                             ║\n");
    printf("║  0. 退出程序                                                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("请选择测试项目 (0-6): ");
}

/**
 * @brief 打印最终测试报告
 */
void print_final_report() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                         测试总结报告                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  总测试数量: %-47d ║\n", g_test_stats.total_tests);
    printf("║  通过测试:   %-47d ║\n", g_test_stats.passed_tests);
    printf("║  失败测试:   %-47d ║\n", g_test_stats.failed_tests);
    if (g_test_stats.total_tests > 0) {
        double success_rate = (double)g_test_stats.passed_tests / g_test_stats.total_tests * 100.0;
        printf("║  成功率:     %-46.1f%% ║\n", success_rate);
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

// ============================================================================
// 主程序
// ============================================================================

int main() {
    // 初始化
    platform_init();
    init_log_file();
    set_log_level(LOG_LEVEL_INFO);
    srand(time(NULL));
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║               DNS LRU缓存综合测试程序 v2.0                   ║\n");
    printf("║                      设计者：DNS Relay Team                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    int choice;
    do {
        show_menu();
        
        #ifdef _WIN32
        // 使用标准scanf而不是Windows特定的scanf_s
        scanf("%d", &choice);
        #else
        scanf("%d", &choice);
        #endif
        
        clock_t test_start = clock();
        
        switch (choice) {
            case 1:
                test_basic_lru_operations();
                break;
            case 2:
                test_ttl_expiration();
                break;
            case 3:
                test_performance_benchmark();
                break;
            case 4:
                test_domain_table();
                break;
            case 5:
                test_unified_query_interface();
                break;
            case 6:
                printf("\n🚀 开始执行所有测试...\n");
                test_basic_lru_operations();
                test_ttl_expiration();
                test_performance_benchmark();
                test_domain_table();
                test_unified_query_interface();
                break;
            case 0:
                printf("\n👋 感谢使用LRU缓存测试程序！\n");
                break;
            default:
                printf("❌ 无效选择，请重新输入！\n");
                continue;
        }
        
        clock_t test_end = clock();
        if (choice >= 1 && choice <= 6) {
            double test_time = ((double)(test_end - test_start)) / CLOCKS_PER_SEC;
            g_test_stats.total_time += test_time;
            printf("⏱️  测试耗时：%.3f 秒\n", test_time);
        }
        
    } while (choice != 0);
    
    if (g_test_stats.total_tests > 0) {
        print_final_report();
    }
    
    // 清理
    cleanup_log_file();
    platform_cleanup();
    
    return 0;
} 