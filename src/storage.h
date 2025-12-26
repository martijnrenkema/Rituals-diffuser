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
    bool intervalEnabled;
    uint8_t intervalOnTime;
    uint8_t intervalOffTime;
};

#define SETTINGS_MAGIC 0xD1FF0001  // Magic number for valid settings

class Storage {
public:
    void begin();

    // Load all settings
    DiffuserSettings load();

    // Save all settings
    void save(const DiffuserSettings& settings);

    // Individual setters
    void setWiFi(const char* ssid, const char* password);
    void setMQTT(const char* host, uint16_t port, const char* user, const char* password);
    void setDeviceName(const char* name);
    void setFanSpeed(uint8_t speed);
    void setIntervalMode(bool enabled, uint8_t onTime, uint8_t offTime);

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
