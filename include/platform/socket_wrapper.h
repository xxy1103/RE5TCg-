#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

/**
 * @file socket_wrapper.h
 * @brief 跨平台套接字抽象层
 * 
 * 这个头文件提供了一个统一的套接字接口，隐藏了不同操作系统间的差异。
 * 支持Windows (Winsock2)、Linux和macOS (BSD sockets)。
 */

#ifdef _WIN32
    /* Windows平台 */
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define WOULD_BLOCK_ERROR WSAEWOULDBLOCK
    
#else
    /* Unix/Linux/macOS平台 */
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <netdb.h>
    
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE (-1)
    #define SOCKET_ERROR_VALUE (-1)
    #define WOULD_BLOCK_ERROR EWOULDBLOCK
    
    /* Windows兼容性宏 */
    #define closesocket close
    #define ioctlsocket fcntl
    
#endif

/* 通用常量定义 */
#define PLATFORM_SUCCESS 1
#define PLATFORM_ERROR 0

/**
 * @brief 初始化网络子系统
 * @return PLATFORM_SUCCESS 成功，PLATFORM_ERROR 失败
 */
int platform_socket_init(void);

/**
 * @brief 清理网络子系统
 */
void platform_socket_cleanup(void);

/**
 * @brief 创建UDP套接字
 * @return 有效的套接字描述符，失败返回INVALID_SOCKET_VALUE
 */
socket_t platform_create_udp_socket(void);

/**
 * @brief 设置套接字为非阻塞模式
 * @param sock 套接字描述符
 * @return PLATFORM_SUCCESS 成功，PLATFORM_ERROR 失败
 */
int platform_set_nonblocking(socket_t sock);

/**
 * @brief 设置套接字重用地址选项
 * @param sock 套接字描述符
 * @return PLATFORM_SUCCESS 成功，PLATFORM_ERROR 失败
 */
int platform_set_reuseaddr(socket_t sock);

/**
 * @brief 关闭套接字
 * @param sock 套接字描述符
 * @return PLATFORM_SUCCESS 成功，PLATFORM_ERROR 失败
 */
int platform_close_socket(socket_t sock);

/**
 * @brief 获取最后的网络错误代码
 * @return 错误代码
 */
int platform_get_last_error(void);

/**
 * @brief 检查错误是否为"操作将阻塞"类型
 * @param error 错误代码
 * @return 1 如果是阻塞错误，0 否则
 */
int platform_is_would_block_error(int error);

/**
 * @brief 获取错误代码的描述字符串
 * @param error 错误代码
 * @return 错误描述字符串
 */
const char* platform_get_error_string(int error);

/**
 * @brief 跨平台的select函数封装
 * @param nfds 最大文件描述符+1 (Windows下忽略)
 * @param readfds 读取文件描述符集
 * @param writefds 写入文件描述符集
 * @param exceptfds 异常文件描述符集
 * @param timeout 超时时间
 * @return 准备好的文件描述符数量，错误返回-1
 */
int platform_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);

#endif /* SOCKET_WRAPPER_H */
