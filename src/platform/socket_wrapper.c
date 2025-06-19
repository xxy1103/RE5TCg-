#include "platform/socket_wrapper.h"
#include "debug/debug.h"

#ifdef _WIN32
/* Windows平台实现 */

static WSADATA g_wsaData;

int platform_socket_init(void) {
    if (WSAStartup(MAKEWORD(2, 2), &g_wsaData) != 0) {
        log_error("WSAStartup 失败，错误码: %d", WSAGetLastError());
        return PLATFORM_ERROR;
    }
    log_debug("Windows网络子系统初始化成功");
    return PLATFORM_SUCCESS;
}

void platform_socket_cleanup(void) {
    WSACleanup();
    log_debug("Windows网络子系统清理完成");
}

socket_t platform_create_udp_socket(void) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        log_error("创建UDP套接字失败，错误码: %d", WSAGetLastError());
    }
    return sock;
}

int platform_set_nonblocking(socket_t sock) {
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
        log_error("设置非阻塞模式失败，错误码: %d", WSAGetLastError());
        return PLATFORM_ERROR;
    }
    return PLATFORM_SUCCESS;
}

int platform_set_reuseaddr(socket_t sock) {
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
        log_warn("设置SO_REUSEADDR失败，错误码: %d", WSAGetLastError());
        return PLATFORM_ERROR;
    }
    return PLATFORM_SUCCESS;
}

int platform_close_socket(socket_t sock) {
    if (closesocket(sock) == SOCKET_ERROR) {
        log_error("关闭套接字失败，错误码: %d", WSAGetLastError());
        return PLATFORM_ERROR;
    }
    return PLATFORM_SUCCESS;
}

int platform_get_last_error(void) {
    return WSAGetLastError();
}

int platform_is_would_block_error(int error) {
    return (error == WSAEWOULDBLOCK);
}

const char* platform_get_error_string(int error) {
    static char error_buffer[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   error_buffer, sizeof(error_buffer), NULL);
    return error_buffer;
}

int platform_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
    /* Windows的select不使用nfds参数 */
    (void)nfds;
    int result = select(0, readfds, writefds, exceptfds, timeout);
    if (result == SOCKET_ERROR) {
        log_error("select调用失败，错误码: %d", WSAGetLastError());
    }
    return result;
}

#else
/* Unix/Linux/macOS平台实现 */

int platform_socket_init(void) {
    /* Unix系统不需要特殊初始化 */
    log_debug("Unix网络子系统初始化成功（无需特殊操作）");
    return PLATFORM_SUCCESS;
}

void platform_socket_cleanup(void) {
    /* Unix系统不需要特殊清理 */
    log_debug("Unix网络子系统清理完成（无需特殊操作）");
}

socket_t platform_create_udp_socket(void) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VALUE) {
        log_error("创建UDP套接字失败，错误码: %d (%s)", errno, strerror(errno));
    }
    return sock;
}

int platform_set_nonblocking(socket_t sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        log_error("获取套接字标志失败，错误码: %d (%s)", errno, strerror(errno));
        return PLATFORM_ERROR;
    }
    
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("设置非阻塞模式失败，错误码: %d (%s)", errno, strerror(errno));
        return PLATFORM_ERROR;
    }
    
    return PLATFORM_SUCCESS;
}

int platform_set_reuseaddr(socket_t sock) {
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        log_warn("设置SO_REUSEADDR失败，错误码: %d (%s)", errno, strerror(errno));
        return PLATFORM_ERROR;
    }
    return PLATFORM_SUCCESS;
}

int platform_close_socket(socket_t sock) {
    if (close(sock) == -1) {
        log_error("关闭套接字失败，错误码: %d (%s)", errno, strerror(errno));
        return PLATFORM_ERROR;
    }
    return PLATFORM_SUCCESS;
}

int platform_get_last_error(void) {
    return errno;
}

int platform_is_would_block_error(int error) {
    return (error == EWOULDBLOCK || error == EAGAIN);
}

const char* platform_get_error_string(int error) {
    return strerror(error);
}

int platform_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
    int result = select(nfds, readfds, writefds, exceptfds, timeout);
    if (result == -1) {
        log_error("select调用失败，错误码: %d (%s)", errno, strerror(errno));
    }
    return result;
}

#endif
