# DNS代理服务器

这个项目是一个高性能的DNS代理服务器，实现了**并发处理多个DNS请求**的功能。

## 功能特点

1. **监听本地53端口**：接收来自客户端的DNS请求
2. **并发处理**：使用非阻塞I/O和ID映射技术，可同时处理多个DNS请求
3. **ID映射管理**：自动管理客户端请求ID与上游响应ID的映射关系
4. **DNS请求解析**：将收到的DNS请求解析为DNS_ENTITY结构
5. **请求信息打印**：显示接收到的DNS请求的基本信息
6. **请求转发**：将DNS请求转发给上游DNS服务器（默认8.8.8.8）
7. **响应解析**：解析从上游DNS服务器收到的响应
8. **响应转发**：将响应发送回原始请求的客户端
9. **超时处理**：自动清理过期的请求映射关系
10. **高并发支持**：最多支持1000个并发DNS请求

## 核心技术

### 并发处理架构
- **非阻塞I/O**：使用select()系统调用实现事件驱动的网络处理
- **ID映射表**：维护客户端原始ID与服务器分配ID的映射关系
- **请求去重**：避免DNS Transaction ID冲突，确保响应正确返回给对应客户端
- **内存管理**：高效的映射表管理，支持动态添加/删除映射关系
- **超时清理**：定期清理超时的请求，避免内存泄漏

### 数据结构
```c
// DNS映射表项
typedef struct {
    unsigned short original_id;      // 客户端原始请求ID
    unsigned short new_id;          // 分配给上游的新ID
    struct sockaddr_in client_addr; // 客户端地址
    int client_addr_len;            // 客户端地址长度
    time_t timestamp;               // 请求时间戳
    int is_active;                  // 是否激活状态
} dns_mapping_entry_t;
```

## 项目结构

```
src/
├── main.c              # 主程序 - DNS代理服务器实现
├── datagram/
│   └── datagram.c      # DNS报文解析和序列化
├── websocket/
│   └── websocket.c     # 网络通信功能
└── debug/
    └── debug.c         # 调试和日志功能

include/
├── datagram/
│   └── datagram.h      # DNS数据结构定义
├── websocket/
│   └── websocket.h     # 网络通信函数声明
└── debug/
    └── debug.h         # 日志系统定义
```

## 编译和运行

### 编译
```bash
# 进入项目目录
cd d:\windows\desktop\DNS\my_DNS

# 使用CMake编译
cmake --build build
```

### 运行
**重要：必须以管理员权限运行，因为需要绑定到53端口**

1. 以管理员身份打开命令提示符或PowerShell
2. 进入项目目录：
   ```cmd
   cd d:\windows\desktop\DNS\my_DNS
   ```
3. 运行DNS代理服务器：
   ```cmd
   .\build\bin\my_DNS.exe
   ```

## 并发性能测试

### 使用PowerShell测试脚本
项目提供了两个测试脚本来验证并发处理能力：

1. **简单测试脚本**（推荐）：
   ```powershell
   .\test_concurrent_simple.ps1
   ```
   
2. **高级测试脚本**：
   ```powershell
   .\test_concurrent_dns.ps1 -RequestCount 20
   ```

### 手动测试
同时打开多个命令窗口，执行以下命令：
```cmd
nslookup google.com 127.0.0.1
nslookup github.com 127.0.0.1
nslookup stackoverflow.com 127.0.0.1
```

### 性能指标
- **最大并发请求**：1000个
- **请求超时时间**：5秒
- **映射表清理间隔**：10秒
- **响应时间**：通常在50-200ms之间（取决于上游DNS服务器）

## 测试
使用提供的PowerShell测试脚本（需要管理员权限）：
```powershell
# 以管理员身份运行PowerShell
.\test_dns_proxy.ps1
```

或者手动测试：
```cmd
# 查询域名（将DNS服务器指向127.0.0.1）
nslookup www.baidu.com 127.0.0.1
nslookup google.com 127.0.0.1
```

## 工作流程

1. **启动服务器**：程序启动后监听本地53端口
2. **接收请求**：等待客户端的DNS查询请求
3. **解析请求**：将二进制DNS报文解析为结构化数据
4. **打印信息**：显示请求的详细信息（事务ID、查询域名、记录类型等）
5. **转发请求**：将请求发送给上游DNS服务器（8.8.8.8）
6. **接收响应**：从上游服务器获取DNS响应
7. **解析响应**：将响应解析为结构化数据并打印信息
8. **转发响应**：将响应发送回原始客户端

## 日志信息

程序会输出详细的日志信息，包括：
- 接收到的DNS请求详情
- 转发到上游服务器的状态
- 从上游服务器收到的响应详情
- 发送给客户端的响应状态

## 技术特点

- 使用Windows Sockets API进行网络通信
- 支持DNS报文的完整解析和序列化
- 实现DNS协议的代理转发功能
- 提供详细的调试日志输出
- 支持IPv4 A记录和IPv6 AAAA记录查询

## 注意事项

1. **管理员权限**：必须以管理员权限运行才能绑定到53端口
2. **防火墙设置**：可能需要在Windows防火墙中允许程序通过
3. **端口占用**：确保53端口没有被其他程序占用
4. **网络连接**：需要能够访问外部DNS服务器（8.8.8.8）

## 系统要求

- Windows操作系统
- Visual Studio或MinGW编译器
- CMake 3.10+
- 管理员权限
