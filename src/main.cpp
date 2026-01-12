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
#include "logger.h"
#include "update_checker.h"
#include "button_handler.h"

#ifdef PLATFORM_ESP8266
#include "sync_ota.h"
#endif

// Global settings
DiffuserSettings settings;

// Time sync
bool timeConfigured = false;
unsigned long lastNightModeCheck = 0;

// OTA state tracking
bool otaInProgress = false;

// Configure NTP time sync
void setupTimeSync() {
    // Configure time for Europe/Amsterdam timezone (CET/CEST)
#ifdef PLATFORM_ESP8266
    // ESP8266: Use setenv for timezone, then configTime
    // TZ string: CET-1CEST,M3.5.0/2,M10.5.0/3
    // - CET is UTC+1, CEST is UTC+2
    // - DST starts last Sunday of March at 02:00
    // - DST ends last Sunday of October at 03:00
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
#else
    // ESP32: Use configTzTime for automatic DST handling
    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
#endif
    Serial.println("[TIME] NTP sync configured (CET/CEST with auto DST)");
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

// Central LED status update - determines LED color based on priority
// Priority (highest first):
// 1. OTA in progress (purple fast blink)
// 2. AP mode (orange pulsing)
// 3. WiFi connecting (cyan blinking)
// 4. WiFi disconnected (red)
// 5. Timer + Interval mode + fan on (blue slow breathing - combined state)
// 6. Timer active + fan on (blue solid)
// 7. Interval mode + fan on (purple solid)
// 8. Fan on (green solid)
// 9. Standby / fan off (LED off)
void updateLedStatus() {
    // 1. OTA has highest priority
    if (otaInProgress) {
        ledController.showOTA();
        return;
    }

    // 2. AP mode
    if (wifiManager.isAPMode()) {
        ledController.showAPMode();
        return;
    }

    // 3. WiFi connecting
    if (wifiManager.getState() == WifiStatus::CONNECTING) {
        ledController.showConnecting();
        return;
    }

    // 4. WiFi disconnected (but not AP mode or connecting)
    if (!wifiManager.isConnected() && !wifiManager.isAPMode()) {
        ledController.showError();
        return;
    }

    // 5-8. Fan states (only when WiFi is connected)
    if (fanController.isOn()) {
        if (fanController.isTimerActive() && fanController.isIntervalMode()) {
            // 5. Timer + Interval mode - blue slow breathing (combined state)
            ledController.setColor(LED_COLOR_BLUE);
            ledController.setMode(LedMode::BREATHE_SLOW);
        } else if (fanController.isTimerActive()) {
            // 6. Timer active only - blue solid
            ledController.setColor(LED_COLOR_BLUE);
            ledController.setMode(LedMode::ON);
        } else if (fanController.isIntervalMode()) {
            // 7. Interval mode only - purple solid
            ledController.showIntervalMode();
        } else {
            // 8. Normal fan on - green solid
            ledController.showFanRunning();
        }
        return;
    }

    // 9. Standby - LED off
    ledController.off();
}

// WiFi state change handler
void onWiFiStateChange(WifiStatus state) {
    if (state == WifiStatus::CONNECTED) {
        // Start OTA when connected
        otaHandler.begin();
        // Setup NTP time sync
        setupTimeSync();
    }
    updateLedStatus();
}

// Fan state change handler
void onFanStateChange(bool on, uint8_t speed) {
    updateLedStatus();

    // Publish state to MQTT
    mqttHandler.publishState();

    // Only save speed if it actually changed (avoid flash wear)
    static uint8_t lastSavedSpeed = 0;
    if (speed != lastSavedSpeed && speed > 0) {
        storage.setFanSpeed(speed);
        lastSavedSpeed = speed;
    }
}

// OTA handlers
void onOTAStart() {
    otaInProgress = true;
    updateLedStatus();
    fanController.turnOff();
    logger.info("OTA update started");
}

void onOTAEnd() {
    otaInProgress = false;
    updateLedStatus();
    logger.info("OTA update completed");
}

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
        // Start AP mode for WiFi configuration
        Serial.println("[MAIN] AP mode triggered by button!");
        logger.info("AP mode triggered by button");
        wifiManager.startAP();
        updateLedStatus();
    }
}

