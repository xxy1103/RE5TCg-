#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

// 定义调试级别
typedef enum {
    LOG_LEVEL_NONE,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} LogLevel;

// --- 配置 ---
// 设置要编译的最高日志级别。
// 高于此级别的日志将被编译掉。
#ifndef MAX_LOG_LEVEL
#define MAX_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// 全局日志级别变量
// 可以在运行时更改以控制详细程度
extern LogLevel current_log_level;

// --- 日志宏 ---

// 核心日志宏
// 使用 do-while(0) 循环使宏成为单个语句
#define LOG(level, format, ...) \
    do { \
        if (level <= MAX_LOG_LEVEL && level <= current_log_level && level != LOG_LEVEL_NONE) { \
            write_log_to_file(level, __FILE__, __LINE__, format, ##__VA_ARGS__); \
        } \
    } while (0)

// --- 用于将枚举转换为字符串的辅助函数 ---
const char* log_level_to_string(LogLevel level);
void set_log_level(LogLevel level);

// --- 文件日志相关函数 ---
void init_log_file(void);
void cleanup_log_file(void);
void write_log_to_file(LogLevel level, const char* file, int line, const char* format, ...);


// --- 每个级别的便捷宏 ---
#define log_error(format, ...) LOG(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  LOG(LOG_LEVEL_WARN,  format, ##__VA_ARGS__)
#define log_info(format, ...)  LOG(LOG_LEVEL_INFO,  format, ##__VA_ARGS__)
#define log_debug(format, ...) LOG(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)

#endif // DEBUG_H

