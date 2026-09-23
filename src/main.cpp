// Skip main.cpp when building RC522 test firmware
#ifndef RC522_TEST_MODE

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
#include "state_lock.h"

#ifdef PLATFORM_ESP8266
#include "sync_ota.h"
#endif

// RFID support for all platforms with RC522_ENABLED
#if defined(RC522_ENABLED)
#include "rfid_handler.h"
#endif

// Time sync
bool timeConfigured = false;
unsigned long lastNightModeCheck = 0;

// OTA state tracking (also written from the AsyncTCP upload handler)
volatile bool otaInProgress = false;

// Rear button held: factory reset warning shown on the LED
bool factoryResetWarning = false;

// Fan speed persistence is debounced to avoid a flash write per slider step
static uint8_t pendingSpeedSave = 0;
static unsigned long speedChangedAt = 0;

// Write settings that are still waiting for their debounce delay.
// Call before any intentional restart.
void flushPendingSettings() {
    if (pendingSpeedSave > 0) {
        storage.setFanSpeed(pendingSpeedSave);
        pendingSpeedSave = 0;
    }
}

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

// Check and apply night mode.
// Set force=true after settings are saved so brightness changes take effect
// immediately even when the day/night state itself hasn't transitioned.
void checkNightMode(bool force) {
    static bool wasNight = false;
    static bool initialized = false;

    if (!storage.isNightModeEnabled()) {
        // Disabled: restore full brightness if we were previously dimming
        if (force || (initialized && wasNight)) {
            ledController.setBrightness(LED_DEFAULT_BRIGHTNESS);
            wasNight = false;
            initialized = true;
        }
        return;
    }

    uint8_t hour = getCurrentHour();
    if (hour == 255) return;  // Time not available

    bool isNight = storage.isNightModeActive(hour);

    if (force || !initialized || isNight != wasNight) {
        initialized = true;
        if (isNight) {
            ledController.setBrightness(storage.getNightModeBrightness());
            Serial.printf("[MAIN] Night mode brightness applied (hour=%d, %d%%)\n",
                          hour, storage.getNightModeBrightness());
        } else {
            ledController.setBrightness(LED_DEFAULT_BRIGHTNESS);
            Serial.printf("[MAIN] Night mode inactive, day brightness (hour=%d)\n", hour);
        }
        wasNight = isNight;
    }
}

// Central LED status update - determines LED color based on priority
// Priority (highest first):
// 0. Factory reset warning (red slow blink while rear button is held)
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
    // 0. Rear button held - warn before factory reset
    if (factoryResetWarning) {
        ledController.showResetWarning();
        return;
    }

    // 1. OTA
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

    // Request a state publish instead of calling publishState() directly.
    // The callback can fire from AsyncTCP (HTTP route) on ESP32, and
    // requestStatePublish() is the synchronisation entry point used by every
    // other caller.
    mqttHandler.requestStatePublish();

    // Persist speed once it has been stable for FAN_SPEED_SAVE_DELAY_MS
    // (dragging the web slider would otherwise write flash on every step)
    if (speed == 0) return;
    if (speed == storage.getSettings().fanSpeed) {
        pendingSpeedSave = 0;
    } else {
        pendingSpeedSave = speed;
        speedChangedAt = millis();
    }
}

