cmake_minimum_required(VERSION 3.10)

# 项目信息
project(my_DNS 
    VERSION 1.0.0
    DESCRIPTION "Cross-platform DNS Server Project with UDP Socket Programming"
    LANGUAGES C
)

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 设置构建类型（如果没有指定的话）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
if(MSVC)
    # 对于Visual Studio编译器
    add_compile_options(/W4)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # 对于GCC或Clang编译器
    add_compile_options(-Wall -Wextra -pedantic)
    # 在Debug模式下添加调试信息
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g)
    endif()
endif()

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找源文件
file(GLOB_RECURSE SOURCES "src/*.c")

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 平台特定设置
if(WIN32)
    # Windows平台设置
    # 链接Winsock2库
    target_link_libraries(${PROJECT_NAME} ws2_32)
    
    # 如果使用MinGW，可能需要额外的库
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${PROJECT_NAME} wsock32)
    endif()
    
    # 定义Windows宏
    target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0601)
    
elseif(UNIX)
    # Unix/Linux/macOS平台设置
    
    # 检查并链接必要的库
    find_library(PTHREAD_LIB pthread)
    if(PTHREAD_LIB)
        target_link_libraries(${PROJECT_NAME} ${PTHREAD_LIB})
    endif()
    
    # macOS特定设置
    if(APPLE)
        # macOS可能需要特定的网络库
        target_link_libraries(${PROJECT_NAME} "-framework Foundation")
    endif()
    
    # Linux特定设置
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Linux可能需要realtime库
        find_library(RT_LIB rt)
        if(RT_LIB)
            target_link_libraries(${PROJECT_NAME} ${RT_LIB})
        endif()
    endif()
endif()

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 创建输出目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 安装规则（可选）
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# 打印构建信息
message(STATUS "=== 构建配置信息 ===")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "C编译器: ${CMAKE_C_COMPILER_ID}")
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "项目版本: ${PROJECT_VERSION}")
message(STATUS "目标平台: ${CMAKE_SYSTEM_NAME}")
message(STATUS "系统处理器: ${CMAKE_SYSTEM_PROCESSOR}")

# 平台检测提示
if(WIN32)
    message(STATUS "平台: Windows - 将链接Winsock2库")
elseif(APPLE)
    message(STATUS "平台: macOS - 将链接Foundation框架")
elseif(UNIX)
    message(STATUS "平台: Linux/Unix - 将链接pthread库")
endif()

message(STATUS "====================")