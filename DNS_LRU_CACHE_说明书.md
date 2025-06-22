# DNS中继服务器LRU缓存功能说明书

## 📋 目录
1. [功能概述](#功能概述)
2. [架构设计](#架构设计)
3. [数据结构详解](#数据结构详解)
4. [函数接口详解](#函数接口详解)
5. [集成指南](#集成指南)
6. [性能特性](#性能特性)
7. [使用示例](#使用示例)
8. [配置说明](#配置说明)

---

## 功能概述

本LRU缓存模块为DNS中继服务器提供了高性能的域名解析缓存功能，以实现《计算机网络课程设计》任务书中关于"实现LRU机制的Cache缓存"的要求。

### 🎯 核心功能
- **三级查询架构**: 本地域名表 → LRU缓存 → 上游DNS服务器
- **LRU淘汰策略**: 最近最少使用算法，自动管理缓存容量
- **TTL支持**: 基于DNS记录TTL的自动过期机制
- **恶意网站拦截**: 支持0.0.0.0标记的域名阻止功能
- **高性能设计**: O(1)查找和更新复杂度
- **统计监控**: 完整的缓存命中率和性能统计

### 📂 文件结构
```
include/DNScache/relayBuild.h    # 头文件，数据结构和函数声明
src/DNScache/relayBuild.c        # 实现文件，核心算法逻辑
dnsrelay.txt                     # 本地域名表配置文件
```

---

## 架构设计

### 🏗️ 整体架构图
```
客户端DNS查询
       ↓
┌─────────────────┐
│   统一查询接口   │ ← dns_relay_query()
└─────────────────┘
       ↓
┌─────────────────┐    命中 → 返回结果
│  本地域名表查询  │ ← domain_table_lookup()
└─────────────────┘
       ↓ 未命中
┌─────────────────┐    命中 → 返回缓存
│   LRU缓存查询   │ ← dns_cache_get()
└─────────────────┘
       ↓ 未命中
┌─────────────────┐
│  上游DNS查询    │ → 查询结果缓存 ← dns_cache_put()
└─────────────────┘
```

### 🔄 LRU算法原理
```
哈希表索引: [0] [1] [2] ... [2047]
              ↓   ↓   ↓
           Entry Entry Entry
              ↓
LRU双向链表: HEAD ←→ Entry1 ←→ Entry2 ←→ ... ←→ TAIL
            (最新)                              (最旧)

访问操作: 移动到HEAD
淘汰操作: 移除TAIL
```

---

## 数据结构详解

### 1. 本地域名表结构

#### `domain_entry_t` - 域名表条目
```c
typedef struct domain_entry {
    char domain[MAX_DOMAIN_LENGTH];     // 域名 (最大256字符)
    char ip[MAX_IP_LENGTH];             // IP地址 (最大16字符)
    int is_blocked;                     // 阻止标记 (0.0.0.0为true)
    struct domain_entry* next;          // 哈希冲突链表指针
} domain_entry_t;
```

#### `domain_table_t` - 域名表管理器
```c
typedef struct {
    domain_entry_t* hash_table[DOMAIN_TABLE_HASH_SIZE];  // 4096个哈希槽
    int entry_count;                    // 当前条目数量
    time_t last_load_time;              // 最后加载时间
} domain_table_t;
```

### 2. LRU缓存结构

#### `dns_cache_entry_t` - 缓存条目
```c
typedef struct dns_cache_entry {
    char domain[MAX_DOMAIN_LENGTH];     // 域名
    DNS_ENTITY* dns_response;           // 完整DNS响应
    time_t expire_time;                 // 过期时间戳
    time_t access_time;                 // 最后访问时间
    
    // LRU双向链表指针
    struct dns_cache_entry* prev;       // 前驱节点
    struct dns_cache_entry* next;       // 后继节点
    
    // 哈希表链表指针
    struct dns_cache_entry* hash_next;  // 哈希冲突链表
} dns_cache_entry_t;
```

#### `dns_lru_cache_t` - LRU缓存管理器
```c
typedef struct {
    dns_cache_entry_t* hash_table[DNS_CACHE_HASH_SIZE];  // 2048个哈希槽
    dns_cache_entry_t* lru_head;        // LRU链表头(最新)
    dns_cache_entry_t* lru_tail;        // LRU链表尾(最旧)
    dns_cache_entry_t* entry_pool;      // 预分配条目池
    int current_size;                   // 当前缓存大小
    int max_size;                       // 最大缓存容量
    
    // 统计信息
    unsigned long cache_hits;           // 缓存命中次数
    unsigned long cache_misses;         // 缓存未命中次数
    unsigned long cache_evictions;      // 缓存淘汰次数
} dns_lru_cache_t;
```

### 3. 查询结果结构

#### `dns_query_result_t` - 查询结果类型
```c
typedef enum {
    QUERY_RESULT_BLOCKED,       // 域名被阻止 (0.0.0.0)
    QUERY_RESULT_LOCAL_HIT,     // 本地表命中
    QUERY_RESULT_CACHE_HIT,     // 缓存命中
    QUERY_RESULT_CACHE_MISS,    // 缓存未命中，需上游查询
    QUERY_RESULT_ERROR          // 查询错误
} dns_query_result_t;
```

#### `dns_query_response_t` - 查询响应结构
```c
typedef struct {
    dns_query_result_t result_type;     // 查询结果类型
    DNS_ENTITY* dns_response;           // DNS响应(如果有)
    char resolved_ip[MAX_IP_LENGTH];    // 解析的IP地址
} dns_query_response_t;
```

---

## 函数接口详解

### 🔧 本地域名表管理函数

#### `int domain_table_init(domain_table_t* table)`
**功能**: 初始化本地域名表
**参数**: 
- `table`: 域名表结构指针
**返回值**: 
- `MYSUCCESS`: 初始化成功
- `MYERROR`: 参数错误
**实现逻辑**:
1. 检查参数有效性
2. 清零所有哈希槽位
3. 初始化计数器和时间戳
4. 记录初始化日志

#### `int domain_table_load_from_file(domain_table_t* table, const char* filename)`
**功能**: 从文件加载域名-IP映射表
**参数**:
- `table`: 域名表结构指针
- `filename`: 配置文件路径(通常为"dnsrelay.txt")
**返回值**:
- `MYSUCCESS`: 加载成功
- `MYERROR`: 文件打开失败或参数错误
**实现逻辑**:
1. 打开配置文件
2. 逐行解析"IP 域名"格式
3. 跳过空行和注释行(#开头)
4. 为每个条目分配内存并填充数据
5. 检测0.0.0.0标记设置阻止标志
6. 使用哈希函数插入哈希表
7. 更新统计信息和加载时间

#### `domain_entry_t* domain_table_lookup(domain_table_t* table, const char* domain)`
**功能**: 查找域名表条目
**参数**:
- `table`: 域名表结构指针
- `domain`: 要查找的域名
**返回值**:
- `非NULL`: 找到的域名条目指针
- `NULL`: 未找到或参数错误
**实现逻辑**:
1. 计算域名哈希值
2. 定位到对应哈希槽
3. 遍历冲突链表
4. 使用不区分大小写比较(`strcasecmp`)
5. 返回匹配的条目

#### `void domain_table_destroy(domain_table_t* table)`
**功能**: 销毁域名表并释放所有内存
**参数**:
- `table`: 域名表结构指针
**实现逻辑**:
1. 遍历所有哈希槽
2. 释放每个冲突链表中的所有节点
3. 清零哈希表指针
4. 重置计数器

### 🚀 LRU缓存核心函数

#### `int dns_cache_init(dns_lru_cache_t* cache, int max_size)`
**功能**: 初始化LRU缓存
**参数**:
- `cache`: 缓存结构指针
- `max_size`: 最大缓存容量
**返回值**:
- `MYSUCCESS`: 初始化成功
- `MYERROR`: 内存分配失败或参数错误
**实现逻辑**:
1. 验证参数有效性
2. 清零哈希表
3. 预分配条目池内存(`calloc`)
4. 初始化LRU链表指针
5. 清零统计计数器
6. 记录初始化日志

#### `dns_cache_entry_t* dns_cache_get(dns_lru_cache_t* cache, const char* domain)`
**功能**: 从缓存获取DNS条目(核心查找函数)
**参数**:
- `cache`: 缓存结构指针
- `domain`: 要查找的域名
**返回值**:
- `非NULL`: 找到的缓存条目指针
- `NULL`: 未找到、已过期或参数错误
**实现逻辑**:
1. 计算域名哈希值定位槽位
2. 遍历哈希冲突链表
3. 不区分大小写匹配域名
4. 检查TTL是否过期
5. 如果有效，更新访问时间
6. 调用`lru_move_to_head()`移到链表头部
7. 增加命中计数器
8. 返回条目指针

#### `int dns_cache_put(dns_lru_cache_t* cache, const char* domain, DNS_ENTITY* response, int ttl)`
**功能**: 向缓存添加或更新DNS条目
**参数**:
- `cache`: 缓存结构指针
- `domain`: 域名
- `response`: DNS响应结构
- `ttl`: 生存时间(秒)
**返回值**:
- `MYSUCCESS`: 添加/更新成功
- `MYERROR`: 操作失败
**实现逻辑**:
1. 检查是否已存在(调用`dns_cache_get`)
2. 如果存在，更新现有条目的响应和过期时间
3. 如果不存在且缓存已满，调用`lru_remove_tail()`淘汰最旧条目
4. 从条目池中找到空闲条目
5. 填充条目数据(域名、响应、时间戳)
6. 插入哈希表
7. 调用`lru_move_to_head()`插入LRU链表头部
8. 增加当前大小计数

#### `void lru_move_to_head(dns_lru_cache_t* cache, dns_cache_entry_t* entry)`
**功能**: 将缓存条目移动到LRU链表头部(最近使用)
**参数**:
- `cache`: 缓存结构指针
- `entry`: 要移动的条目
**实现逻辑**:
1. 检查是否已经是头部节点
2. 从当前位置断开链接(更新前驱和后继的指针)
3. 如果是尾部节点，更新尾部指针
4. 插入到头部(更新头部指针和相关链接)
5. 如果链表为空，同时设置尾部指针

#### `void lru_remove_tail(dns_lru_cache_t* cache)`
**功能**: 移除LRU链表尾部条目(最久未使用)
**参数**:
- `cache`: 缓存结构指针
**实现逻辑**:
1. 获取尾部条目指针
2. 从哈希表中移除(遍历对应槽位的冲突链表)
3. 从LRU链表中断开(更新前驱节点指针)
4. 释放DNS响应内存(`free_dns_entity`)
5. 清零条目数据(`memset`)
6. 更新缓存大小和淘汰计数器

#### `void dns_cache_cleanup_expired(dns_lru_cache_t* cache)`
**功能**: 清理过期的缓存条目
**参数**:
- `cache`: 缓存结构指针
**实现逻辑**:
1. 获取当前时间戳
2. 从尾部开始遍历LRU链表
3. 检查每个条目的过期时间
4. 调用`lru_remove_tail()`移除过期条目
5. 统计清理数量并记录日志

#### `void dns_cache_print_stats(dns_lru_cache_t* cache)`
**功能**: 打印缓存统计信息
**参数**:
- `cache`: 缓存结构指针
**输出信息**:
- 当前大小和最大容量
- 缓存命中次数和未命中次数
- 缓存淘汰次数
- 命中率百分比

#### `void dns_cache_destroy(dns_lru_cache_t* cache)`
**功能**: 销毁缓存并释放所有资源
**参数**:
- `cache`: 缓存结构指针
**实现逻辑**:
1. 遍历条目池释放所有DNS响应
2. 释放条目池内存
3. 清零哈希表指针
4. 重置链表指针和计数器

### 🌐 统一查询接口函数

#### `dns_query_response_t* dns_relay_query(const char* domain, unsigned short qtype)`
**功能**: 统一DNS查询接口，实现三级查询架构
**参数**:
- `domain`: 要查询的域名
- `qtype`: DNS查询类型(A、AAAA等)
**返回值**:
- `非NULL`: 查询响应结构指针
- `NULL`: 内存分配失败
**实现逻辑**:
1. 分配响应结构内存
2. **第一级**: 调用`domain_table_lookup()`查询本地表
   - 如果找到且被阻止，返回`QUERY_RESULT_BLOCKED`
   - 如果找到且有效，返回`QUERY_RESULT_LOCAL_HIT`和IP地址
3. **第二级**: 调用`dns_cache_get()`查询缓存
   - 如果命中，返回`QUERY_RESULT_CACHE_HIT`和DNS响应
4. **第三级**: 返回`QUERY_RESULT_CACHE_MISS`，需要查询上游DNS

#### `int dns_relay_init(const char* domain_file)`
**功能**: 初始化整个DNS中继服务
**参数**:
- `domain_file`: 域名表文件路径
**返回值**:
- `MYSUCCESS`: 初始化成功
- `MYERROR`: 初始化失败
**实现逻辑**:
1. 调用`domain_table_init()`初始化域名表
2. 调用`domain_table_load_from_file()`加载配置文件
3. 调用`dns_cache_init()`初始化LRU缓存
4. 如果任何步骤失败，清理已分配资源

#### `void dns_relay_cleanup(void)`
**功能**: 清理DNS中继服务并打印统计信息
**实现逻辑**:
1. 调用`dns_cache_print_stats()`打印缓存统计
2. 调用`dns_cache_destroy()`销毁缓存
3. 调用`domain_table_destroy()`销毁域名表
4. 记录清理完成日志

### 🔗 集成辅助函数

#### `int dns_relay_cache_response(const char* domain, DNS_ENTITY* response)`
**功能**: 将上游DNS响应添加到缓存
**参数**:
- `domain`: 域名
- `response`: DNS响应结构
**返回值**:
- `MYSUCCESS`: 缓存成功
- `MYERROR`: 缓存失败
**使用场景**: 在收到上游DNS服务器响应后调用

#### `void dns_relay_get_stats(int* domain_count, int* cache_size, unsigned long* cache_hits, unsigned long* cache_misses)`
**功能**: 获取统计信息
**参数**: 各统计数据的输出指针
**用途**: 监控和调试

#### `unsigned int hash_domain(const char* domain)`
**功能**: 域名哈希函数
**算法**: djb2哈希算法
**特性**: 
- 自动转换为小写
- 良好的分布特性
- 快速计算

---

## 集成指南

### 1. 编译配置

在`CMakeLists.txt`中确保包含了缓存模块：
```cmake
# 已经包含在现有配置中
include_directories(include)
```

### 2. 头文件引入

在需要使用缓存功能的源文件中：
```c
#include "DNScache/relayBuild.h"
```

### 3. 主程序集成

#### 在`main()`函数中初始化：
```c
int main(int argc, char* argv[]) {
    // 解析命令行参数获取配置文件路径
    const char* config_file = "dnsrelay.txt";  // 默认配置文件
    
    // 初始化DNS中继服务
    if (dns_relay_init(config_file) != MYSUCCESS) {
        log_error("DNS中继服务初始化失败");
        return -1;
    }
    
    // 其他初始化代码...
    
    // 主服务循环
    while (running) {
        // 处理DNS请求
    }
    
    // 程序退出前清理
    dns_relay_cleanup();
    return 0;
}
```

### 4. DNS请求处理集成

#### 替换现有的域名查询逻辑：
```c
void handle_dns_query(const char* domain, unsigned short qtype, 
                     struct sockaddr_in* client_addr) {
    // 使用统一查询接口
    dns_query_response_t* result = dns_relay_query(domain, qtype);
    if (!result) {
        log_error("查询失败: %s", domain);
        return;
    }
    
    switch (result->result_type) {
        case QUERY_RESULT_BLOCKED:
            // 构建域名不存在的DNS响应
            send_nxdomain_response(client_addr, domain);
            log_info("阻止域名访问: %s", domain);
            break;
            
        case QUERY_RESULT_LOCAL_HIT:
            // 使用本地IP地址构建DNS响应
            send_dns_response(client_addr, domain, result->resolved_ip);
            log_info("本地表命中: %s -> %s", domain, result->resolved_ip);
            break;
            
        case QUERY_RESULT_CACHE_HIT:
            // 直接发送缓存的DNS响应
            send_cached_dns_response(client_addr, result->dns_response);
            log_info("缓存命中: %s", domain);
            break;
            
        case QUERY_RESULT_CACHE_MISS:
            // 查询上游DNS服务器
            query_upstream_dns(domain, qtype, client_addr);
            log_debug("缓存未命中，查询上游: %s", domain);
            break;
            
        case QUERY_RESULT_ERROR:
            log_error("查询错误: %s", domain);
            break;
    }
    
    // 释放查询结果
    free(result);
}
```

### 5. 上游DNS响应处理

#### 在收到上游DNS响应后缓存结果：
```c
void handle_upstream_response(const char* domain, DNS_ENTITY* response,
                            struct sockaddr_in* client_addr) {
    // 将响应添加到缓存
    if (dns_relay_cache_response(domain, response) == MYSUCCESS) {
        log_debug("成功缓存上游响应: %s", domain);
    }
    
    // 转发响应给客户端
    forward_dns_response(client_addr, response);
}
```

---

## 性能特性

### 🚀 时间复杂度
- **查找操作**: O(1) 平均情况，O(n) 最坏情况(哈希冲突)
- **插入操作**: O(1) 平均情况
- **删除操作**: O(1)
- **LRU更新**: O(1)

### 💾 空间复杂度
- **哈希表**: O(n) 其中n为缓存条目数
- **LRU链表**: O(n) 额外空间
- **总空间**: O(n) 线性空间复杂度

### 📊 性能参数
```c
#define DNS_CACHE_SIZE 1000             // 缓存容量: 1000条记录
#define DNS_CACHE_HASH_SIZE 2048        // 哈希表大小: 2048个槽位
#define DOMAIN_TABLE_HASH_SIZE 4096     // 域名表哈希大小: 4096个槽位
#define DEFAULT_TTL 300                 // 默认TTL: 5分钟
```

### 🎯 性能优化特性
1. **预分配内存池**: 避免频繁malloc/free
2. **双重哈希**: 域名表和缓存分别优化
3. **TTL自动清理**: 定期清理过期条目
4. **统计监控**: 实时性能指标

---

## 使用示例

### 1. 基本使用流程
```c
#include "DNScache/relayBuild.h"

int main() {
    // 1. 初始化
    if (dns_relay_init("dnsrelay.txt") != MYSUCCESS) {
        return -1;
    }
    
    // 2. 查询域名
    dns_query_response_t* result = dns_relay_query("www.baidu.com", A);
    if (result) {
        switch (result->result_type) {
            case QUERY_RESULT_LOCAL_HIT:
                printf("本地解析: %s\n", result->resolved_ip);
                break;
            case QUERY_RESULT_CACHE_HIT:
                printf("缓存命中\n");
                break;
            case QUERY_RESULT_CACHE_MISS:
                printf("需要查询上游DNS\n");
                break;
        }
        free(result);
    }
    
    // 3. 清理
    dns_relay_cleanup();
    return 0;
}
```

### 2. 统计信息查看
```c
void print_cache_statistics() {
    int domain_count, cache_size;
    unsigned long cache_hits, cache_misses;
    
    dns_relay_get_stats(&domain_count, &cache_size, &cache_hits, &cache_misses);
    
    printf("域名表条目: %d\n", domain_count);
    printf("缓存大小: %d\n", cache_size);
    printf("缓存命中: %lu\n", cache_hits);
    printf("缓存未命中: %lu\n", cache_misses);
    
    if (cache_hits + cache_misses > 0) {
        double hit_rate = (double)cache_hits / (cache_hits + cache_misses) * 100.0;
        printf("命中率: %.2f%%\n", hit_rate);
    }
}
```

---

## 配置说明

### 1. dnsrelay.txt格式
```
# 注释行以#开头
# 格式: IP地址 域名
# 0.0.0.0表示阻止该域名

# 阻止恶意网站
0.0.0.0 malware.example.com
0.0.0.0 phishing.example.com

# 本地解析
192.168.1.100 local.server.com
10.0.0.1 internal.company.com

# 公共域名映射
202.108.33.89 sina.com.cn
61.135.181.175 sohu.com
```

### 2. 编译时配置
```c
// 在relayBuild.h中可调整的参数
#define MAX_DOMAIN_LENGTH 256           // 最大域名长度
#define MAX_IP_LENGTH 16                // 最大IP长度
#define DNS_CACHE_SIZE 1000             // 缓存容量
#define DNS_CACHE_HASH_SIZE 2048        // 缓存哈希表大小
#define DOMAIN_TABLE_HASH_SIZE 4096     // 域名表哈希大小
#define DEFAULT_TTL 300                 // 默认TTL(秒)
```

### 3. 运行时配置
通过命令行参数指定配置文件：
```bash
./dnsrelay -d 8.8.8.8 custom_domains.txt
```

---

## 📈 性能测试建议

### 1. 基准测试
```c
void benchmark_cache_performance() {
    clock_t start, end;
    const int test_count = 10000;
    
    // 测试查找性能
    start = clock();
    for (int i = 0; i < test_count; i++) {
        dns_query_response_t* result = dns_relay_query("test.domain.com", A);
        if (result) free(result);
    }
    end = clock();
    
    double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("查找性能: %d次查询耗时%.2f秒\n", test_count, cpu_time);
    printf("平均每次查询: %.2f微秒\n", cpu_time * 1000000 / test_count);
}
```

### 2. 内存使用监控
```c
void monitor_memory_usage() {
    // 获取统计信息
    dns_cache_print_stats(&g_dns_cache);
    
    // 计算内存使用
    size_t cache_memory = sizeof(dns_lru_cache_t) + 
                         g_dns_cache.max_size * sizeof(dns_cache_entry_t);
    printf("缓存内存使用: %zu KB\n", cache_memory / 1024);
}
```

---

## 🔧 故障排除

### 常见问题及解决方案

1. **缓存命中率低**
   - 增加缓存容量 (`DNS_CACHE_SIZE`)
   - 检查TTL设置是否过短
   - 分析查询模式是否适合缓存

2. **内存使用过高**
   - 减少缓存容量
   - 启用定期过期清理
   - 检查DNS响应是否正确释放

3. **哈希冲突过多**
   - 增加哈希表大小
   - 检查域名分布是否均匀
   - 考虑使用更好的哈希函数

4. **配置文件加载失败**
   - 检查文件路径和权限
   - 验证文件格式是否正确
   - 查看错误日志获取详细信息

---

## 📝 总结

本LRU缓存模块完全满足课程设计要求，提供了：

✅ **完整的LRU机制**: 基于访问时间的自动淘汰策略  
✅ **高性能实现**: O(1)查找和更新复杂度  
✅ **三级查询架构**: 本地表→缓存→上游DNS的完整解决方案  
✅ **TTL支持**: 基于DNS协议的自动过期机制  
✅ **统计监控**: 完整的性能指标和调试信息  
✅ **易于集成**: 清晰的API接口和详细的集成指南  

该实现不仅满足了基本的缓存需求，还提供了生产级别的性能和可靠性，为DNS中继服务器提供了强大的缓存支持。 