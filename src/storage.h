#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

// Settings structure - stored in EEPROM/NVS
struct DiffuserSettings {
    uint32_t magic;  // Magic number to verify valid data

    // WiFi
    char wifiSsid[64];
    char wifiPassword[64];

    // MQTT
    char mqttHost[64];
    uint16_t mqttPort;
    char mqttUser[32];
    char mqttPassword[64];

    // Device
    char deviceName[32];

    // Fan
    uint8_t fanSpeed;
    uint8_t fanMinPWM;  // Calibrated minimum PWM for fan to start
    bool intervalEnabled;
    uint8_t intervalOnTime;
    uint8_t intervalOffTime;

    // Security - configurable passwords
    char otaPassword[32];
    char apPassword[32];

    // Usage Statistics
    uint32_t totalRuntimeMinutes;     // Total fan runtime ever
    uint32_t sessionStartTime;        // Timestamp when session started

    // Night Mode
    bool nightModeEnabled;
    uint8_t nightModeStart;  // Hour (0-23)
    uint8_t nightModeEnd;    // Hour (0-23)
    uint8_t nightModeBrightness;  // LED brightness during night (0-100)
};

#define SETTINGS_MAGIC 0xD1FF0004  // Magic number for valid settings (incremented for new fields)

class Storage {
public:
    void begin();

    // Load all settings (from NVS - use sparingly)
    DiffuserSettings load();

    // Get cached settings (fast, no NVS read)
    const DiffuserSettings& getSettings() { return _settings; }

    // Save all settings
    void save(const DiffuserSettings& settings);

    // Individual setters
    void setWiFi(const char* ssid, const char* password);
    void setMQTT(const char* host, uint16_t port, const char* user, const char* password);
    void setDeviceName(const char* name);
    void setFanSpeed(uint8_t speed);
    void setFanMinPWM(uint8_t minPWM);
    uint8_t getFanMinPWM();
    void setIntervalMode(bool enabled, uint8_t onTime, uint8_t offTime);
    void setOTAPassword(const char* password);
    void setAPPassword(const char* password);

    // Password getters
    const char* getOTAPassword();
    const char* getAPPassword();

    // Usage statistics
    void addRuntimeMinutes(uint32_t minutes);
    uint32_t getTotalRuntimeMinutes();

    // Night mode
    void setNightMode(bool enabled, uint8_t startHour, uint8_t endHour, uint8_t brightness);
    bool isNightModeEnabled();
    bool isNightModeActive(uint8_t currentHour);
    uint8_t getNightModeBrightness();

    // Check if WiFi is configured
    bool hasWiFiCredentials();

    // Check if MQTT is configured
    bool hasMQTTConfig();

    // Factory reset
    void reset();

private:
    DiffuserSettings _settings;
    bool _loaded = false;

    void ensureDefaults(DiffuserSettings& settings);
    void commit();
};

extern Storage storage;

#endif // STORAGE_H
