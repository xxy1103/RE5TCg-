# include "idmapping/idmapping.h"

dns_mapping_table_t g_mapping_table;

/**
 * @brief 初始化映射表
 * 
 * 将映射表的所有条目清零，并设置初始状态。
 * 这个函数应该在服务器启动时调用一次。
 * 
 * @param table 指向要初始化的映射表的指针
 */
void init_mapping_table(dns_mapping_table_t* table) {
    memset(table, 0, sizeof(dns_mapping_table_t));
    table->next_id = 1;  // 从1开始分配ID，避免使用0（通常表示无效值）
    table->count = 0;    // 当前活跃映射数量为0
}

/**
 * @brief 添加新的映射关系
 * 
 * 为新的DNS请求创建ID映射关系。这个函数会：
 * 1. 先清理过期的映射条目以释放空间
 * 2. 查找空闲的映射表槽位
 * 3. 分配新的DNS Transaction ID
 * 4. 保存客户端信息和映射关系
 * 
 * @param table 映射表指针
 * @param original_id 客户端原始请求ID
 * @param client_addr 客户端地址信息
 * @param client_addr_len 客户端地址长度
 * @param new_id 输出参数，返回分配的新ID
 * @return int 成功返回MYSUCCESS，失败返回MYERROR
 */
int add_mapping(dns_mapping_table_t* table, unsigned short original_id, 
                struct sockaddr_in* client_addr, int client_addr_len, unsigned short* new_id) {
    // 清理过期映射，为新映射腾出空间
    cleanup_expired_mappings(table);
    
    // 遍历映射表，查找空闲槽位
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (!table->entries[i].is_active) {
            // 分配新ID，避免0（通常作为无效值）
            *new_id = table->next_id;
            if (table->next_id == 0) table->next_id = 1;  // 如果溢出到0，跳过并从1开始
            table->next_id++;
            
            // 填充映射信息 - 保存完整的客户端状态
            table->entries[i].original_id = original_id;        // 客户端原始ID
            table->entries[i].new_id = *new_id;                 // 服务器分配的新ID
            table->entries[i].client_addr = *client_addr;       // 客户端网络地址
            table->entries[i].client_addr_len = client_addr_len; // 地址结构长度
            table->entries[i].timestamp = time(NULL);           // 请求创建时间戳
            table->entries[i].is_active = 1;                    // 标记为活跃状态
              table->count++;  // 增加活跃映射计数
            log_debug("添加映射: 原始ID=%d -> 新ID=%d (槽位 %d, 总数: %d)", 
                     original_id, *new_id, i, table->count);
            return MYSUCCESS;
        }
    }
    
    // 映射表已满，无法添加新映射
    log_error("映射表已满 (最大: %d)，无法添加新映射", MAX_CONCURRENT_REQUESTS);
    return MYERROR;
}



/**
 * @brief 根据新ID查找映射
 * 
 * 当收到上游DNS服务器的响应时，使用响应中的Transaction ID
 * 来查找对应的映射条目，以确定原始客户端信息。
 * 
 * @param table 映射表指针
 * @param new_id 要查找的新ID（来自上游响应）
 * @return dns_mapping_entry_t* 找到的映射条目指针，未找到返回NULL
 */
dns_mapping_entry_t* find_mapping_by_new_id(dns_mapping_table_t* table, unsigned short new_id) {    // 线性搜索映射表
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && table->entries[i].new_id == new_id) {
            log_debug("找到新ID=%d 的映射，位于槽位 %d", new_id, i);
            return &table->entries[i];
        }
    }
    
    // 未找到对应的映射，可能是：
    // 1. 响应ID错误
    // 2. 映射已过期被清理
    // 3. 收到了重复响应
    log_debug("未找到新ID=%d 对应的映射", new_id);
    return NULL;
}



/**
 * @brief 移除映射
 * 
 * 当DNS请求完成（收到响应并转发给客户端）后，
 * 移除对应的映射条目以释放资源。
 * 
 * @param table 映射表指针
 * @param new_id 要移除的映射的新ID
 */
void remove_mapping(dns_mapping_table_t* table, unsigned short new_id) {
    // 查找并移除指定ID的映射
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && table->entries[i].new_id == new_id) {            // 标记为非活跃状态（实际上是删除）
            table->entries[i].is_active = 0;
            table->count--;  // 减少活跃映射计数
            log_debug("移除映射: 新ID=%d，槽位 %d (剩余: %d)", 
                     new_id, i, table->count);
            return;
        }
    }
    
    // 如果执行到这里，说明没有找到要删除的映射
    log_warn("尝试移除不存在的映射: 新ID=%d", new_id);
}



/**
 * @brief 清理过期映射
 * 
 * 定期清理超时的DNS请求映射，防止：
 * 1. 内存泄漏（映射表条目一直被占用）
 * 2. 映射表满载（影响新请求处理）
 * 3. 无效映射积累（降低查找效率）
 * 
 * 判断过期的标准：当前时间 - 请求时间戳 > REQUEST_TIMEOUT
 * 
 * @param table 映射表指针
 */
void cleanup_expired_mappings(dns_mapping_table_t* table) {
    time_t current_time = time(NULL);  // 获取当前时间戳
    int cleaned = 0;  // 清理计数器
    
    // 遍历所有映射条目
    for (int i = 0; i < MAX_CONCURRENT_REQUESTS; i++) {
        if (table->entries[i].is_active && 
            (current_time - table->entries[i].timestamp) > REQUEST_TIMEOUT) {
              // 记录被清理的映射信息（用于调试）
            log_debug("清理过期映射: 槽位=%d, 新ID=%d, 存活时间=%ld 秒", 
                     i, table->entries[i].new_id, 
                     (long)(current_time - table->entries[i].timestamp));
            
            // 标记为非活跃（删除）
            table->entries[i].is_active = 0;
            table->count--;  // 减少活跃映射计数
            cleaned++;       // 增加清理计数
        }
    }
    
    // 如果清理了映射，记录清理统计信息
    if (cleaned > 0) {
        log_info("清理了 %d 个过期映射 (超时: %d 秒, 剩余: %d)", 
                cleaned, REQUEST_TIMEOUT, table->count);
    }
}