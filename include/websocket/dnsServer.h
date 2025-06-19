#ifndef DNSSERVER_H
#define DNSSERVER_H

#include "websocket/websocket.h" // 包含自定义的 WebSocket 头文件
#include "websocket/datagram.h" // 包含自定义的数据报头文件，定义了 DNS 报文相关结构体和函数
#include "idmapping/idmapping.h" // 包含 ID 映射相关的头文件
#include "debug/debug.h"
#include <time.h>


// DNS代理服务器函数
int start_dns_proxy_server();
int forward_to_upstream_dns(char* request_buffer, int request_len, char* response_buffer, int* response_len);
int handle_dns_request(char* request_buffer, int request_len, struct sockaddr_in* client_addr, int client_addr_len, SOCKET server_socket);



// 新增并发处理相关函数
int handle_receive();
void handle_client_requests(DNS_ENTITY* dns_entity,struct sockaddr_in source_addr, int source_addr_len,int receive_len);
void handle_upstream_responses(DNS_ENTITY* dns_entity,struct sockaddr_in source_addr, int source_addr_len, int receive_len);
int forward_request_to_upstream(char* request_buffer, int request_len) ;
#endif // DNSSERVER_H