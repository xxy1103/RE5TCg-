cmake_minimum_required(VERSION 3.10)

# ==============================================
# 项目信息
# ==============================================
project(my_DNS 
    VERSION 1.0.0
    DESCRIPTION "DNS Relay Server - Computer Network Course Design"
    LANGUAGES C
)

# ==============================================
# 编译配置
# ==============================================

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 设置构建类型（如果没有指定的话）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 输出目录配置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ==============================================
# 编译选项
# ==============================================

# 平台和编译器特定的编译选项
if(MSVC)
    # Visual Studio编译器
    add_compile_options(
        /W4                 # 警告级别4
        /WX-                # 警告不作为错误处理
        /permissive-        # 禁用非标准扩展
        /utf-8              # 源文件和执行字符集都使用UTF-8
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # GCC或Clang编译器
    add_compile_options(
        -Wall               # 启用大部分警告
        -Wextra             # 启用额外警告
        -Wpedantic          # 严格遵循标准
        -Wformat=2          # 格式化字符串检查
        -Wunused            # 未使用变量/函数警告
        -Wshadow            # 变量遮蔽警告
    )
    
    # Release模式优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
    
    # Debug模式配置
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DDEBUG)
    endif()
endif()

# ==============================================
# 包含目录
# ==============================================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ==============================================
# 源文件收集
# ==============================================

# 定义各模块源文件
set(DNS_CACHE_SOURCES
    src/DNScache/relayBuild.c
    src/DNScache/free_stack.c
)

set(WEBSOCKET_SOURCES
    src/websocket/datagram.c
    src/websocket/dnsServer.c
    src/websocket/upstream_config.c
    src/websocket/websocket.c
)

set(DEBUG_SOURCES
    src/debug/debug.c
)

set(PLATFORM_SOURCES
    src/platform/platform.c
)

set(THREAD_SOURCES
    src/Thread/thread_pool.c
)

set(IDMAPPING_SOURCES
    src/idmapping/idmapping.c
)

set(MAIN_SOURCE
    src/main.c
)

# 组合所有源文件
set(ALL_SOURCES
    ${MAIN_SOURCE}
    ${DNS_CACHE_SOURCES}
    ${WEBSOCKET_SOURCES}
    ${DEBUG_SOURCES}
    ${PLATFORM_SOURCES}
    ${THREAD_SOURCES}
    ${IDMAPPING_SOURCES}
)

# ==============================================
# 可执行文件创建
# ==============================================
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# ==============================================
# 链接库配置
# ==============================================

# 平台特定链接库
if(WIN32)
    # Windows平台
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ws2_32      # Winsock2库
        iphlpapi    # IP助手API
    )
elseif(UNIX)
    # Unix/Linux平台
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        pthread     # POSIX线程库
        m           # 数学库
    )
    
    # 如果是Linux，可能需要额外的库
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${PROJECT_NAME} PRIVATE rt)
    endif()
endif()

# ==============================================
# 构建后处理
# ==============================================

# 复制配置文件到输出目录
configure_file(
    ${CMAKE_SOURCE_DIR}/dnsrelay.txt
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dnsrelay.txt
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/upstream_dns.conf
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/upstream_dns.conf
    COPYONLY
)

# ==============================================
# 安装配置（可选）
# ==============================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

install(FILES 
    dnsrelay.txt
    upstream_dns.conf
    DESTINATION bin
    COMPONENT Runtime
)

# ==============================================
# 构建信息输出
# ==============================================
message(STATUS "==============================================")
message(STATUS "DNS Relay Server Build Configuration")
message(STATUS "==============================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "==============================================")

# ==============================================
# 项目特性总结
# ==============================================
message(STATUS "Project Features:")
message(STATUS "  ✓ Multi-threaded DNS relay server")
message(STATUS "  ✓ LRU cache mechanism")
message(STATUS "  ✓ Domain blocking and filtering")
message(STATUS "  ✓ Upstream DNS server pool")
message(STATUS "  ✓ Command line parameter support")
message(STATUS "  ✓ Cross-platform compatibility")
message(STATUS "  ✓ High-performance concurrent processing")
message(STATUS "==============================================")