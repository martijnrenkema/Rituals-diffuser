#include "logger.h"
#include <time.h>
#include <stdarg.h>

Logger logger;

void Logger::begin() {
    _head = 0;
    _count = 0;
    info("Logger initialized");
}

void Logger::info(const char* message) {
    addEntry(LogLevel::INFO, message);
}

void Logger::warn(const char* message) {
    addEntry(LogLevel::WARN, message);
}

void Logger::error(const char* message) {
    addEntry(LogLevel::ERROR, message);
}

void Logger::infof(const char* format, ...) {
    char buffer[80];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    info(buffer);
}

void Logger::warnf(const char* format, ...) {
    char buffer[80];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    warn(buffer);
}

void Logger::errorf(const char* format, ...) {
    char buffer[80];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    error(buffer);
}

void Logger::addEntry(LogLevel level, const char* message) {
    LogEntry& entry = _entries[_head];

    entry.timestamp = millis();
    entry.level = level;

    // Get epoch time if NTP is synced
    time_t now = time(nullptr);
    entry.epochTime = (now > 1000000000) ? now : 0;

    // Copy message, truncate if needed
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    // Also print to Serial for debugging
    Serial.printf("[LOG][%s] %s\n", levelToString(level), message);

    // Advance head (circular buffer)
    _head = (_head + 1) % MAX_LOG_ENTRIES;
    if (_count < MAX_LOG_ENTRIES) {
        _count++;
    }
}

uint8_t Logger::getCount() {
    return _count;
}

const LogEntry* Logger::getEntry(uint8_t index) {
    if (index >= _count) return nullptr;

    // Calculate actual position in circular buffer
    // If buffer is full, oldest entry is at _head
    // If buffer is not full, oldest entry is at 0
    uint8_t pos;
    if (_count < MAX_LOG_ENTRIES) {
        pos = index;
    } else {
        pos = (_head + index) % MAX_LOG_ENTRIES;
    }

    return &_entries[pos];
}

void Logger::clear() {
    _head = 0;
    _count = 0;
    info("Log cleared");
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "???";
    }
}

String Logger::toJson() {
    String json = "[";

    for (uint8_t i = 0; i < _count; i++) {
        const LogEntry* entry = getEntry(i);
        if (!entry) continue;

        if (i > 0) json += ",";

        json += "{\"t\":";
        json += String(entry->timestamp);
        json += ",\"e\":";
        json += String(entry->epochTime);
        json += ",\"l\":\"";
        json += levelToString(entry->level);
        json += "\",\"m\":\"";

        // Escape quotes and backslashes in message
        for (const char* p = entry->message; *p; p++) {
            if (*p == '"') json += "\\\"";
            else if (*p == '\\') json += "\\\\";
            else if (*p == '\n') json += "\\n";
            else if (*p == '\r') json += "\\r";
            else json += *p;
        }

        json += "\"}";
    }

    json += "]";
    return json;
}
