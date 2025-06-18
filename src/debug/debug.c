#include "debug/debug.h"

// 初始化全局日志级别
LogLevel current_log_level = LOG_LEVEL_INFO;

// 将日志级别枚举转换为字符串
const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default:              return "UNKNOWN";
    }
}

// 设置当前日志级别
void set_log_level(LogLevel level) {
    current_log_level = level;
}

