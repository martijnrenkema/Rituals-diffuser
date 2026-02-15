#include "logger.h"
#include "config.h"
#include <time.h>
#include <stdarg.h>

#ifdef PLATFORM_ESP8266
    #include <LittleFS.h>
    #define FILESYSTEM LittleFS
#else
    #include <SPIFFS.h>
    #define FILESYSTEM SPIFFS
#endif

Logger logger;

// Log retention: 7 days in seconds
#define LOG_RETENTION_SECONDS (7 * 24 * 60 * 60)

// Save interval: 60 seconds (reduce flash wear)
#define LOG_SAVE_INTERVAL_MS 60000

// File header for version checking
struct LogFileHeader {
    uint32_t magic;      // 0x4C4F4731 = "LOG1"
    uint16_t count;      // Number of entries
    uint16_t head;       // Head position
};
#define LOG_FILE_MAGIC 0x4C4F4731

void Logger::begin() {
    _head = 0;
    _count = 0;
    _dirty = false;
    _lastSave = 0;

    // Initialize filesystem
#ifdef PLATFORM_ESP8266
    if (!LittleFS.begin()) {
        Serial.println("[LOGGER] LittleFS mount failed");
    }
#else
    if (!SPIFFS.begin(true)) {
        Serial.println("[LOGGER] SPIFFS mount failed");
    }
#endif

    // Load existing logs from file
    loadFromFile();

    info("Logger initialized");
}

void Logger::loadFromFile() {
    File file = FILESYSTEM.open(LOG_FILE_PATH, "r");
    if (!file) {
        Serial.println("[LOGGER] No log file found, starting fresh");
        return;
    }

    // Read header
    LogFileHeader header;
    if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        Serial.println("[LOGGER] Invalid log file header");
        file.close();
        return;
    }

    // Verify magic
    if (header.magic != LOG_FILE_MAGIC) {
        Serial.println("[LOGGER] Log file magic mismatch, starting fresh");
        file.close();
        return;
    }

    // Read entries
    uint16_t validCount = min(header.count, (uint16_t)MAX_LOG_ENTRIES);
    uint16_t loaded = 0;

    for (uint16_t i = 0; i < validCount; i++) {
        LogEntry entry;
        if (file.read((uint8_t*)&entry, sizeof(entry)) != sizeof(entry)) {
            break;
        }

        // Check retention - skip entries older than 7 days
        time_t now = time(nullptr);
        if (now > 1000000000 && entry.epochTime > 0) {
            if ((now - entry.epochTime) > LOG_RETENTION_SECONDS) {
                continue;  // Skip old entry
            }
        }

        _entries[loaded] = entry;
        loaded++;
    }

    file.close();

    _count = loaded;
    _head = loaded % MAX_LOG_ENTRIES;

    Serial.printf("[LOGGER] Loaded %d logs from file\n", loaded);
}

void Logger::saveToFile() {
    File file = FILESYSTEM.open(LOG_FILE_PATH, "w");
    if (!file) {
        Serial.println("[LOGGER] Failed to open log file for writing");
        return;
    }

    // Write header
    LogFileHeader header;
    header.magic = LOG_FILE_MAGIC;
    header.count = _count;
    header.head = _head;
    file.write((uint8_t*)&header, sizeof(header));

    // Write entries in order (oldest first)
    for (uint16_t i = 0; i < _count; i++) {
        const LogEntry* entry = getEntry(i);
        if (entry) {
            file.write((uint8_t*)entry, sizeof(LogEntry));
        }
    }

    file.close();
    _dirty = false;
    _lastSave = millis();

    Serial.printf("[LOGGER] Saved %d logs to file\n", _count);
}

void Logger::save() {
    // Save if urgent, or if dirty and enough time has passed
    if (_dirty && (_urgentSave || (millis() - _lastSave >= LOG_SAVE_INTERVAL_MS))) {
        saveToFile();
        _urgentSave = false;
    }
}

bool Logger::needsUrgentSave() const {
    return _urgentSave;
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
    char buffer[LOG_MESSAGE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    info(buffer);
}

void Logger::warnf(const char* format, ...) {
    char buffer[LOG_MESSAGE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    warn(buffer);
}

void Logger::errorf(const char* format, ...) {
    char buffer[LOG_MESSAGE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    error(buffer);
}

void Logger::addEntry(LogLevel level, const char* message) {
    LogEntry& entry = _entries[_head];

    entry.uptimeMs = millis();
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

    _dirty = true;

    // Mark for urgent save on important events (but don't block here)
    // The main loop will call save() which checks _urgentSave
    if (level == LogLevel::ERROR || level == LogLevel::WARN) {
        _urgentSave = true;
    }
}

uint16_t Logger::getCount() {
    return _count;
}

const LogEntry* Logger::getEntry(uint16_t index) {
    if (index >= _count) return nullptr;

    // Calculate actual position in circular buffer
    // If buffer is full, oldest entry is at _head
    // If buffer is not full, oldest entry is at 0
    uint16_t pos;
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
    _dirty = false;

    // Delete log file
    FILESYSTEM.remove(LOG_FILE_PATH);

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
    // Pre-allocate to avoid heap fragmentation from repeated concatenation
    // Each entry: ~80 bytes JSON overhead + message (max 64 chars, worst-case 2x with escaping) = ~210 bytes max
    // Reserve enough for all entries plus array brackets
    String json;
    json.reserve(_count * 220 + 2);

    json = "[";

    for (uint16_t i = 0; i < _count; i++) {
        const LogEntry* entry = getEntry(i);
        if (!entry) continue;

        if (i > 0) json += ',';

        // Build entry JSON - use char for single characters to avoid String temporaries
        json += "{\"u\":";
        json += String(entry->uptimeMs);
        json += ",\"e\":";
        json += String((unsigned long)entry->epochTime);
        json += ",\"l\":\"";
        json += levelToString(entry->level);
        json += "\",\"m\":\"";

        // Escape quotes and backslashes in message
        for (const char* p = entry->message; *p; p++) {
            char c = *p;
            if (c == '"') json += "\\\"";
            else if (c == '\\') json += "\\\\";
            else if (c == '\n') json += "\\n";
            else if (c == '\r') json += "\\r";
            else json += c;  // Single char, not string
        }

        json += "\"}";
    }

    json += ']';
    return json;
}
