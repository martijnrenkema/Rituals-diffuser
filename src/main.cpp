#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "web_server.h"
#include "fan_controller.h"
#include "led_controller.h"
#include "ota_handler.h"
#include "rfid_handler.h"

#ifdef PLATFORM_ESP8266
#include "button_handler.h"
#endif

// Global settings
DiffuserSettings settings;

// Time sync
bool timeConfigured = false;
unsigned long lastNightModeCheck = 0;

// Configure NTP time sync
void setupTimeSync() {
    // Configure time for Europe/Amsterdam timezone (CET/CEST)
    configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
    Serial.println("[TIME] NTP sync configured");
    timeConfigured = true;
}

// Get current hour (0-23), returns 255 if time not available
uint8_t getCurrentHour() {
    if (!timeConfigured) return 255;

    time_t now = time(nullptr);
    if (now < 1000000000) return 255;  // Time not yet synced

    struct tm* timeinfo = localtime(&now);
    return timeinfo->tm_hour;
}

// Check and apply night mode
void checkNightMode() {
    if (!storage.isNightModeEnabled()) return;

    uint8_t hour = getCurrentHour();
    if (hour == 255) return;  // Time not available

    bool isNight = storage.isNightModeActive(hour);
    static bool wasNight = false;

    if (isNight != wasNight) {
        if (isNight) {
            ledController.setBrightness(storage.getNightModeBrightness());
            Serial.printf("[MAIN] Night mode activated (hour=%d)\n", hour);
        } else {
            ledController.setBrightness(100);
            Serial.printf("[MAIN] Night mode deactivated (hour=%d)\n", hour);
        }
        wasNight = isNight;
    }
}

// RFID cartridge change handler
void onCartridgeChange(const char* name) {
    Serial.printf("[MAIN] New cartridge: %s\n", name);
    mqttHandler.publishState();
}

// WiFi state change handler
void onWiFiStateChange(WifiStatus state) {
    switch (state) {
        case WifiStatus::CONNECTING:
            ledController.showConnecting();
            break;
        case WifiStatus::CONNECTED:
            if (fanController.isOn()) {
                ledController.showFanRunning();
            } else {
                ledController.showConnected();
            }
            // Start OTA when connected
            otaHandler.begin();
            // Setup NTP time sync
            setupTimeSync();
            // Initialize RFID after WiFi (needs SPI)
            rfidHandler.begin();
            break;
        case WifiStatus::AP_MODE:
            ledController.showAPMode();
            break;
        case WifiStatus::DISCONNECTED:
            ledController.showError();
            break;
    }
}

// Fan state change handler
void onFanStateChange(bool on, uint8_t speed) {
    if (wifiManager.isConnected() || wifiManager.isAPMode()) {
        if (on) {
            ledController.showFanRunning();
            if (fanController.isTimerActive()) {
                ledController.setMode(LedMode::PULSE);
            }
        } else {
            if (wifiManager.isConnected()) {
                ledController.showConnected();
            } else {
                ledController.showAPMode();
            }
        }
    }

    // Publish state to MQTT
    mqttHandler.publishState();

    // Save speed to storage
    storage.setFanSpeed(speed);
}

// OTA handlers
void onOTAStart() {
    ledController.showOTA();
    fanController.turnOff();
}

void onOTAEnd() {
    ledController.off();
}

#ifdef PLATFORM_ESP8266
// Button handlers for Rituals Genie
void onFrontButton(ButtonEvent event) {
    if (event == ButtonEvent::SHORT_PRESS) {
        // Toggle fan on/off
        if (fanController.isOn()) {
            fanController.turnOff();
        } else {
            fanController.turnOn();
        }
    } else if (event == ButtonEvent::LONG_PRESS) {
        // Factory reset - blink red and reset
        Serial.println("[MAIN] Factory reset triggered!");
        ledController.showError();
        delay(2000);
        storage.reset();
        ESP.restart();
    }
}

void onRearButton(ButtonEvent event) {
    if (event == ButtonEvent::SHORT_PRESS) {
        // Cycle through speed presets: 25% -> 50% -> 75% -> 100% -> 25%
        uint8_t currentSpeed = fanController.getSpeed();
        uint8_t newSpeed;

        if (currentSpeed < 25) newSpeed = 25;
        else if (currentSpeed < 50) newSpeed = 50;
        else if (currentSpeed < 75) newSpeed = 75;
        else if (currentSpeed < 100) newSpeed = 100;
        else newSpeed = 25;

        fanController.setSpeed(newSpeed);
        if (!fanController.isOn()) {
            fanController.turnOn();
        }
        Serial.printf("[MAIN] Speed set to %d%%\n", newSpeed);
    } else if (event == ButtonEvent::LONG_PRESS) {
        // Toggle interval mode
        bool newState = !fanController.isIntervalMode();
        fanController.setIntervalMode(newState);
        Serial.printf("[MAIN] Interval mode: %s\n", newState ? "ON" : "OFF");
    }
}
#endif

void setup() {
    // Initialize serial
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    Serial.println();
    Serial.println("=================================");
    Serial.println("  Rituals Perfume Genie 2.0");
    Serial.println("  Custom Firmware v1.1.0");
    Serial.println("=================================");
    Serial.println();

    // Initialize components
    storage.begin();
    settings = storage.load();

    ledController.begin();
    ledController.showConnecting();

    fanController.begin();
    fanController.onStateChange(onFanStateChange);

    // Apply saved settings
    fanController.setSpeed(settings.fanSpeed);
    fanController.setIntervalMode(settings.intervalEnabled);
    fanController.setIntervalTimes(settings.intervalOnTime, settings.intervalOffTime);

#ifdef PLATFORM_ESP8266
    // Initialize buttons (Rituals Genie only)
    buttonHandler.begin();
    buttonHandler.onFrontButton(onFrontButton);
    buttonHandler.onRearButton(onRearButton);
#endif

    // Initialize WiFi
    wifiManager.begin();
    wifiManager.onStateChange(onWiFiStateChange);

    // Check if we have WiFi credentials
    if (storage.hasWiFiCredentials()) {
        Serial.printf("[MAIN] Connecting to saved WiFi: %s\n", settings.wifiSsid);
        wifiManager.connect(settings.wifiSsid, settings.wifiPassword);
    } else {
        Serial.println("[MAIN] No WiFi credentials, starting AP mode");
        wifiManager.startAP();
    }

    // Initialize MQTT
    mqttHandler.begin();
    if (storage.hasMQTTConfig()) {
        Serial.printf("[MAIN] MQTT configured: %s:%d\n", settings.mqttHost, settings.mqttPort);
        mqttHandler.connect(settings.mqttHost, settings.mqttPort,
                           settings.mqttUser, settings.mqttPassword);
    }

    // Initialize web server
    webServer.begin();

    // Setup OTA callbacks
    otaHandler.onStart(onOTAStart);
    otaHandler.onEnd(onOTAEnd);

    // Setup RFID callback
    rfidHandler.onCartridgeChange(onCartridgeChange);

    Serial.println("[MAIN] Setup complete");
    Serial.println();
}

void loop() {
    // Run all component loops
    wifiManager.loop();
    mqttHandler.loop();
    fanController.loop();
    ledController.loop();
    otaHandler.loop();
    rfidHandler.loop();

#ifdef PLATFORM_ESP8266
    buttonHandler.loop();
#endif

    // Check night mode every minute
    unsigned long now = millis();
    if (now - lastNightModeCheck >= 60000) {
        checkNightMode();
        lastNightModeCheck = now;
    }

    // Small delay to prevent watchdog issues
    yield();
}
