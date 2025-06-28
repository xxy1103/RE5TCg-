#include "debug/debug.h"
#include <stdarg.h>
#include <string.h>

// 初始化全局日志级别
LogLevel current_log_level = LOG_LEVEL_INFO;

// 全局日志文件指针
static FILE* log_file = NULL;
static const char* LOG_FILE_PATH = "log.txt";

/**
 * @brief 将日志级别枚举转换为字符串
 */
const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "error";
        case LOG_LEVEL_WARN:  return "warn";
        case LOG_LEVEL_INFO:  return "info";
        case LOG_LEVEL_DEBUG: return "debug";
        default:              return "unknown";
    }
}

// 设置当前日志级别
void set_log_level(LogLevel level) {
    current_log_level = level;
}

// 初始化日志文件
void init_log_file(void) {  
    if (log_file == NULL) {
        log_file = fopen(LOG_FILE_PATH, "w"); //以写入模式打开日志文件
        if (log_file == NULL) {
            fprintf(stderr, "Error: Cannot open log file %s\n", LOG_FILE_PATH);
        }
    }
}

// 清理日志文件
void cleanup_log_file(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

// 写入日志到文件
void write_log_to_file(LogLevel level, const char* file, int line, const char* format, ...) {
    // 如果日志文件未初始化，先初始化
    if (log_file == NULL) {
        init_log_file();
    }
    
    // 如果文件打开失败，回退到控制台输出
    FILE* output = (log_file != NULL) ? log_file : stdout;
    
    // 获取当前时间
    time_t timer;
    char buffer[26];
    struct tm* tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 提取文件名（去掉路径）
    const char* filename = strrchr(file, '\\');
    if (filename == NULL) {
        filename = strrchr(file, '/');
    }
    filename = (filename != NULL) ? filename + 1 : file;
    
    // 写入时间戳、级别、文件名和行号
    fprintf(output, "[%s] [%s] [%s:%d] ", 
            buffer, log_level_to_string(level), filename, line);
    
    // 写入格式化的消息
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    // 添加换行符
    fprintf(output, "\n");
    
    // 强制刷新缓冲区以确保日志立即写入
    fflush(output);
}