void onRearButton(ButtonEvent event) {
    if (event == ButtonEvent::SHORT_PRESS) {
        // Restart ESP32
        Serial.println("[MAIN] Restart triggered by button");
        logger.info("Restart triggered by button");
        ledController.showError();  // Flash red to indicate restart
        delay(500);
        ESP.restart();
    } else if (event == ButtonEvent::LONG_PRESS) {
        // Factory reset - clear all settings and restart
        Serial.println("[MAIN] Factory reset triggered!");
        logger.warn("Factory reset triggered");
        ledController.showError();
        delay(1000);
        storage.reset();
        ESP.restart();
    }
}

void setup() {
    // Initialize serial
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    Serial.println();
    Serial.println("=================================");
    Serial.println("  Rituals Perfume Genie 2.0");
    Serial.print("  Custom Firmware v");
    Serial.println(FIRMWARE_VERSION);
    Serial.println("=================================");
    Serial.println();

    // Initialize logger first
    logger.begin();
    logger.infof("System startup - v%s", FIRMWARE_VERSION);

    // Initialize components
    storage.begin();  // Loads settings internally
    settings = storage.getSettings();  // Get cached settings (no double load)

    ledController.begin();
    ledController.showError();  // Red during startup

    fanController.begin();
    fanController.onStateChange(onFanStateChange);

    // Apply saved settings
    fanController.setSpeed(settings.fanSpeed);
    fanController.setIntervalMode(settings.intervalEnabled);
    fanController.setIntervalTimes(settings.intervalOnTime, settings.intervalOffTime);

    // Initialize buttons
    buttonHandler.begin();
    buttonHandler.onFrontButton(onFrontButton);
    buttonHandler.onRearButton(onRearButton);

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

    // Initialize update checker (ESP32 only - ESP8266 can't handle this due to RAM)
#ifndef PLATFORM_ESP8266
    updateChecker.begin();
#endif

    // Setup OTA callbacks
    otaHandler.onStart(onOTAStart);
    otaHandler.onEnd(onOTAEnd);

    // Ensure LED shows correct status after all initialization
    // This handles the case where WiFi was already connected via SDK auto-reconnect
    updateLedStatus();

    Serial.println("[MAIN] Setup complete");
    Serial.println();
}

void loop() {
    #ifdef PLATFORM_ESP8266
    // Check if sync OTA mode is requested
    if (requestSyncOTAMode) {
        Serial.println("[OTA-SYNC] *** FLAG DETECTED! ***");
        requestSyncOTAMode = false;
        Serial.printf("[OTA-SYNC] Sync OTA mode requested. Free heap: %u bytes\n", ESP.getFreeHeap());
        // Longer delay to ensure HTTP response is fully sent
        delay(500);
        Serial.println("[OTA-SYNC] Starting sync OTA server...");
        // runSyncOTAServer() will stop AsyncWebServer and MQTT to free memory
        runSyncOTAServer();  // This function never returns (loops until reboot)
    }
    #endif

    // Run all component loops with strategic yields for ESP8266 stability
    wifiManager.loop();
    yield();

    fanController.loop();
    ledController.loop();

    otaHandler.loop();
    buttonHandler.loop();
    webServer.loop();  // Process pending web actions
    yield();

#ifndef PLATFORM_ESP8266
    updateChecker.loop();  // Check for firmware updates (ESP32 only)
#endif

    // Run MQTT loop with extra yield time
    mqttHandler.loop();
    yield();

    // Check for urgent log saves (ERROR/WARN logs need saving)
    if (logger.needsUrgentSave()) {
        logger.save();
    }

    // Periodic tasks every minute
    unsigned long now = millis();
    if (now - lastNightModeCheck >= 60000) {
        checkNightMode();
        lastNightModeCheck = now;

        // Save logs periodically (only writes if dirty)
        logger.save();
    }

    // Give async tasks (WiFi, MQTT, WebServer) enough CPU time
    // This prevents the AsyncTCP watchdog timeout
    // ESP32 needs longer delay than ESP8266
    delay(20);
}
