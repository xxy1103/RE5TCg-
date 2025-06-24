cmake_minimum_required(VERSION 3.10)

# 项目信息
project(my_DNS 
    VERSION 1.0.0
    DESCRIPTION "DNS Server Project with TCP Socket Programming"
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
    # 发布版本优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # 对于GCC编译器
    add_compile_options(-Wall -Wextra -pedantic)
    # 发布版本O3优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
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
    # 在Windows上，链接Winsock2库
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 创建输出目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 打印一些有用的信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")