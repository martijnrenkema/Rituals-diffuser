#include "storage.h"
#include "config.h"

#ifdef PLATFORM_ESP8266
    #include <EEPROM.h>
#else
    #include <Preferences.h>
    static Preferences prefs;
#endif

Storage storage;

void Storage::begin() {
#ifdef PLATFORM_ESP8266
    EEPROM.begin(sizeof(DiffuserSettings) + 16);  // Extra padding
    Serial.println("[STORAGE] EEPROM initialized");
#else
    prefs.begin(NVS_NAMESPACE, false);
    Serial.println("[STORAGE] NVS initialized");
#endif

    // Load settings on init
    _settings = load();
    _loaded = true;
}

DiffuserSettings Storage::load() {
    DiffuserSettings settings;
    memset(&settings, 0, sizeof(settings));

#ifdef PLATFORM_ESP8266
    EEPROM.get(0, settings);

    // Check magic number
    if (settings.magic != SETTINGS_MAGIC) {
        Serial.println("[STORAGE] No valid settings found, using defaults");
        memset(&settings, 0, sizeof(settings));
        settings.magic = SETTINGS_MAGIC;
    }
#else
    // ESP32: Use Preferences
    String ssid = prefs.getString(NVS_WIFI_SSID, "");
    String pass = prefs.getString(NVS_WIFI_PASS, "");
    strlcpy(settings.wifiSsid, ssid.c_str(), sizeof(settings.wifiSsid));
    strlcpy(settings.wifiPassword, pass.c_str(), sizeof(settings.wifiPassword));

    String mqttHost = prefs.getString(NVS_MQTT_HOST, "");
    settings.mqttPort = prefs.getUShort(NVS_MQTT_PORT, 1883);
    String mqttUser = prefs.getString(NVS_MQTT_USER, "");
    String mqttPass = prefs.getString(NVS_MQTT_PASS, "");
    strlcpy(settings.mqttHost, mqttHost.c_str(), sizeof(settings.mqttHost));
    strlcpy(settings.mqttUser, mqttUser.c_str(), sizeof(settings.mqttUser));
    strlcpy(settings.mqttPassword, mqttPass.c_str(), sizeof(settings.mqttPassword));

    String deviceName = prefs.getString(NVS_DEVICE_NAME, "Rituals Diffuser");
    strlcpy(settings.deviceName, deviceName.c_str(), sizeof(settings.deviceName));

    settings.fanSpeed = prefs.getUChar(NVS_FAN_SPEED, 50);
    settings.intervalEnabled = prefs.getBool(NVS_INTERVAL_ENABLED, false);
    settings.intervalOnTime = prefs.getUChar(NVS_INTERVAL_ON, INTERVAL_ON_DEFAULT);
    settings.intervalOffTime = prefs.getUChar(NVS_INTERVAL_OFF, INTERVAL_OFF_DEFAULT);
#endif

    ensureDefaults(settings);
    Serial.println("[STORAGE] Settings loaded");
    return settings;
}

void Storage::save(const DiffuserSettings& settings) {
    _settings = settings;
    _settings.magic = SETTINGS_MAGIC;

#ifdef PLATFORM_ESP8266
    EEPROM.put(0, _settings);
    EEPROM.commit();
#else
    prefs.putString(NVS_WIFI_SSID, settings.wifiSsid);
    prefs.putString(NVS_WIFI_PASS, settings.wifiPassword);
    prefs.putString(NVS_MQTT_HOST, settings.mqttHost);
    prefs.putUShort(NVS_MQTT_PORT, settings.mqttPort);
    prefs.putString(NVS_MQTT_USER, settings.mqttUser);
    prefs.putString(NVS_MQTT_PASS, settings.mqttPassword);
    prefs.putString(NVS_DEVICE_NAME, settings.deviceName);
    prefs.putUChar(NVS_FAN_SPEED, settings.fanSpeed);
    prefs.putBool(NVS_INTERVAL_ENABLED, settings.intervalEnabled);
    prefs.putUChar(NVS_INTERVAL_ON, settings.intervalOnTime);
    prefs.putUChar(NVS_INTERVAL_OFF, settings.intervalOffTime);
#endif

    Serial.println("[STORAGE] Settings saved");
}

void Storage::commit() {
    save(_settings);
}

void Storage::setWiFi(const char* ssid, const char* password) {
    strlcpy(_settings.wifiSsid, ssid, sizeof(_settings.wifiSsid));
    strlcpy(_settings.wifiPassword, password, sizeof(_settings.wifiPassword));
    commit();
    Serial.println("[STORAGE] WiFi credentials saved");
}

void Storage::setMQTT(const char* host, uint16_t port, const char* user, const char* password) {
    strlcpy(_settings.mqttHost, host, sizeof(_settings.mqttHost));
    _settings.mqttPort = port;
    strlcpy(_settings.mqttUser, user, sizeof(_settings.mqttUser));
    strlcpy(_settings.mqttPassword, password, sizeof(_settings.mqttPassword));
    commit();
    Serial.println("[STORAGE] MQTT config saved");
}

void Storage::setDeviceName(const char* name) {
    strlcpy(_settings.deviceName, name, sizeof(_settings.deviceName));
    commit();
    Serial.println("[STORAGE] Device name saved");
}

void Storage::setFanSpeed(uint8_t speed) {
    _settings.fanSpeed = speed;
    commit();
}

void Storage::setIntervalMode(bool enabled, uint8_t onTime, uint8_t offTime) {
    _settings.intervalEnabled = enabled;
    _settings.intervalOnTime = onTime;
    _settings.intervalOffTime = offTime;
    commit();
    Serial.println("[STORAGE] Interval settings saved");
}

bool Storage::hasWiFiCredentials() {
    return strlen(_settings.wifiSsid) > 0;
}

bool Storage::hasMQTTConfig() {
    return strlen(_settings.mqttHost) > 0;
}

void Storage::reset() {
    memset(&_settings, 0, sizeof(_settings));
    _settings.magic = 0;  // Invalidate magic

#ifdef PLATFORM_ESP8266
    EEPROM.put(0, _settings);
    EEPROM.commit();
#else
    prefs.clear();
#endif

    Serial.println("[STORAGE] Factory reset complete");
}

void Storage::ensureDefaults(DiffuserSettings& settings) {
    if (settings.mqttPort == 0) settings.mqttPort = 1883;
    if (settings.fanSpeed == 0) settings.fanSpeed = 50;
    if (settings.intervalOnTime < INTERVAL_MIN) settings.intervalOnTime = INTERVAL_ON_DEFAULT;
    if (settings.intervalOffTime < INTERVAL_MIN) settings.intervalOffTime = INTERVAL_OFF_DEFAULT;
    if (strlen(settings.deviceName) == 0) {
        strcpy(settings.deviceName, "Rituals Diffuser");
    }
}
