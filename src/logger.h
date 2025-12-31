#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Log levels
enum class LogLevel {
    INFO,
    WARN,
    ERROR
};

// Single log entry
struct LogEntry {
    unsigned long timestamp;    // millis() when logged
    time_t epochTime;          // Unix timestamp (0 if NTP not synced)
    LogLevel level;
    char message[80];          // Truncated to save memory
};

// Maximum number of log entries to keep in memory
// Each entry is ~90 bytes, so 50 entries = ~4.5KB RAM
#define MAX_LOG_ENTRIES 50

class Logger {
public:
    void begin();

    // Log methods
    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);

    // Printf-style logging
    void infof(const char* format, ...);
    void warnf(const char* format, ...);
    void errorf(const char* format, ...);

    // Get logs
    uint8_t getCount();
    const LogEntry* getEntry(uint8_t index);  // 0 = oldest

    // Clear all logs
    void clear();

    // Get JSON representation of all logs
    String toJson();

private:
    LogEntry _entries[MAX_LOG_ENTRIES];
    uint8_t _head = 0;      // Next write position
    uint8_t _count = 0;     // Number of entries

    void addEntry(LogLevel level, const char* message);
    const char* levelToString(LogLevel level);
};

// Global instance
extern Logger logger;

#endif // LOGGER_H
