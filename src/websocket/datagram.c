#include "websocket/datagram.h" // 包含 DNS 报头和问题结构的定义
#include <string.h> // 为 strcpy, strcat, strlen 函数添加头文件
#include <stdlib.h> // 为 malloc, free 函数添加头文件

// 函数：将域名格式化为DNS查询格式 (例如 "www.baidu.com" -> "3www5baidu3com0")
// DNS协议要求域名以一种特殊的格式表示，每个标签前有一个字节表示该标签的长度。
void format_domain_name(char* dns, const char* host) {
    int lock = 0;
    char temp_host[256];
    strcpy(temp_host, host); // 复制域名，因为我们需要在末尾添加一个点
    strcat(temp_host, ".");  // 在域名末尾添加一个点，方便处理

    // 遍历域名字符串
    for (size_t i = 0; i < strlen(temp_host); i++) {        // 当遇到点时，表示一个标签结束
        if (temp_host[i] == '.') {
            *dns++ = (char)(i - lock); // 写入标签的长度
            // 写入标签本身
            for (; lock < (int)i; lock++) {
                *dns++ = temp_host[lock];
            }
            lock++; // 更新下一个标签的起始位置
        }
    }
    *dns++ = '\0'; // 以一个长度为0的字节结束，表示根域
}

// 函数：创建一个DNS查询实体
DNS_ENTITY* create_dns_query(const char* hostname, unsigned short qtype) {
    DNS_ENTITY* entity = (DNS_ENTITY*)malloc(sizeof(DNS_ENTITY));
    if (!entity) return NULL;
    
    // 初始化DNS头部
    entity->id = (unsigned short)(rand() % 65536);
    entity->flags = 0x0100; // 标准查询，递归请求
    entity->qdcount = 1;
    entity->ancount = 0;
    entity->nscount = 0;
    entity->arcount = 0;
    
    // 创建问题部分
    entity->questions = (DNS_QUESTION_ENTITY*)malloc(sizeof(DNS_QUESTION_ENTITY));
    if (!entity->questions) {
        free(entity);
        return NULL;
    }
    
    // 格式化域名
    entity->questions[0].qname = (char*)malloc(256);
    if (!entity->questions[0].qname) {
        free(entity->questions);
        free(entity);
        return NULL;
    }
    format_domain_name(entity->questions[0].qname, hostname);
    entity->questions[0].qtype = qtype;
    entity->questions[0].qclass = 1; // IN class
    
    entity->answers = NULL;
    
    return entity;
}

// 函数：将DNS_ENTITY序列化为DNS协议格式的字节流
int serialize_dns_packet(char* buffer, const DNS_ENTITY* dns_entity) {
    int offset = 0;
    
    // 写入DNS头部
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->id);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->flags);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->qdcount);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->ancount);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->nscount);
    offset += 2;
    *((unsigned short*)(buffer + offset)) = htons(dns_entity->arcount);
    offset += 2;
    
    // 写入问题部分
    for (int i = 0; i < dns_entity->qdcount; i++) {
        // 写入域名
        int qname_len = strlen(dns_entity->questions[i].qname) + 1;
        memcpy(buffer + offset, dns_entity->questions[i].qname, qname_len);
        offset += qname_len;
        
        // 写入qtype和qclass
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->questions[i].qtype);
        offset += 2;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->questions[i].qclass);
        offset += 2;
    }
    
    return offset;
}

// 函数：从缓冲区中读取域名（处理DNS压缩）
int read_name_from_buffer(const char* buffer, int buffer_len, int* offset, char* name, int max_name_len) {
    int pos = *offset;
    int jumped = 0;
    int name_len = 0;
    
    while (pos < buffer_len && buffer[pos] != 0) {
        if ((buffer[pos] & 0xC0) == 0xC0) {
            // 压缩指针
            if (!jumped) {
                *offset = pos + 2;
            }
            pos = ((buffer[pos] & 0x3F) << 8) | (unsigned char)buffer[pos + 1];
            jumped = 1;
        } else {
            // 标签长度
            int label_len = buffer[pos];
            pos++;
            
            if (name_len + label_len + 1 >= max_name_len) {
                return -1; // 名称太长
            }
            
            if (name_len > 0) {
                name[name_len++] = '.';
            }
            
            for (int i = 0; i < label_len && pos < buffer_len; i++) {
                name[name_len++] = buffer[pos++];
            }
        }
    }
    
    name[name_len] = '\0';
    
    if (!jumped) {
        *offset = pos + 1;
    }
    
    return name_len;
}

