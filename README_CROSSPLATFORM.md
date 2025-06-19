# DNS Relay Server - 跨平台版本

## 项目概述

这是一个高性能的跨平台DNS代理服务器，支持Windows、Linux和macOS。项目采用了优雅的抽象层设计，隐藏了不同操作系统间网络编程API的差异。

## 支持的平台

- ✅ **Windows** (Windows 7/8/10/11, Windows Server)
- ✅ **Linux** (Ubuntu, CentOS, Debian, Arch Linux等)
- ✅ **macOS** (10.12+)

## 架构特性

### 🎯 跨平台抽象层
- **统一的套接字接口**：隐藏Windows Winsock2和Unix BSD sockets的差异
- **平台感知的错误处理**：自动处理不同系统的错误码和描述
- **智能的构建配置**：CMake自动检测平台并链接相应的库

### ⚡ 高性能设计
- **事件驱动架构**：使用select()实现高并发处理
- **非阻塞I/O**：避免线程阻塞，提高响应速度
- **ID映射机制**：支持多客户端并发请求，自动处理ID冲突

### 🔧 核心功能
- DNS查询代理和转发
- 智能请求ID管理
- 并发客户端支持
- 详细的日志记录
- 过期映射自动清理

## 快速开始

### 构建要求

#### Windows
```bash
# 使用MinGW-w64或Visual Studio
# 需要CMake 3.10+
```

#### Linux
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake

# CentOS/RHEL
sudo yum install gcc cmake make

# Arch Linux
sudo pacman -S base-devel cmake
```

#### macOS
```bash
# 使用Homebrew
brew install cmake

# 或使用Xcode Command Line Tools
xcode-select --install
```

### 编译步骤

```bash
# 克隆或下载项目
cd my_DNS

# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
cmake --build .

# 运行
./bin/my_DNS        # Linux/macOS
./bin/my_DNS.exe    # Windows
```

### 高级构建选项

```bash
# Debug构建（包含调试信息）
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release构建（优化性能）
cmake -DCMAKE_BUILD_TYPE=Release ..

# 指定编译器
cmake -DCMAKE_C_COMPILER=gcc ..
cmake -DCMAKE_C_COMPILER=clang ..
```

## 项目结构

```
my_DNS/
├── CMakeLists.txt              # 跨平台构建配置
├── include/
│   ├── platform/
│   │   └── socket_wrapper.h    # 🔥 跨平台套接字抽象层
│   ├── websocket/
│   │   ├── websocket.h         # 网络通信接口
│   │   ├── datagram.h          # DNS数据报处理
│   │   └── dnsServer.h         # DNS服务器核心
│   ├── debug/
│   │   └── debug.h             # 日志和调试
│   └── idmapping/
│       └── idmapping.h         # ID映射管理
├── src/
│   ├── platform/
│   │   └── socket_wrapper.c    # 🔥 跨平台实现
│   ├── websocket/
│   │   ├── websocket.c         # 网络通信实现
│   │   ├── datagram.c          # DNS报文解析
│   │   └── dnsServer.c         # 服务器主逻辑
│   ├── debug/
│   │   └── debug.c             # 日志实现
│   ├── idmapping/
│   │   └── idmapping.c         # ID映射实现
│   └── main.c                  # 程序入口
└── build/                      # 构建输出目录
```

## 核心改进说明

### 1. 跨平台套接字抽象 (`socket_wrapper.h/c`)

**问题**：原项目使用Windows特定的Winsock2 API，无法在其他平台运行。

**解决方案**：创建统一的套接字抽象层：

```c
// 统一的套接字类型
typedef SOCKET socket_t;      // Windows
typedef int socket_t;         // Unix/Linux/macOS

// 统一的函数接口
int platform_socket_init(void);           // 初始化网络子系统
socket_t platform_create_udp_socket(void); // 创建UDP套接字
int platform_set_nonblocking(socket_t sock); // 设置非阻塞
void platform_socket_cleanup(void);       // 清理资源
```

### 2. 错误处理统一化

**原代码**：
```c
int error = WSAGetLastError();  // Windows特定
if (error == WSAEWOULDBLOCK) { ... }
```

**改进后**：
```c
int error = platform_get_last_error();  // 跨平台
if (platform_is_would_block_error(error)) { ... }
const char* desc = platform_get_error_string(error);
```

### 3. 智能构建配置

CMakeLists.txt自动检测平台并配置相应的链接库：

```cmake
if(WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32)
elseif(UNIX)
    find_library(PTHREAD_LIB pthread)
    target_link_libraries(${PROJECT_NAME} ${PTHREAD_LIB})
endif()
```

## 配置说明

### DNS服务器配置
```c
#define DNS_SERVER "8.8.8.8"  // 可修改上游DNS服务器
#define DNS_PORT 53            // 监听端口
#define BUF_SIZE 65536         // 缓冲区大小
```

### 日志级别
- `LOG_LEVEL_DEBUG`: 详细调试信息
- `LOG_LEVEL_INFO`: 一般信息
- `LOG_LEVEL_WARN`: 警告信息
- `LOG_LEVEL_ERROR`: 错误信息

## 性能特性

### 并发处理能力
- **事件驱动**: 单线程处理多个并发连接
- **非阻塞I/O**: 避免阻塞等待，提高吞吐量
- **批量处理**: 一次select调用处理多个请求

### 内存管理
- **映射表**: 自动管理客户端-服务器ID映射关系
- **过期清理**: 定期清理超时的映射，防止内存泄漏
- **缓冲区复用**: 减少内存分配开销

### 错误恢复
- **网络异常**: 自动重试和错误报告
- **资源清理**: 程序退出时自动清理所有资源
- **平台兼容**: 统一的错误处理机制

## 开发指南

### 添加新功能

1. **网络相关功能**: 在`socket_wrapper.h`中添加新的抽象函数
2. **DNS处理**: 在`datagram.h`中扩展DNS报文处理
3. **服务器逻辑**: 在`dnsServer.h`中添加新的服务功能

### 平台特定代码

```c
#ifdef _WIN32
    // Windows特定代码
#else
    // Unix/Linux/macOS代码
#endif
```

### 调试建议

```bash
# 启用调试模式编译
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 运行时查看详细日志
./my_DNS  # 查看 log.txt 文件
```

## 故障排除

### 常见问题

1. **端口占用**: 确保53端口未被其他服务占用
2. **权限问题**: 在某些系统上可能需要管理员权限绑定53端口
3. **防火墙**: 检查防火墙是否允许UDP 53端口通信

### 平台特定问题

#### Windows
- 确保安装了Visual Studio或MinGW-w64
- 某些杀毒软件可能阻止网络操作

#### Linux
```bash
# 允许非root用户绑定1024以下端口（可选）
sudo setcap cap_net_bind_service=+ep ./my_DNS

# 或使用其他端口测试
# 修改 DNS_PORT 为 5353 等
```

#### macOS
```bash
# 可能需要允许网络访问
# 在"系统偏好设置" -> "安全性与隐私" -> "防火墙"中配置
```

## 贡献指南

欢迎提交Issue和Pull Request！请确保：

1. 遵循现有的代码风格
2. 添加适当的错误处理
3. 更新相关文档
4. 在多个平台上测试

## 许可证

[根据项目需要添加许可证信息]

## 更新日志

### v1.0.0 (跨平台版本)
- ✅ 添加完整的跨平台支持
- ✅ 实现套接字抽象层
- ✅ 优化错误处理机制
- ✅ 改进构建系统
- ✅ 增强文档和示例
