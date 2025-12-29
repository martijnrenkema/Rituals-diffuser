#include "logger.h"
#include <stdarg.h>

Logger logger;

void Logger::begin() {
    _head = 0;
    _count = 0;
    Serial.println("[LOGGER] Initialized");
    info("System started");
}

void Logger::log(LogLevel level, const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    addLog(level, buffer);

    // Also print to Serial with timestamp and level
    Serial.printf("[%lu][%s] %s\n", millis(), getLevelString(level), buffer);
}

void Logger::info(const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LogLevel::INFO, "%s", buffer);
}

void Logger::warning(const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LogLevel::WARNING, "%s", buffer);
}

void Logger::error(const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LogLevel::ERROR, "%s", buffer);
}

void Logger::debug(const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(LogLevel::DEBUG, "%s", buffer);
}

void Logger::addLog(LogLevel level, const char* message) {
    // Add to circular buffer
    _buffer[_head].timestamp = millis();
    _buffer[_head].level = level;
    strlcpy(_buffer[_head].message, message, sizeof(_buffer[_head].message));

    _head = (_head + 1) % LOG_BUFFER_SIZE;
    if (_count < LOG_BUFFER_SIZE) {
        _count++;
    }
}

int Logger::getLogCount() {
    return _count;
}

const LogEntry& Logger::getLog(int index) {
    // Return logs in chronological order (oldest first)
    int bufferIndex;
    if (_count < LOG_BUFFER_SIZE) {
        bufferIndex = index;
    } else {
        bufferIndex = (_head + index) % LOG_BUFFER_SIZE;
    }
    return _buffer[bufferIndex];
}

void Logger::clearLogs() {
    _head = 0;
    _count = 0;
    Serial.println("[LOGGER] Logs cleared");
}

const char* Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::DEBUG:   return "DEBUG";
        default:                return "UNKNOWN";
    }
}
