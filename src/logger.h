#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#define LOG_BUFFER_SIZE 50  // Keep last 50 log entries

enum class LogLevel {
    INFO,
    WARNING,
    ERROR,
    DEBUG
};

struct LogEntry {
    unsigned long timestamp;
    LogLevel level;
    char message[128];
};

class Logger {
public:
    void begin();
    void log(LogLevel level, const char* format, ...);
    void info(const char* format, ...);
    void warning(const char* format, ...);
    void error(const char* format, ...);
    void debug(const char* format, ...);

    // Get logs for API
    int getLogCount();
    const LogEntry& getLog(int index);
    void clearLogs();

private:
    LogEntry _buffer[LOG_BUFFER_SIZE];
    int _head = 0;
    int _count = 0;

    void addLog(LogLevel level, const char* message);
    const char* getLevelString(LogLevel level);
};

extern Logger logger;

#endif // LOGGER_H
