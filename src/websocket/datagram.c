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
//只在测试中使用，DNS服务器中没有使用
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
    entity->questions[0].qtype = qtype;    entity->questions[0].qclass = 1; // IN class
    
    entity->answers = NULL;
    entity->authorities = NULL;
    entity->additionals = NULL;
    
    return entity;
}

// 函数：将DNS_ENTITY序列化为DNS协议格式的字节流
int serialize_dns_packet(char* buffer, const DNS_ENTITY* dns_entity) {    int offset = 0;
    
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
        // 检查域名格式并写入域名
        if (strchr(dns_entity->questions[i].qname, '.') != NULL) {
            // 包含点号，是人类可读格式，需要转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->questions[i].qname);
            int qname_len = get_dns_name_length(dns_formatted);
            memcpy(buffer + offset, dns_formatted, qname_len);
            offset += qname_len;
        } else {
            // 不包含点号，可能已经是DNS格式或者是单个标签
            int qname_len = get_dns_name_length(dns_entity->questions[i].qname);
            memcpy(buffer + offset, dns_entity->questions[i].qname, qname_len);
            offset += qname_len;
        }
        
        // 写入qtype和qclass
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->questions[i].qtype);
        offset += 2;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->questions[i].qclass);
        offset += 2;
    }    // 写入答案部分
    for (int i = 0; i < dns_entity->ancount; i++) {
        // 检查域名格式并写入域名
        // 改进的DNS格式检测：检查是否包含点号来判断是否为人类可读格式
        if (strchr(dns_entity->answers[i].name, '.') != NULL) {
            // 包含点号，是人类可读格式，需要转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->answers[i].name);
            int name_len = get_dns_name_length(dns_formatted);
            memcpy(buffer + offset, dns_formatted, name_len);
            offset += name_len;
        } else {
            // 不包含点号，可能已经是DNS格式或者是单个标签
            // 直接使用原始数据
            int name_len = get_dns_name_length(dns_entity->answers[i].name);
            memcpy(buffer + offset, dns_entity->answers[i].name, name_len);
            offset += name_len;
        }
        
        // 写入type、class、ttl、data_len
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->answers[i].type);
        offset += 2;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->answers[i]._class);
        offset += 2;
        *((unsigned int*)(buffer + offset)) = htonl(dns_entity->answers[i].ttl);
        offset += 4;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->answers[i].data_len);
        offset += 2;        
        // 写入rdata
        if (dns_entity->answers[i].type == CNAME) {
            // 对于CNAME记录，需要将字符串格式的域名转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->answers[i].rdata);
            int cname_len = get_dns_name_length(dns_formatted);
            
            // 更新data_len为实际的DNS格式长度（回退到data_len字段位置）
            *((unsigned short*)(buffer + offset - 2)) = htons(cname_len);
            
            // 写入DNS格式的CNAME数据
            memcpy(buffer + offset, dns_formatted, cname_len);
            offset += cname_len;
        } else {
            // 对于其他类型的记录，直接复制原始数据
            memcpy(buffer + offset, dns_entity->answers[i].rdata, dns_entity->answers[i].data_len);
            offset += dns_entity->answers[i].data_len;
        }
    }    // 写入权威记录部分
    for (int i = 0; i < dns_entity->nscount; i++) {
        // 检查域名格式并写入域名
        if (strchr(dns_entity->authorities[i].name, '.') != NULL) {
            // 包含点号，是人类可读格式，需要转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->authorities[i].name);
            int name_len = get_dns_name_length(dns_formatted);
            memcpy(buffer + offset, dns_formatted, name_len);
            offset += name_len;
        } else {
            // 不包含点号，可能已经是DNS格式或者是单个标签
            int name_len = get_dns_name_length(dns_entity->authorities[i].name);
            memcpy(buffer + offset, dns_entity->authorities[i].name, name_len);
            offset += name_len;
        }
        
        // 写入type、class、ttl、data_len
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->authorities[i].type);
        offset += 2;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->authorities[i]._class);
        offset += 2;
        *((unsigned int*)(buffer + offset)) = htonl(dns_entity->authorities[i].ttl);
        offset += 4;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->authorities[i].data_len);
        offset += 2;        
        // 写入rdata
        if (dns_entity->authorities[i].type == CNAME) {
            // 对于CNAME记录，需要将字符串格式的域名转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->authorities[i].rdata);
            int cname_len = get_dns_name_length(dns_formatted);
            
            // 更新data_len为实际的DNS格式长度
            *((unsigned short*)(buffer + offset - 2)) = htons(cname_len);
            
            // 写入DNS格式的CNAME数据
            memcpy(buffer + offset, dns_formatted, cname_len);
            offset += cname_len;
        } else {
            // 对于其他类型的记录，直接复制原始数据
            memcpy(buffer + offset, dns_entity->authorities[i].rdata, dns_entity->authorities[i].data_len);
            offset += dns_entity->authorities[i].data_len;
        }
    }

    // 写入附加记录部分
    for (int i = 0; i < dns_entity->arcount; i++) {
        // 检查域名格式并写入域名
        if (strchr(dns_entity->additionals[i].name, '.') != NULL) {
            // 包含点号，是人类可读格式，需要转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->additionals[i].name);
            int name_len = get_dns_name_length(dns_formatted);
            memcpy(buffer + offset, dns_formatted, name_len);
            offset += name_len;
        } else {
            // 不包含点号，可能已经是DNS格式或者是单个标签
            int name_len = get_dns_name_length(dns_entity->additionals[i].name);
            memcpy(buffer + offset, dns_entity->additionals[i].name, name_len);
            offset += name_len;
        }
        
        // 写入type、class、ttl、data_len
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->additionals[i].type);
        offset += 2;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->additionals[i]._class);
        offset += 2;
        *((unsigned int*)(buffer + offset)) = htonl(dns_entity->additionals[i].ttl);
        offset += 4;
        *((unsigned short*)(buffer + offset)) = htons(dns_entity->additionals[i].data_len);
        offset += 2;        
        // 写入rdata
        if (dns_entity->additionals[i].type == CNAME) {
            // 对于CNAME记录，需要将字符串格式的域名转换为DNS格式
            char dns_formatted[256];
            format_domain_name(dns_formatted, dns_entity->additionals[i].rdata);
            int cname_len = get_dns_name_length(dns_formatted);
            
            // 更新data_len为实际的DNS格式长度
            *((unsigned short*)(buffer + offset - 2)) = htons(cname_len);
            
            // 写入DNS格式的CNAME数据
            memcpy(buffer + offset, dns_formatted, cname_len);
            offset += cname_len;
        } else {
            // 对于其他类型的记录，直接复制原始数据
            memcpy(buffer + offset, dns_entity->additionals[i].rdata, dns_entity->additionals[i].data_len);
            offset += dns_entity->additionals[i].data_len;
        }
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
    
    // 解析权威记录部分
    if (entity->nscount > 0) {
        entity->authorities = (R_DATA_ENTITY*)malloc(sizeof(R_DATA_ENTITY) * entity->nscount);
        if (!entity->authorities) {
            // 清理已分配的内存
            if (entity->questions) {
                for (int j = 0; j < entity->qdcount; j++) {
                    free(entity->questions[j].qname);
                }
                free(entity->questions);
            }
            if (entity->answers) {
                for (int j = 0; j < entity->ancount; j++) {
                    free(entity->answers[j].name);
                    free(entity->answers[j].rdata);
                }
                free(entity->answers);
            }
            free(entity);
            return NULL;
        }
        
        for (int i = 0; i < entity->nscount; i++) {
            // 读取域名
            entity->authorities[i].name = (char*)malloc(256);
            if (!entity->authorities[i].name) {
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->authorities[j].name);
                    free(entity->authorities[j].rdata);
                }
                free(entity->authorities);
                // 清理其他已分配的内存
                if (entity->questions) {
                    for (int j = 0; j < entity->qdcount; j++) {
                        free(entity->questions[j].qname);
                    }
                    free(entity->questions);
                }
                if (entity->answers) {
                    for (int j = 0; j < entity->ancount; j++) {
                        free(entity->answers[j].name);
                        free(entity->answers[j].rdata);
                    }
                    free(entity->answers);
                }
                free(entity);
                return NULL;
            }
            
            read_name_from_buffer(buffer, buffer_len, &offset, entity->authorities[i].name, 256);
            
            // 读取资源记录的固定部分
            entity->authorities[i].type = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->authorities[i]._class = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->authorities[i].ttl = ntohl(*((unsigned int*)(buffer + offset)));
            offset += 4;
            entity->authorities[i].data_len = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            
            // 读取RDATA
            int rdata_size = (entity->authorities[i].type == CNAME) ? 256 : entity->authorities[i].data_len;
            entity->authorities[i].rdata = (char*)malloc(rdata_size);
            if (!entity->authorities[i].rdata) {
                free(entity->authorities[i].name);
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->authorities[j].name);
                    free(entity->authorities[j].rdata);
                }
                free(entity->authorities);
                // 清理其他已分配的内存
                if (entity->questions) {
                    for (int j = 0; j < entity->qdcount; j++) {
                        free(entity->questions[j].qname);
                    }
                    free(entity->questions);
                }
                if (entity->answers) {
                    for (int j = 0; j < entity->ancount; j++) {
                        free(entity->answers[j].name);
                        free(entity->answers[j].rdata);
                    }
                    free(entity->answers);
                }
                free(entity);
                return NULL;
            }
            
            // 如果是CNAME记录，需要特殊处理域名解析
            if (entity->authorities[i].type == CNAME) {
                char cname[256];
                int temp_offset = offset;
                if (read_name_from_buffer(buffer, buffer_len, &temp_offset, cname, sizeof(cname)) > 0) {
                    strcpy(entity->authorities[i].rdata, cname);
                } else {
                    strcpy(entity->authorities[i].rdata, "(parse error)");
                }
            } else {
                memcpy(entity->authorities[i].rdata, buffer + offset, entity->authorities[i].data_len);
            }
            
            offset += entity->authorities[i].data_len;
        }
    } else {
        entity->authorities = NULL;
    }
    
    // 解析附加记录部分
    if (entity->arcount > 0) {
        entity->additionals = (R_DATA_ENTITY*)malloc(sizeof(R_DATA_ENTITY) * entity->arcount);
        if (!entity->additionals) {
            // 清理已分配的内存
            if (entity->questions) {
                for (int j = 0; j < entity->qdcount; j++) {
                    free(entity->questions[j].qname);
                }
                free(entity->questions);
            }
            if (entity->answers) {
                for (int j = 0; j < entity->ancount; j++) {
                    free(entity->answers[j].name);
                    free(entity->answers[j].rdata);
                }
                free(entity->answers);
            }
            if (entity->authorities) {
                for (int j = 0; j < entity->nscount; j++) {
                    free(entity->authorities[j].name);
                    free(entity->authorities[j].rdata);
                }
                free(entity->authorities);
            }
            free(entity);
            return NULL;
        }
        
        for (int i = 0; i < entity->arcount; i++) {
            // 读取域名
            entity->additionals[i].name = (char*)malloc(256);
            if (!entity->additionals[i].name) {
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->additionals[j].name);
                    free(entity->additionals[j].rdata);
                }
                free(entity->additionals);
                // 清理其他已分配的内存
                if (entity->questions) {
                    for (int j = 0; j < entity->qdcount; j++) {
                        free(entity->questions[j].qname);
                    }
                    free(entity->questions);
                }
                if (entity->answers) {
                    for (int j = 0; j < entity->ancount; j++) {
                        free(entity->answers[j].name);
                        free(entity->answers[j].rdata);
                    }
                    free(entity->answers);
                }
                if (entity->authorities) {
                    for (int j = 0; j < entity->nscount; j++) {
                        free(entity->authorities[j].name);
                        free(entity->authorities[j].rdata);
                    }
                    free(entity->authorities);
                }
                free(entity);
                return NULL;
            }
            
            read_name_from_buffer(buffer, buffer_len, &offset, entity->additionals[i].name, 256);
            
            // 读取资源记录的固定部分
            entity->additionals[i].type = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->additionals[i]._class = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            entity->additionals[i].ttl = ntohl(*((unsigned int*)(buffer + offset)));
            offset += 4;
            entity->additionals[i].data_len = ntohs(*((unsigned short*)(buffer + offset)));
            offset += 2;
            
            // 读取RDATA
            int rdata_size = (entity->additionals[i].type == CNAME) ? 256 : entity->additionals[i].data_len;
            entity->additionals[i].rdata = (char*)malloc(rdata_size);
            if (!entity->additionals[i].rdata) {
                free(entity->additionals[i].name);
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(entity->additionals[j].name);
                    free(entity->additionals[j].rdata);
                }
                free(entity->additionals);
                // 清理其他已分配的内存
                if (entity->questions) {
                    for (int j = 0; j < entity->qdcount; j++) {
                        free(entity->questions[j].qname);
                    }
                    free(entity->questions);
                }
                if (entity->answers) {
                    for (int j = 0; j < entity->ancount; j++) {
                        free(entity->answers[j].name);
                        free(entity->answers[j].rdata);
                    }
                    free(entity->answers);
                }
                if (entity->authorities) {
                    for (int j = 0; j < entity->nscount; j++) {
                        free(entity->authorities[j].name);
                        free(entity->authorities[j].rdata);
                    }
                    free(entity->authorities);
                }
                free(entity);
                return NULL;
            }
            
            // 如果是CNAME记录，需要特殊处理域名解析
            if (entity->additionals[i].type == CNAME) {
                char cname[256];
                int temp_offset = offset;
                if (read_name_from_buffer(buffer, buffer_len, &temp_offset, cname, sizeof(cname)) > 0) {
                    strcpy(entity->additionals[i].rdata, cname);
                } else {
                    strcpy(entity->additionals[i].rdata, "(parse error)");
                }
            } else {
                memcpy(entity->additionals[i].rdata, buffer + offset, entity->additionals[i].data_len);
            }
            
            offset += entity->additionals[i].data_len;
        }
    } else {
        entity->additionals = NULL;
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
    
    if (entity->authorities) {
        for (int i = 0; i < entity->nscount; i++) {
            free(entity->authorities[i].name);
            free(entity->authorities[i].rdata);
        }
        free(entity->authorities);
    }
    
    if (entity->additionals) {
        for (int i = 0; i < entity->arcount; i++) {
            free(entity->additionals[i].name);
            free(entity->additionals[i].rdata);
        }
        free(entity->additionals);
    }
      free(entity);
}

