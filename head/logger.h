#pragma once
// logger.h — 专业轻量日志系统, 完美解决Windows乱码与多线程冲突

#ifdef _WIN32
#include <windows.h>
#define localtime_r(timer, result) localtime_s(result, timer)
#endif

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex> // 引入互斥锁保证线程安全

enum LogLevel { LOG_LV_DEBUG = 0, LOG_LV_INFO = 1, LOG_LV_WARN = 2, LOG_LV_ERROR = 3 };

// 默认只显示报错
inline LogLevel g_log_level = LOG_LV_ERROR;  
inline std::mutex g_log_mutex; // 全局日志锁

inline void log_write(LogLevel lv, const char* file, int line, const char* fmt, ...) {
    static const char* tags[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    // ANSI 终端颜色代码
    static const char* colors[] = {
        "\033[36m", // DEBUG - 青色
        "\033[32m", // INFO  - 绿色
        "\033[33m", // WARN  - 黄色
        "\033[31m"  // ERROR - 红色
    };
    static const char* reset = "\033[0m";

    time_t now = time(nullptr);
    struct tm tb;
#ifdef _WIN32
    localtime_s(&tb, &now);
#else
    localtime_r(&now, &tb);
#endif

    // 👉 核心改进 1：加锁，防止多线程同时写日志导致字符错乱
    std::lock_guard<std::mutex> lock(g_log_mutex); 

    // 输出带颜色的头部
    fprintf(stderr, "%s[%04d-%02d-%02d %02d:%02d:%02d][%s][%s:%d]%s ",
            colors[lv],
            tb.tm_year + 1900, tb.tm_mon + 1, tb.tm_mday,
            tb.tm_hour, tb.tm_min, tb.tm_sec, tags[lv], file, line, reset);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

// 👉 核心改进 2：初始化时解决乱码与颜色支持
inline void LogInit(LogLevel level = LOG_LV_INFO) {
    g_log_level = level;

#ifdef _WIN32
    // 1. 解决中文乱码：强制设置控制台输出为 UTF-8
    SetConsoleOutputCP(65001);

    // 2. 开启 Windows 10/11 的终端颜色支持
    HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

// 👉 核心改进 3：宏优化。级别不够时，直接不执行后续代码，不占 CPU 压栈耗时
#define LOG_D(fmt, ...) do { if (LOG_LV_DEBUG >= g_log_level) log_write(LOG_LV_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define LOG_I(fmt, ...) do { if (LOG_LV_INFO >= g_log_level)  log_write(LOG_LV_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define LOG_W(fmt, ...) do { if (LOG_LV_WARN >= g_log_level)  log_write(LOG_LV_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define LOG_E(fmt, ...) do { if (LOG_LV_ERROR >= g_log_level) log_write(LOG_LV_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)