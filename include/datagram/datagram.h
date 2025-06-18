#ifndef DATAGRAM_H
#define DATAGRAM_H

#include <winsock2.h> // 包含 Winsock2 头文件，用于 Windows Sockets API
#include <ws2tcpip.h> // 包含 TCP/IP 协议相关的头文件
#include "debug/debug.h"   // 包含调试相关的头文件



#define A 1
#define AAAA 28
#define CNAME 5
#define MX 15

// 定义 DNS 查询实体和资源记录实体

typedef struct DNS_QUESTION_ENTITY {
    char* qname;            // 查询名：域名字符串，格式化为 DNS 查询格式
    unsigned short qtype;   // 查询类型：指定查询的资源记录类型 (例如，1 表示 A 记录)
    unsigned short qclass;  // 查询类：通常为 1，表示 IN (Internet)
} DNS_QUESTION_ENTITY;

typedef struct R_DATA_ENTITY {
    char* name;            // 域名：资源记录的域名，可能是压缩格式
    unsigned short type;   // 类型：资源记录的类型 (例如，1 表示 A 记录)
    unsigned short _class; // 类：通常为 1，表示 IN (Internet)
    unsigned int ttl;      // 生存时间(Time to Live)：资源记录可以被缓存的秒数
    unsigned short data_len; // 数据长度：RDATA 字段的长度（以字节为单位）
    char* rdata;           // RDATA 字段：包含实际的数据，如 IP 地址等
} R_DATA_ENTITY;


typedef struct DNS_ENTITY{
    unsigned short id;       // 事务ID：一个16位的标识符，用于匹配请求和响应
    unsigned short flags;    // 标志：一个16位的字段，包含查询/响应、操作码、标志位等信息
    unsigned short qdcount;  // 问题数：查询区域中的问题数量
    unsigned short ancount;  // 回答数：回答区域中的资源记录数量
    unsigned short nscount;  // 授权回答数：授权区域中的名称服务器资源记录数量
    unsigned short arcount;  // 附加回答数：附加区域中的资源记录数量
    DNS_QUESTION_ENTITY* questions; // 问题部分的数组
    R_DATA_ENTITY* answers; // 回答部分的数组
} DNS_ENTITY;


// 函数：将域名格式化为DNS查询格式 (例如 "www.baidu.com" -> "3www5baidu3com0")
// DNS协议要求域名以一种特殊的格式表示，每个标签前有一个字节表示该标签的长度。
void format_domain_name(char* dns, const char* host);

// 函数：将DNS_ENTITY序列化为DNS协议格式的字节流
int serialize_dns_packet(char* buffer, const DNS_ENTITY* dns_entity);

// 函数：从DNS协议格式的字节流解析为DNS_ENTITY
DNS_ENTITY* parse_dns_packet(const char* buffer, int buffer_len);

// 函数：创建一个DNS查询实体
DNS_ENTITY* create_dns_query(const char* hostname, unsigned short qtype);

// 函数：释放DNS_ENTITY及其相关资源
void free_dns_entity(DNS_ENTITY* entity);

// 函数：从缓冲区中读取域名（处理DNS压缩）
int read_name_from_buffer(const char* buffer, int buffer_len, int* offset, char* name, int max_name_len);

#endif // DATAGRAM_H