// 函数：获取DNS记录类型的字符串表示
const char* get_record_type_name(unsigned short type) {
    switch(type) {
        case A: return "A";
        case AAAA: return "AAAA";
        case CNAME: return "CNAME";
        case MX: return "MX";
        case 2: return "NS";
        case 6: return "SOA";
        case 12: return "PTR";
        case 16: return "TXT";
        default: return "UNKNOWN";
    }
}

// 函数：格式化IP地址
void format_ip_address(char* output, const R_DATA_ENTITY* record) {
    if (record->type == A && record->data_len == 4) {
        // IPv4地址
        unsigned char* ip = (unsigned char*)record->rdata;
        sprintf(output, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else if (record->type == AAAA && record->data_len == 16) {
        // IPv6地址
        unsigned char* ip = (unsigned char*)record->rdata;
        sprintf(output, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7],
                ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]);
    } else if (record->type == CNAME) {
        // CNAME记录
        strcpy(output, record->rdata);
    } else {
        // 其他类型显示数据长度
        sprintf(output, "[%d bytes of data]", record->data_len);
    }
}

// 函数：将DNS_ENTITY转换为格式化的字符串表示
char* dns_entity_to_string(const DNS_ENTITY* entity) {
    if (!entity) return NULL;
    
    // 分配一个足够大的缓冲区来存储格式化的字符串
    char* result = (char*)malloc(8192); // 8KB应该足够大
    if (!result) return NULL;
    
    char temp[1024];
    result[0] = '\0'; // 初始化为空字符串
    
    // DNS头部信息
    sprintf(temp, "=== DNS Message ===\n");
    strcat(result, temp);
    
    sprintf(temp, "Transaction ID: 0x%04X (%d)\n", entity->id, entity->id);
    strcat(result, temp);
    
    // 解析标志位
    sprintf(temp, "Flags: 0x%04X\n", entity->flags);
    strcat(result, temp);
    
    sprintf(temp, "  QR: %d (%s)\n", (entity->flags >> 15) & 1, 
            ((entity->flags >> 15) & 1) ? "Response" : "Query");
    strcat(result, temp);
    
    sprintf(temp, "  Opcode: %d\n", (entity->flags >> 11) & 0xF);
    strcat(result, temp);
    
    sprintf(temp, "  AA: %d (Authoritative Answer)\n", (entity->flags >> 10) & 1);
    strcat(result, temp);
    
    sprintf(temp, "  TC: %d (Truncated)\n", (entity->flags >> 9) & 1);
    strcat(result, temp);
    
    sprintf(temp, "  RD: %d (Recursion Desired)\n", (entity->flags >> 8) & 1);
    strcat(result, temp);
    
    sprintf(temp, "  RA: %d (Recursion Available)\n", (entity->flags >> 7) & 1);
    strcat(result, temp);
    
    sprintf(temp, "  RCODE: %d\n", entity->flags & 0xF);
    strcat(result, temp);
    
    // 计数信息
    sprintf(temp, "\n=== Counts ===\n");
    strcat(result, temp);
    
    sprintf(temp, "Questions: %d\n", entity->qdcount);
    strcat(result, temp);
    
    sprintf(temp, "Answers: %d\n", entity->ancount);
    strcat(result, temp);
    
    sprintf(temp, "Authority Records: %d\n", entity->nscount);
    strcat(result, temp);
    
    sprintf(temp, "Additional Records: %d\n", entity->arcount);
    strcat(result, temp);
    
    // 问题部分
    if (entity->qdcount > 0 && entity->questions) {
        sprintf(temp, "\n=== Questions ===\n");
        strcat(result, temp);
          for (int i = 0; i < entity->qdcount; i++) {
            sprintf(temp, "Question %d:\n", i + 1);
            strcat(result, temp);
              // 域名已经在parse_dns_packet中转换为可读格式
            sprintf(temp, "  Name: %s\n", entity->questions[i].qname ? entity->questions[i].qname : "(null)");
            strcat(result, temp);
            
            sprintf(temp, "  Type: %d (%s)\n", entity->questions[i].qtype, 
                    get_record_type_name(entity->questions[i].qtype));
            strcat(result, temp);
            
            sprintf(temp, "  Class: %d (%s)\n", entity->questions[i].qclass,
                    entity->questions[i].qclass == 1 ? "IN" : "OTHER");
            strcat(result, temp);
        }
    }
    
    // 答案部分
    if (entity->ancount > 0 && entity->answers) {
        sprintf(temp, "\n=== Answers ===\n");
        strcat(result, temp);
        
        for (int i = 0; i < entity->ancount; i++) {
            sprintf(temp, "Answer %d:\n", i + 1);
            strcat(result, temp);
            
            sprintf(temp, "  Name: %s\n", entity->answers[i].name ? entity->answers[i].name : "(null)");
            strcat(result, temp);
            
            sprintf(temp, "  Type: %d (%s)\n", entity->answers[i].type,
                    get_record_type_name(entity->answers[i].type));
            strcat(result, temp);
            
            sprintf(temp, "  Class: %d (%s)\n", entity->answers[i]._class,
                    entity->answers[i]._class == 1 ? "IN" : "OTHER");
            strcat(result, temp);
            
            sprintf(temp, "  TTL: %u seconds\n", entity->answers[i].ttl);
            strcat(result, temp);
            
            sprintf(temp, "  Data Length: %d bytes\n", entity->answers[i].data_len);
            strcat(result, temp);
            
            char data_str[512];
            format_ip_address(data_str, &entity->answers[i]);
            sprintf(temp, "  Data: %s\n", data_str);
            strcat(result, temp);
        }
    }
    
    // 权威记录部分
    if (entity->nscount > 0 && entity->authorities) {
        sprintf(temp, "\n=== Authority Records ===\n");
        strcat(result, temp);
        
        for (int i = 0; i < entity->nscount; i++) {
            sprintf(temp, "Authority %d:\n", i + 1);
            strcat(result, temp);
            
            sprintf(temp, "  Name: %s\n", entity->authorities[i].name ? entity->authorities[i].name : "(null)");
            strcat(result, temp);
            
            sprintf(temp, "  Type: %d (%s)\n", entity->authorities[i].type,
                    get_record_type_name(entity->authorities[i].type));
            strcat(result, temp);
            
            sprintf(temp, "  Class: %d (%s)\n", entity->authorities[i]._class,
                    entity->authorities[i]._class == 1 ? "IN" : "OTHER");
            strcat(result, temp);
            
            sprintf(temp, "  TTL: %u seconds\n", entity->authorities[i].ttl);
            strcat(result, temp);
            
            sprintf(temp, "  Data Length: %d bytes\n", entity->authorities[i].data_len);
            strcat(result, temp);
            
            char data_str[512];
            format_ip_address(data_str, &entity->authorities[i]);
            sprintf(temp, "  Data: %s\n", data_str);
            strcat(result, temp);
        }
    }
    
    // 附加记录部分
    if (entity->arcount > 0 && entity->additionals) {
        sprintf(temp, "\n=== Additional Records ===\n");
        strcat(result, temp);
        
        for (int i = 0; i < entity->arcount; i++) {
            sprintf(temp, "Additional %d:\n", i + 1);
            strcat(result, temp);
            
            sprintf(temp, "  Name: %s\n", entity->additionals[i].name ? entity->additionals[i].name : "(null)");
            strcat(result, temp);
            
            sprintf(temp, "  Type: %d (%s)\n", entity->additionals[i].type,
                    get_record_type_name(entity->additionals[i].type));
            strcat(result, temp);
            
            sprintf(temp, "  Class: %d (%s)\n", entity->additionals[i]._class,
                    entity->additionals[i]._class == 1 ? "IN" : "OTHER");
            strcat(result, temp);
            
            sprintf(temp, "  TTL: %u seconds\n", entity->additionals[i].ttl);
            strcat(result, temp);
            
            sprintf(temp, "  Data Length: %d bytes\n", entity->additionals[i].data_len);
            strcat(result, temp);
            
            char data_str[512];
            format_ip_address(data_str, &entity->additionals[i]);
            sprintf(temp, "  Data: %s\n", data_str);
            strcat(result, temp);
        }
    }
    
    sprintf(temp, "\n=== End of DNS Message ===\n");
    strcat(result, temp);    
    return result;
}

// 函数：计算DNS格式域名的实际长度
// DNS域名格式：每个标签前有长度字节，以0结尾
// 例如：3www5baidu3com0 - 需要计算到最后的0字节（包含）
int get_dns_name_length(const char* dns_name) {
    if (!dns_name) return 0;
    
    int length = 0;
    
    while (dns_name[length] != 0) {
        int label_len = (unsigned char)dns_name[length];
        
        // 检查是否是压缩指针
        if ((label_len & 0xC0) == 0xC0) {
            // 压缩指针，占用2字节
            length += 2;
            break;
        }
        
        // 跳过长度字节和标签内容
        length += 1 + label_len;
    }
    
    // 如果不是压缩指针，需要包含结尾的0字节
    if (dns_name[length] == 0) {
        length += 1;
    }
    
    return length;
}