// 函数：从DNS协议格式的字节流解析为DNS_ENTITY
DNS_ENTITY* parse_dns_packet(const char* buffer, int buffer_len) {
    if (buffer_len < 12) return NULL; // DNS头部至少12字节
    
    DNS_ENTITY* entity = (DNS_ENTITY*)malloc(sizeof(DNS_ENTITY));
    if (!entity) return NULL;
    
    int offset = 0;
    
    // 解析DNS头部
    entity->id = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    entity->flags = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    entity->qdcount = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    entity->ancount = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    entity->nscount = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    entity->arcount = ntohs(*((unsigned short*)(buffer + offset)));
    offset += 2;
    
    // 解析问题部分
    if (entity->qdcount > 0) {
        entity->questions = (DNS_QUESTION_ENTITY*)malloc(sizeof(DNS_QUESTION_ENTITY) * entity->qdcount);
        if (!entity->questions) {
            free(entity);
            return NULL;
        }
        
        for (int i = 0; i < entity->qdcount; i++) {
            // 读取域名
            entity->questions[i].qname = (char*)malloc(256);
            if (!entity->questions[i].qname) {
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->questions[j].qname);
                }
                free(entity->questions);
                free(entity);
                return NULL;
            }
            
            read_name_from_buffer(buffer, buffer_len, &offset, entity->questions[i].qname, 256);
            
            // 读取qtype和qclass
            entity->questions[i].qtype = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->questions[i].qclass = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
        }
    } else {
        entity->questions = NULL;
    }
    
    // 解析回答部分
    if (entity->ancount > 0) {
        entity->answers = (R_DATA_ENTITY*)malloc(sizeof(R_DATA_ENTITY) * entity->ancount);
        if (!entity->answers) {
            free(entity);
            return NULL;
        }
        
        for (int i = 0; i < entity->ancount; i++) {
            // 读取域名
            entity->answers[i].name = (char*)malloc(256);
            if (!entity->answers[i].name) {
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->answers[j].name);
                    free(entity->answers[j].rdata);
                }
                free(entity->answers);
                free(entity);
                return NULL;
            }
            
            read_name_from_buffer(buffer, buffer_len, &offset, entity->answers[i].name, 256);
            
            // 读取资源记录的固定部分
            entity->answers[i].type = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->answers[i]._class = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->answers[i].ttl = ntohl(*((unsigned int*)(buffer + offset)));
            offset += 4;
            entity->answers[i].data_len = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;            // 读取RDATA
            // 对于CNAME记录，需要更多内存来存储解析后的域名字符串
            int rdata_size = (entity->answers[i].type == CNAME) ? 256 : entity->answers[i].data_len;
            entity->answers[i].rdata = (char*)malloc(rdata_size);
            if (!entity->answers[i].rdata) {
                free(entity->answers[i].name);
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->answers[j].name);
                    free(entity->answers[j].rdata);
                }
                free(entity->answers);
                free(entity);
                return NULL;
            }
            
            // 如果是CNAME记录，需要特殊处理域名解析
            if (entity->answers[i].type == CNAME) {
                // 对于CNAME记录，解析域名而不是直接拷贝字节
                char cname[256];
                int temp_offset = offset;
                if (read_name_from_buffer(buffer, buffer_len, &temp_offset, cname, sizeof(cname)) > 0) {
                    // 将解析后的域名存储为字符串
                    strcpy(entity->answers[i].rdata, cname);
                } else {
                    strcpy(entity->answers[i].rdata, "(parse error)");
                }
            } else {
                // 对于其他类型的记录，直接拷贝原始数据
                memcpy(entity->answers[i].rdata, buffer + offset, entity->answers[i].data_len);
            }
            
            offset += entity->answers[i].data_len;
        }    } else {
        entity->answers = NULL;
    }
    
    return entity;
}

// 函数：释放DNS_ENTITY及其相关资源
void free_dns_entity(DNS_ENTITY* entity) {
    if (!entity) return;
    
    if (entity->questions) {
        for (int i = 0; i < entity->qdcount; i++) {
            free(entity->questions[i].qname);
        }
        free(entity->questions);
    }
    
    if (entity->answers) {
        for (int i = 0; i < entity->ancount; i++) {
            free(entity->answers[i].name);
            free(entity->answers[i].rdata);
        }
        free(entity->answers);
    }
    
    free(entity);
}