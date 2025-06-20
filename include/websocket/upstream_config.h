#ifndef UPSTREAM_CONFIG_H
#define UPSTREAM_CONFIG_H

#include "websocket/websocket.h"

/**
 * @brief 从配置文件加载DNS服务器列表
 * @param pool DNS服务器池指针
 * @param config_file 配置文件路径
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_load_from_file(upstream_dns_pool_t* pool, const char* config_file);

/**
 * @brief 将DNS服务器列表保存到配置文件
 * @param pool DNS服务器池指针
 * @param config_file 配置文件路径
 * @return 成功返回MYSUCCESS，失败返回MYERROR
 */
int upstream_pool_save_to_file(upstream_dns_pool_t* pool, const char* config_file);

/**
 * @brief 打印当前DNS服务器池状态
 * @param pool DNS服务器池指针
 */
void upstream_pool_print_status(upstream_dns_pool_t* pool);

#endif // UPSTREAM_CONFIG_H
