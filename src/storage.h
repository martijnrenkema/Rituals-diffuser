#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

// Settings structure - stored in EEPROM/NVS
// ESP8266: Use smaller buffers to save ~100 bytes RAM
struct DiffuserSettings {
    uint32_t magic;  // Magic number to verify valid data

    // WiFi (SSID max 32, password max 63)
#ifdef PLATFORM_ESP8266
    char wifiSsid[33];         // Max SSID + null
    char wifiPassword[48];     // Most passwords are < 48 chars
#else
    char wifiSsid[64];
    char wifiPassword[64];
#endif

    // MQTT
#ifdef PLATFORM_ESP8266
    char mqttHost[48];         // Most hostnames < 48 chars
    uint16_t mqttPort;
    char mqttUser[24];         // Most usernames < 24 chars
    char mqttPassword[48];
#else
    char mqttHost[64];
    uint16_t mqttPort;
    char mqttUser[32];
    char mqttPassword[64];
#endif

    // Device
#ifdef PLATFORM_ESP8266
    char deviceName[24];       // Shorter device names
#else
    char deviceName[32];
#endif

    // Fan
    uint8_t fanSpeed;
    uint8_t fanMinPWM;  // Calibrated minimum PWM for fan to start
    bool intervalEnabled;
    uint8_t intervalOnTime;
    uint8_t intervalOffTime;

    // Security - configurable passwords
#ifdef PLATFORM_ESP8266
    char otaPassword[20];      // OTA passwords typically short
    char apPassword[20];       // AP passwords typically short
#else
    char otaPassword[32];
    char apPassword[32];
#endif

    // Usage Statistics
    uint32_t totalRuntimeMinutes;     // Total fan runtime ever
    // Note: sessionStartTime removed in v5 - managed by fan_controller locally

    // Night Mode
    bool nightModeEnabled;
    uint8_t nightModeStart;  // Hour (0-23)
    uint8_t nightModeEnd;    // Hour (0-23)
    uint8_t nightModeBrightness;  // LED brightness during night (0-100)

    // Update Checker (v6)
    char lastKnownVersion[16];    // Last version seen from GitHub
    bool updateAvailable;          // Cached update availability
};

// Magic number for valid settings validation
// Format: 0xD1FF00XX where XX is the version number
// Increment version when struct layout changes to invalidate old settings
// v1 (0x01): Initial version
// v2 (0x02): Added interval mode fields
// v3 (0x03): Added OTA/AP passwords
// v4 (0x04): Added night mode and runtime stats
// v5 (0x05): Removed unused sessionStartTime field
// v6 (0x06): Added update checker fields
// v7 (0x07): ESP8266 smaller buffer sizes for RAM optimization
#ifdef PLATFORM_ESP8266
#define SETTINGS_MAGIC 0xD1FF0007
#else
#define SETTINGS_MAGIC 0xD1FF0006
#endif

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