// Show a red fast blink for a moment before a restart/reset.
// Blocking is fine here: the device is about to reboot anyway.
static void blinkBeforeRestart(unsigned long durationMs) {
    ledController.showError();
    unsigned long start = millis();
    while (millis() - start < durationMs) {
        ledController.loop();
        delay(10);
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

// Rear button:
//   short press (<1s)       -> restart
//   hold 1s                 -> LED blinks red slowly (factory reset warning)
//   release before 5s       -> cancel, nothing happens
//   hold 5s                 -> LED blinks red fast (confirmation), factory reset
void onRearButton(ButtonEvent event) {
    if (event == ButtonEvent::SHORT_PRESS) {
        Serial.println("[MAIN] Restart triggered by button");
        logger.info("Restart triggered by button");
        logger.save();
        flushPendingSettings();
        blinkBeforeRestart(500);
        ESP.restart();
    } else if (event == ButtonEvent::HOLD_WARNING) {
        Serial.println("[MAIN] Rear button held - release to cancel factory reset");
        factoryResetWarning = true;
        updateLedStatus();
    } else if (event == ButtonEvent::HOLD_CANCELLED) {
        Serial.println("[MAIN] Factory reset cancelled");
        factoryResetWarning = false;
        updateLedStatus();
    } else if (event == ButtonEvent::LONG_PRESS) {
        Serial.println("[MAIN] Factory reset triggered!");
        logger.warn("Factory reset triggered");
        logger.save();
        factoryResetWarning = false;
        blinkBeforeRestart(1500);
        storage.reset();
        ESP.restart();
    }
}

void setup() {
    // Initialize serial
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    // Must exist before the web server starts handling requests (ESP32)
    stateLockInit();

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
    // Reference the cached settings instead of a global copy (saves ~470B RAM)
    const DiffuserSettings& settings = storage.getSettings();

    ledController.begin();
    ledController.showError();  // Red during startup

    fanController.begin();
    fanController.onStateChange(onFanStateChange);

    // Apply saved settings
    fanController.setSpeed(settings.fanSpeed);
    fanController.setIntervalTimes(settings.intervalOnTime, settings.intervalOffTime);
    fanController.setIntervalMode(settings.intervalEnabled);

    // Log saved settings for debugging
    Serial.printf("[MAIN] Saved settings: speed=%d%%, interval=%s (%ds on, %ds off)\n",
                  settings.fanSpeed,
                  settings.intervalEnabled ? "ON" : "OFF",
                  settings.intervalOnTime,
                  settings.intervalOffTime);

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

    // Initialize update checker
    // ESP8266: auto-checks once 15s after boot (when heap is still available)
    // ESP32: auto-checks 2min after boot, then every 24h
    updateChecker.begin();

    // Setup OTA callbacks
    otaHandler.onStart(onOTAStart);
    otaHandler.onEnd(onOTAEnd);

    // Initialize RFID (all platforms with RC522_ENABLED)
#if defined(RC522_ENABLED)
    if (rfidInit()) {
        Serial.println("[MAIN] RFID reader initialized");
    } else {
        Serial.println("[MAIN] RFID reader NOT detected - check wiring");
    }
#endif

    // Handle case where WiFi was already connected via SDK auto-reconnect
    // (e.g., after OTA update or restart). The callback wasn't registered yet
    // during wifiManager.begin(), so OTA and NTP would never be initialized.
    if (wifiManager.isConnected()) {
        Serial.println("[MAIN] WiFi already connected, initializing OTA and NTP");
        otaHandler.begin();
        setupTimeSync();
    }

    // Ensure LED shows correct status after all initialization
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

    // Run all component loops with strategic yields for ESP8266 stability.
    // Components that share state with HTTP handlers run under the state lock
    // (ESP32: handlers run in the AsyncTCP task; ESP8266: no-op).
    {
        StateLock lock;
        wifiManager.loop();
        yield();

        fanController.loop();
        ledController.loop();

        otaHandler.loop();
        buttonHandler.loop();
        webServer.loop();  // Process pending web actions
    }
    yield();

    // Not locked: blocks for seconds during HTTPS and only touches its own state
    updateChecker.loop();  // Check for firmware updates (heap-guarded on ESP8266)

    // Locks internally around command handling (connect attempts may block ~3s)
    mqttHandler.loop();
    yield();

    unsigned long now = millis();
    {
        StateLock lock;

        // Check for urgent log saves (ERROR/WARN logs need saving).
        // Never write the filesystem while an OTA upload may be replacing it.
        if (!otaInProgress && logger.needsUrgentSave()) {
            logger.save();
        }

        // RFID loop (all platforms with RC522_ENABLED)
#if defined(RC522_ENABLED)
        rfidLoop();
#endif

        // Debounced fan speed persistence
        if (pendingSpeedSave > 0 && now - speedChangedAt >= FAN_SPEED_SAVE_DELAY_MS) {
            flushPendingSettings();
        }

        // Periodic tasks every minute
        if (now - lastNightModeCheck >= 60000) {
            checkNightMode(false);
            lastNightModeCheck = now;

            // Save logs periodically (only writes if dirty)
            if (!otaInProgress) {
                logger.save();
            }
        }
    }

#ifdef PLATFORM_ESP8266
    // Log heap every 60 seconds for OOM debugging
    static unsigned long lastHeapLog = 0;
    if (now - lastHeapLog > 60000) {
        Serial.printf("[HEAP] Free: %u bytes\n", ESP.getFreeHeap());
        lastHeapLog = now;
    }
#endif

    // Give async tasks (WiFi, MQTT, WebServer) enough CPU time
    // This prevents the AsyncTCP watchdog timeout
    // ESP32 needs longer delay than ESP8266
    delay(20);
}

#endif // RC522_TEST_MODE
