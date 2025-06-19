#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    // Windows-specific includes
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    // Linux-specific includes
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <arpa/inet.h> // 包含 inet_ntoa
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

void platform_init();
void platform_cleanup();
SOCKET create_socket();
int set_socket_reuseaddr(SOCKET sock);
int set_socket_nonblocking(SOCKET sock);
int platform_get_last_error();

#endif // PLATFORM_H
