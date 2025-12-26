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

void Storage::setOTAPassword(const char* password) {
    strlcpy(_settings.otaPassword, password, sizeof(_settings.otaPassword));
    commit();
    Serial.println("[STORAGE] OTA password saved");
}

void Storage::setAPPassword(const char* password) {
    strlcpy(_settings.apPassword, password, sizeof(_settings.apPassword));
    commit();
    Serial.println("[STORAGE] AP password saved");
}

const char* Storage::getOTAPassword() {
    if (strlen(_settings.otaPassword) > 0) {
        return _settings.otaPassword;
    }
    return OTA_PASSWORD;  // Default from config.h
}

const char* Storage::getAPPassword() {
    if (strlen(_settings.apPassword) > 0) {
        return _settings.apPassword;
    }
    return WIFI_AP_PASSWORD;  // Default from config.h
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
    // Night mode defaults
    if (settings.nightModeStart == 0 && settings.nightModeEnd == 0) {
        settings.nightModeStart = 22;  // 10 PM
        settings.nightModeEnd = 7;     // 7 AM
        settings.nightModeBrightness = 10;  // 10% brightness
    }
}

// RFID Configuration
void Storage::setRFIDPins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst) {
    _settings.rfidConfigured = true;
    _settings.rfidPinSCK = sck;
    _settings.rfidPinMISO = miso;
    _settings.rfidPinMOSI = mosi;
    _settings.rfidPinSS = ss;
    _settings.rfidPinRST = rst;
    commit();
    Serial.printf("[STORAGE] RFID pins saved: SCK=%d MISO=%d MOSI=%d SS=%d RST=%d\n", sck, miso, mosi, ss, rst);
}

void Storage::clearRFIDConfig() {
    _settings.rfidConfigured = false;
    _settings.rfidPinSCK = 0;
    _settings.rfidPinMISO = 0;
    _settings.rfidPinMOSI = 0;
    _settings.rfidPinSS = 0;
    _settings.rfidPinRST = 0;
    memset(_settings.currentCartridge, 0, sizeof(_settings.currentCartridge));
    commit();
    Serial.println("[STORAGE] RFID config cleared");
}

bool Storage::isRFIDConfigured() {
    return _settings.rfidConfigured;
}

void Storage::setCurrentCartridge(const char* name) {
    strlcpy(_settings.currentCartridge, name, sizeof(_settings.currentCartridge));
    // Reset cartridge runtime when new cartridge detected
    _settings.cartridgeRuntimeMinutes = 0;
    commit();
    Serial.printf("[STORAGE] Cartridge set: %s\n", name);
}

const char* Storage::getCurrentCartridge() {
    return _settings.currentCartridge;
}

// Usage Statistics
void Storage::addRuntimeMinutes(uint32_t minutes) {
    _settings.totalRuntimeMinutes += minutes;
    _settings.cartridgeRuntimeMinutes += minutes;
    commit();
}

void Storage::resetCartridgeRuntime() {
    _settings.cartridgeRuntimeMinutes = 0;
    commit();
    Serial.println("[STORAGE] Cartridge runtime reset");
}

uint32_t Storage::getTotalRuntimeMinutes() {
    return _settings.totalRuntimeMinutes;
}

uint32_t Storage::getCartridgeRuntimeMinutes() {
    return _settings.cartridgeRuntimeMinutes;
}

// Night Mode
void Storage::setNightMode(bool enabled, uint8_t startHour, uint8_t endHour, uint8_t brightness) {
    _settings.nightModeEnabled = enabled;
    _settings.nightModeStart = startHour;
    _settings.nightModeEnd = endHour;
    _settings.nightModeBrightness = brightness;
    commit();
    Serial.printf("[STORAGE] Night mode: %s (%02d:00-%02d:00, %d%% brightness)\n",
                  enabled ? "ON" : "OFF", startHour, endHour, brightness);
}

bool Storage::isNightModeEnabled() {
    return _settings.nightModeEnabled;
}

bool Storage::isNightModeActive(uint8_t currentHour) {
    if (!_settings.nightModeEnabled) return false;

    uint8_t start = _settings.nightModeStart;
    uint8_t end = _settings.nightModeEnd;

    // Handle overnight range (e.g., 22:00 - 07:00)
    if (start > end) {
        return (currentHour >= start || currentHour < end);
    }
    // Handle same-day range (e.g., 13:00 - 18:00)
    return (currentHour >= start && currentHour < end);
}

uint8_t Storage::getNightModeBrightness() {
    return _settings.nightModeBrightness;
}
