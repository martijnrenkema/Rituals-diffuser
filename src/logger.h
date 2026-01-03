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
    time_t epochTime;          // Unix timestamp (0 if NTP not synced)
    unsigned long uptimeMs;    // millis() at log time (for relative time if no NTP)
    LogLevel level;
    char message[80];          // Truncated to save memory
};

// Maximum number of log entries to keep
// Entries are stored in SPIFFS for persistence across reboots
#define MAX_LOG_ENTRIES 100

// Log file path
#define LOG_FILE_PATH "/logs.bin"

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
    uint16_t getCount();
    const LogEntry* getEntry(uint16_t index);  // 0 = oldest

    // Clear all logs
    void clear();

    // Get JSON representation of all logs
    String toJson();

    // Save logs to SPIFFS (called periodically)
    void save();

private:
    LogEntry _entries[MAX_LOG_ENTRIES];
    uint16_t _head = 0;      // Next write position
    uint16_t _count = 0;     // Number of entries
    bool _dirty = false;     // True if logs changed since last save
    unsigned long _lastSave = 0;

    void addEntry(LogLevel level, const char* message);
    const char* levelToString(LogLevel level);
    void loadFromFile();
    void saveToFile();
};

// Global instance
extern Logger logger;

#endif // LOGGER_H
