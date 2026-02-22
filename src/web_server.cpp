#include "web_server.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "fan_controller.h"
#include "led_controller.h"
#include "button_handler.h"
#include "mqtt_handler.h"
#include "update_checker.h"
#include "logger.h"
#include <ArduinoJson.h>

// RFID support for all platforms with RC522_ENABLED
#if defined(RC522_ENABLED)
#include "rfid_handler.h"
#endif

// External function and flag from main.cpp for LED priority system
extern void updateLedStatus();
extern bool otaInProgress;

#ifdef PLATFORM_ESP8266
// External flag from sync_ota.cpp
extern volatile bool requestSyncOTAMode;
#endif

// Function to stop the async web server (called from sync_ota.cpp)
void stopAsyncWebServer() {
    webServer.stop();
}

#ifdef PLATFORM_ESP8266
    #include <LittleFS.h>
    #include <Updater.h>
    // Use LittleFS on ESP8266 (same as logger.cpp to avoid mounting two filesystems)
    #define FILESYSTEM LittleFS
    // ESP8266 Arduino Core 3.x: getErrorString() returns a String
    #define UPDATE_ERROR_STRING() Update.getErrorString().c_str()
    // Linker symbols for filesystem size
    extern "C" uint32_t _FS_start;
    extern "C" uint32_t _FS_end;
#else
    #include <SPIFFS.h>
    #include <Update.h>
    #define FILESYSTEM SPIFFS
    #define UPDATE_ERROR_STRING() Update.errorString()
#endif

// Track upload progress
static size_t updateContentLength = 0;
static bool updateIsFS = false;

WebServer webServer;

void WebServer::begin() {
    if (_server != nullptr) {
        return;
    }

    // Initialize filesystem (LittleFS on ESP8266, SPIFFS on ESP32)
#ifdef PLATFORM_ESP8266
    if (!FILESYSTEM.begin()) {
#else
    if (!FILESYSTEM.begin(true)) {
#endif
        Serial.println("[WEB] Filesystem mount failed");
    }

    _server = new AsyncWebServer(WEBSERVER_PORT);
    if (_server == nullptr) {
        Serial.println("[WEB] ERROR: Failed to allocate AsyncWebServer!");
        return;
    }

    setupRoutes();
    _server->begin();

    Serial.println("[WEB] Server started on port 80");
}

void WebServer::stop() {
    if (_server != nullptr) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
}

void WebServer::loop() {
    // Process deferred actions from async callbacks
    // This prevents blocking the network stack in callbacks

    if (_pendingActionTime == 0) return;

    // Wait for HTTP response to be sent (500ms is enough for TCP ACK)
    if (millis() - _pendingActionTime < 500) return;

    // Process all pending actions, then clear the timestamp
    // This prevents race condition where multiple actions are queued
    bool actionProcessed = false;

    if (_pendingWifiConnect) {
        _pendingWifiConnect = false;
        actionProcessed = true;
        wifiManager.connect(_pendingWifiSsid, _pendingWifiPassword);
        if (_settingsCallback) _settingsCallback();
    }

    if (_pendingMqttConnect) {
        _pendingMqttConnect = false;
        actionProcessed = true;
        mqttHandler.disconnect();
        mqttHandler.connect(_pendingMqttHost, _pendingMqttPort,
                           _pendingMqttUser, _pendingMqttPassword);
        if (_settingsCallback) _settingsCallback();
    }

    if (_pendingReset) {
        _pendingReset = false;
        actionProcessed = true;
        storage.reset();
        ESP.restart();
    }

    if (_pendingRestart) {
        _pendingRestart = false;
        actionProcessed = true;
        ESP.restart();
    }

    if (_pendingUpdateCheck) {
        _pendingUpdateCheck = false;
        actionProcessed = true;
        updateChecker.checkForUpdates();
    }

    #ifndef PLATFORM_ESP8266
    if (_pendingOTAUpdate) {
        _pendingOTAUpdate = false;
        actionProcessed = true;
        updateChecker.startOTAUpdate();
    }
    #endif

    // Only clear timestamp after all pending actions processed
    if (actionProcessed) {
        _pendingActionTime = 0;
    }
}

void WebServer::onSettingsChanged(SettingsCallback callback) {
    _settingsCallback = callback;
}

void WebServer::setupRoutes() {
    // Serve static files from filesystem
    _server->serveStatic("/", FILESYSTEM, "/").setDefaultFile("index.html");

    // API endpoints
    _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStatus(request);
    });

    // Lite status endpoint for polling - uses stack allocation to reduce heap pressure on ESP8266
    _server->on("/api/status/lite", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStatusLite(request);
    });

    _server->on("/api/wifi", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSaveWifi(request);
    });

    _server->on("/api/mqtt", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSaveMqtt(request);
    });

    _server->on("/api/fan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleFanControl(request);
    });

    _server->on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleReset(request);
    });

    _server->on("/api/passwords", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSavePasswords(request);
    });

    _server->on("/api/passwords", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetPasswords(request);
    });

    _server->on("/api/night", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetNightMode(request);
    });

    _server->on("/api/night", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSaveNightMode(request);
    });

    // System logs
    _server->on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", logger.toJson());
    });

    _server->on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        logger.clear();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Logs cleared\"}");
    });

    // Hardware diagnostics
    _server->on("/api/diagnostic", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleDiagnostic(request);
    });

    _server->on("/api/diagnostic/led", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleDiagnosticLed(request);
    });

    _server->on("/api/diagnostic/fan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleDiagnosticFan(request);
    });

    _server->on("/api/diagnostic/buttons", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleDiagnosticButtons(request);
    });

    // Device settings
    _server->on("/api/device", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (request->hasParam("name", true)) {
            String name = request->getParam("name", true)->value();
            if (name.length() > 0 && name.length() < 32) {
                storage.setDeviceName(name.c_str());
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Device name saved\"}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Name must be 1-31 characters\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing name parameter\"}");
        }
    });

    // Update checker endpoints
    _server->on("/api/update/check", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleUpdateCheck(request);
    });

    _server->on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleUpdateStatus(request);
    });

    #ifndef PLATFORM_ESP8266
    _server->on("/api/update/install", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleStartUpdate(request);
    });
    #endif

    #ifdef PLATFORM_ESP8266
    // ESP8266: Prepare for sync OTA mode (stops async server, starts sync server)
    _server->on("/api/ota/prepare", HTTP_POST, [this](AsyncWebServerRequest* request) {
        Serial.println("[OTA] Preparing for sync OTA mode...");
        Serial.printf("[OTA] Flag BEFORE: %d\n", requestSyncOTAMode ? 1 : 0);

        // Set flag BEFORE sending response - main loop will handle the actual switch
        requestSyncOTAMode = true;

        Serial.printf("[OTA] Flag AFTER: %d\n", requestSyncOTAMode ? 1 : 0);
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Switching to OTA mode...\"}");
    });
    #endif

    // OTA Update - Firmware
    _server->on("/api/update/firmware", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            // Upload complete handler
            bool success = !Update.hasError();
            AsyncWebServerResponse* response = request->beginResponse(
                success ? 200 : 500,
                "text/plain",
                success ? "OK" : "Update failed"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                // Schedule restart in loop() to avoid blocking async callback
                _pendingRestart = true;
                _pendingActionTime = millis();
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            // Upload data handler
            if (!index) {
                Serial.printf("[OTA] Firmware update start: %s\n", filename.c_str());
                otaInProgress = true;
                updateLedStatus();
                updateContentLength = request->contentLength();

                // Stop non-essential services to free memory
                mqttHandler.disconnect();

                #ifdef PLATFORM_ESP8266
                if (!Update.begin(updateContentLength, U_FLASH)) {
                #else
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                #endif
                    Serial.printf("[OTA] Update.begin failed: %s\n", UPDATE_ERROR_STRING());
                    Update.printError(Serial);
                    return;
                }
                Serial.println("[OTA] Update.begin success");
            }

            if (Update.hasError()) {
                return;  // Skip writing if already failed
            }

            if (len) {
                if (Update.write(data, len) != len) {
                    Serial.printf("[OTA] Update.write failed: %s\n", UPDATE_ERROR_STRING());
                    return;
                }
                // Feed watchdog to prevent timeout on large uploads
                // NOTE: Do NOT call yield() here - on ESP8266 the AsyncWebServer upload
                // handler runs in system context where yield() causes a panic crash
                #ifdef PLATFORM_ESP8266
                ESP.wdtFeed();  // Explicitly feed software watchdog on ESP8266
                #endif
            }

            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Firmware update success: %u bytes\n", index + len);
                } else {
                    Serial.printf("[OTA] Firmware update failed: %s\n", UPDATE_ERROR_STRING());
                    Update.printError(Serial);
                    // Reset OTA flag on failure so LED returns to normal
                    otaInProgress = false;
                    updateLedStatus();
                }
            }
        }
    );

    // OTA Update - Filesystem
    _server->on("/api/update/filesystem", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse* response = request->beginResponse(
                success ? 200 : 500,
                "text/plain",
                success ? "OK" : "Update failed"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                // Schedule restart in loop() to avoid blocking async callback
                _pendingRestart = true;
                _pendingActionTime = millis();
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[OTA] Filesystem update start: %s\n", filename.c_str());
                otaInProgress = true;
                updateLedStatus();

                // Stop non-essential services to free memory
                mqttHandler.disconnect();

                #ifdef PLATFORM_ESP8266
                size_t fsSize = ((size_t)&_FS_end - (size_t)&_FS_start);
                if (!Update.begin(fsSize, U_FS)) {
                #else
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                #endif
                    Serial.printf("[OTA] Update.begin failed: %s\n", UPDATE_ERROR_STRING());
                    Update.printError(Serial);
                    return;
                }
                Serial.println("[OTA] Update.begin success");
            }

            if (Update.hasError()) {
                return;  // Skip writing if already failed
            }

            if (len) {
                if (Update.write(data, len) != len) {
                    Serial.printf("[OTA] Update.write failed: %s\n", UPDATE_ERROR_STRING());
                    return;
                }
                // Feed watchdog to prevent timeout on large uploads
                // NOTE: Do NOT call yield() here - on ESP8266 the AsyncWebServer upload
                // handler runs in system context where yield() causes a panic crash
                #ifdef PLATFORM_ESP8266
                ESP.wdtFeed();  // Explicitly feed software watchdog on ESP8266
                #endif
            }

            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Filesystem update success: %u bytes\n", index + len);
                } else {
                    Serial.printf("[OTA] Filesystem update failed: %s\n", UPDATE_ERROR_STRING());
                    Update.printError(Serial);
                    // Reset OTA flag on failure so LED returns to normal
                    otaInProgress = false;
                    updateLedStatus();
                }
            }
        }
    );

    // Captive portal detection endpoints
    // Use PROGMEM strings to save RAM on ESP8266
    static const char PROGMEM captiveSuccess[] = "<html><body>Success</body></html>";

    // Android requests /generate_204 - returning 204 signals "no internet" which triggers portal popup
    _server->on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    // Some Android variants
    _server->on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });
    // iOS/macOS captive portal detection
    _server->on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", captiveSuccess);
    });
    _server->on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", captiveSuccess);
    });
    // Windows captive portal detection
    _server->on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", F("Microsoft Connect Test"));
    });
    _server->on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", F("Microsoft NCSI"));
    });
    // Firefox captive portal detection
    _server->on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", captiveSuccess);
    });
    _server->on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", F("success"));
    });

    // Captive portal redirect - only in AP mode, redirect to config page
    _server->onNotFound([](AsyncWebServerRequest* request) {
        // Only redirect GET requests in AP mode for captive portal
        if (request->method() == HTTP_GET && wifiManager.isAPMode()) {
            // Don't redirect if already requesting root (prevents infinite loop if index.html missing)
            String url = request->url();
            if (url == "/" || url == "/index.html") {
                // Filesystem probably missing - send error page
                request->send(200, "text/html",
                    "<html><body style='font-family:sans-serif;text-align:center;padding:50px;'>"
                    "<h1>Rituals Diffuser</h1>"
                    "<p>Web interface files missing!</p>"
                    "<p>Please flash <b>spiffs_esp8266.bin</b> to the device.</p>"
                    "</body></html>");
            } else {
                request->redirect("http://192.168.4.1/");
            }
        } else {
            request->send(404);
        }
    });
}

void WebServer::handleStatus(AsyncWebServerRequest* request) {
#ifdef PLATFORM_ESP8266
    // Protect against OOM during response generation
    if (ESP.getFreeHeap() < 8000) {
        request->send(503, "application/json", "{\"error\":\"Low memory, please retry\"}");
        return;
    }
#endif

    const DiffuserSettings& settings = storage.getSettings();  // Use cache, no NVS read

    // Use DynamicJsonDocument to avoid stack overflow on ESP8266 (limited 4KB stack)
    // Size increased to 1408 to include RFID data + update release_url/error
    DynamicJsonDocument doc(1408);  // Heap allocation, includes update + RFID info

    // WiFi status
    doc["wifi"]["connected"] = wifiManager.isConnected();
    doc["wifi"]["ap_mode"] = wifiManager.isAPMode();
    doc["wifi"]["ssid"] = wifiManager.getSSID();
    doc["wifi"]["ip"] = wifiManager.getIP();
    doc["wifi"]["rssi"] = wifiManager.getRSSI();

    // MQTT status
    doc["mqtt"]["connected"] = mqttHandler.isConnected();
    doc["mqtt"]["host"] = settings.mqttHost;
    doc["mqtt"]["port"] = settings.mqttPort;

    // Fan status
    doc["fan"]["on"] = fanController.isOn();
    doc["fan"]["speed"] = fanController.getSpeed();
    doc["fan"]["rpm"] = fanController.getRPM();
    doc["fan"]["timer_active"] = fanController.isTimerActive();
    doc["fan"]["remaining_minutes"] = fanController.getRemainingMinutes();
    doc["fan"]["interval_mode"] = fanController.isIntervalMode();
    doc["fan"]["interval_on"] = fanController.getIntervalOnTime();
    doc["fan"]["interval_off"] = fanController.getIntervalOffTime();

    // Device info
    doc["device"]["name"] = settings.deviceName;
    doc["device"]["mac"] = wifiManager.getMacAddress();
    doc["device"]["version"] = FIRMWARE_VERSION;
    #ifdef PLATFORM_ESP8266
    doc["device"]["platform"] = "ESP8266";
    #else
    doc["device"]["platform"] = "ESP32";
    #endif

    // Statistics
    doc["stats"]["total_runtime"] = storage.getTotalRuntimeMinutes() / 60.0;  // hours
    doc["stats"]["session_runtime"] = fanController.getSessionRuntimeMinutes();  // minutes

    // Night mode
    doc["night"]["enabled"] = settings.nightModeEnabled;
    doc["night"]["start"] = settings.nightModeStart;
    doc["night"]["end"] = settings.nightModeEnd;
    doc["night"]["brightness"] = settings.nightModeBrightness;

    // Update info
    doc["update"]["available"] = updateChecker.isUpdateAvailable();
    doc["update"]["current"] = updateChecker.getCurrentVersion();
    doc["update"]["latest"] = updateChecker.getLatestVersion();
    doc["update"]["release_url"] = updateChecker.getReleaseUrl();
    doc["update"]["state"] = (int)updateChecker.getState();
    doc["update"]["progress"] = updateChecker.getDownloadProgress();
    doc["update"]["error"] = updateChecker.getErrorMessage();
    #ifndef PLATFORM_ESP8266
    doc["update"]["can_auto_update"] = true;
    #else
    doc["update"]["can_auto_update"] = false;
    #endif

    // RFID status
    #if defined(RC522_ENABLED)
    doc["rfid"]["connected"] = rfidIsConnected();
    doc["rfid"]["has_tag"] = rfidHasTag();
    doc["rfid"]["cartridge_present"] = rfidIsCartridgePresent();  // Is cartridge NOW present?
    doc["rfid"]["last_uid"] = rfidGetLastUID();
    doc["rfid"]["last_scent"] = rfidGetLastScent();
    doc["rfid"]["time_since_tag"] = rfidTimeSinceLastTag();
    // Debug info: version register (0x91/0x92/0x88 = valid, 0x00/0xFF = no communication)
    char versionHex[5];
    snprintf(versionHex, sizeof(versionHex), "0x%02X", rfidGetVersionReg());
    doc["rfid"]["version_reg"] = versionHex;
    #endif

    String response;
    size_t jsonSize = serializeJson(doc, response);
    if (jsonSize == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }

    request->send(200, "application/json", response);
}

void WebServer::handleStatusLite(AsyncWebServerRequest* request) {
    // Lite status endpoint for frequent polling - uses StaticJsonDocument on STACK
    // to avoid heap allocation and fragmentation on ESP8266
    // Contains only data needed for UI polling updates
    StaticJsonDocument<384> doc;

    // Fan status (essential for UI updates)
    doc["fan"]["on"] = fanController.isOn();
    doc["fan"]["speed"] = fanController.getSpeed();
    doc["fan"]["rpm"] = fanController.getRPM();
    doc["fan"]["timer_active"] = fanController.isTimerActive();
    doc["fan"]["remaining_minutes"] = fanController.getRemainingMinutes();
    doc["fan"]["interval_mode"] = fanController.isIntervalMode();
    doc["fan"]["interval_on"] = fanController.getIntervalOnTime();
    doc["fan"]["interval_off"] = fanController.getIntervalOffTime();

    // Connectivity status (for status dots)
    doc["wifi"]["connected"] = wifiManager.isConnected();
    doc["wifi"]["ap_mode"] = wifiManager.isAPMode();
    doc["mqtt"]["connected"] = mqttHandler.isConnected();

    // RFID status (only if enabled)
    #if defined(RC522_ENABLED)
    doc["rfid"]["connected"] = rfidIsConnected();
    doc["rfid"]["cartridge_present"] = rfidIsCartridgePresent();
    doc["rfid"]["last_scent"] = rfidGetLastScent();
    #endif

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleSaveWifi(AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    const String& ssid = request->getParam("ssid", true)->value();
    const String& password = request->getParam("password", true)->value();

    // Validate credential lengths (WiFi standards: SSID max 32, WPA password 8-63)
    if (ssid.length() == 0 || ssid.length() > 32) {
        request->send(400, "application/json", "{\"error\":\"SSID must be 1-32 characters\"}");
        return;
    }
    if (password.length() > 0 && (password.length() < 8 || password.length() > 63)) {
        request->send(400, "application/json", "{\"error\":\"Password must be 8-63 characters (or empty for open network)\"}");
        return;
    }

    storage.setWiFi(ssid.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi saved, connecting...\"}");

    // Schedule WiFi connect in loop() to avoid blocking async callback
    // Use strlcpy for safe copy to fixed-size char arrays
    strlcpy(_pendingWifiSsid, ssid.c_str(), sizeof(_pendingWifiSsid));
    strlcpy(_pendingWifiPassword, password.c_str(), sizeof(_pendingWifiPassword));
    _pendingWifiConnect = true;
    _pendingActionTime = millis();
}

void WebServer::handleSaveMqtt(AsyncWebServerRequest* request) {
    if (!request->hasParam("host", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing host parameter\"}");
        return;
    }

    const String& host = request->getParam("host", true)->value();

    // Validate host length
    if (host.length() == 0 || host.length() > 64) {
        request->send(400, "application/json", "{\"error\":\"Host must be 1-64 characters\"}");
        return;
    }

    uint16_t port = 1883;

    if (request->hasParam("port", true)) {
        int portVal = request->getParam("port", true)->value().toInt();
        // Validate MQTT port range (1-65535)
        if (portVal > 0 && portVal <= 65535) {
            port = portVal;
        } else if (request->getParam("port", true)->value().length() > 0) {
            request->send(400, "application/json", "{\"error\":\"Port must be 1-65535\"}");
            return;
        }
    }

    // Get user and password if provided, with length validation
    String userStr = "";
    String passwordStr = "";
    if (request->hasParam("user", true)) {
        userStr = request->getParam("user", true)->value();
        if (userStr.length() > 32) {
            request->send(400, "application/json", "{\"error\":\"Username must be max 32 characters\"}");
            return;
        }
    }
    if (request->hasParam("password", true)) {
        passwordStr = request->getParam("password", true)->value();
        if (passwordStr.length() > 64) {
            request->send(400, "application/json", "{\"error\":\"Password must be max 64 characters\"}");
            return;
        }
    }

    storage.setMQTT(host.c_str(), port, userStr.c_str(), passwordStr.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"MQTT saved, connecting...\"}");

    // Schedule MQTT reconnect in loop() to avoid blocking async callback
    // Use strlcpy for safe copy to fixed-size char arrays
    strlcpy(_pendingMqttHost, host.c_str(), sizeof(_pendingMqttHost));
    _pendingMqttPort = port;
    strlcpy(_pendingMqttUser, userStr.c_str(), sizeof(_pendingMqttUser));
    strlcpy(_pendingMqttPassword, passwordStr.c_str(), sizeof(_pendingMqttPassword));
    _pendingMqttConnect = true;
    _pendingActionTime = millis();
}

void WebServer::handleFanControl(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> response;
    response["success"] = true;

    if (request->hasParam("power", true)) {
        String power = request->getParam("power", true)->value();
        if (power == "on") {
            fanController.turnOn();
        } else if (power == "off") {
            fanController.turnOff();
        }
        // Ignore invalid power values silently (backwards compatible)
    }

    if (request->hasParam("speed", true)) {
        const String& speedStr = request->getParam("speed", true)->value();
        // Validate: must be numeric and 0-100
        bool validSpeed = true;
        for (unsigned int i = 0; i < speedStr.length(); i++) {
            if (!isDigit(speedStr[i])) {
                validSpeed = false;
                break;
            }
        }
        if (validSpeed && speedStr.length() > 0) {
            int speed = speedStr.toInt();
            if (speed >= 0 && speed <= 100) {
                fanController.setSpeed(speed);
                storage.setFanSpeed(speed);
            }
        }
        // Ignore invalid speed values silently (backwards compatible)
    }

    if (request->hasParam("timer", true)) {
        const String& timerStr = request->getParam("timer", true)->value();
        // Validate: must be numeric
        bool validTimer = true;
        for (unsigned int i = 0; i < timerStr.length(); i++) {
            if (!isDigit(timerStr[i])) {
                validTimer = false;
                break;
            }
        }
        if (validTimer && timerStr.length() > 0) {
            int timer = timerStr.toInt();
            if (timer > 0 && timer <= 1440) {  // Max 24 hours
                fanController.setTimer(timer);
            } else if (timer == 0) {
                fanController.cancelTimer();
            }
        }
        updateLedStatus();
    }

    if (request->hasParam("interval", true)) {
        bool interval = request->getParam("interval", true)->value() == "true";
        fanController.setIntervalMode(interval);
        // Save interval state immediately
        storage.setIntervalMode(interval, fanController.getIntervalOnTime(), fanController.getIntervalOffTime());
        updateLedStatus();
    }

    if (request->hasParam("interval_on", true) && request->hasParam("interval_off", true)) {
        const String& onStr = request->getParam("interval_on", true)->value();
        const String& offStr = request->getParam("interval_off", true)->value();
        // Validate: must be numeric
        bool validOn = onStr.length() > 0;
        bool validOff = offStr.length() > 0;
        for (unsigned int i = 0; i < onStr.length() && validOn; i++) {
            if (!isDigit(onStr[i])) validOn = false;
        }
        for (unsigned int i = 0; i < offStr.length() && validOff; i++) {
            if (!isDigit(offStr[i])) validOff = false;
        }
        if (validOn && validOff) {
            int onTime = onStr.toInt();
            int offTime = offStr.toInt();
            // FanController::setIntervalTimes already constrains to INTERVAL_MIN/MAX
            fanController.setIntervalTimes(onTime, offTime);
            storage.setIntervalMode(fanController.isIntervalMode(), onTime, offTime);
        }
    }

    // Return current state
    response["fan"]["on"] = fanController.isOn();
    response["fan"]["speed"] = fanController.getSpeed();
    response["fan"]["timer_active"] = fanController.isTimerActive();
    response["fan"]["remaining_minutes"] = fanController.getRemainingMinutes();

    String output;
    if (serializeJson(response, output) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", output);

    // Request MQTT state publish (non-blocking)
    mqttHandler.requestStatePublish();
}

void WebServer::handleReset(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Resetting...\"}");

    // Schedule reset in loop() to avoid blocking async callback
    _pendingReset = true;
    _pendingActionTime = millis();
}

void WebServer::handleSavePasswords(AsyncWebServerRequest* request) {
    bool changed = false;

    if (request->hasParam("ota_password", true)) {
        String otaPass = request->getParam("ota_password", true)->value();
        if (otaPass.length() >= 8) {
            storage.setOTAPassword(otaPass.c_str());
            changed = true;
        } else if (otaPass.length() > 0) {
            request->send(400, "application/json", "{\"error\":\"OTA password must be at least 8 characters\"}");
            return;
        }
    }

    if (request->hasParam("ap_password", true)) {
        String apPass = request->getParam("ap_password", true)->value();
        if (apPass.length() >= 8) {
            storage.setAPPassword(apPass.c_str());
            changed = true;
        } else if (apPass.length() > 0) {
            request->send(400, "application/json", "{\"error\":\"AP password must be at least 8 characters\"}");
            return;
        }
    }

    if (changed) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Passwords saved. Restart device to apply.\"}");
    } else {
        request->send(400, "application/json", "{\"error\":\"No valid passwords provided\"}");
    }
}

void WebServer::handleGetPasswords(AsyncWebServerRequest* request) {
    StaticJsonDocument<128> doc;
    // Only indicate if custom passwords are set - NEVER expose actual passwords
    doc["ota_custom"] = strlen(storage.getSettings().otaPassword) > 0;
    doc["ap_custom"] = strlen(storage.getSettings().apPassword) > 0;
    // Note: Default passwords are NOT sent to prevent security leaks

    String response;
    if (serializeJson(doc, response) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", response);
}

void WebServer::handleGetNightMode(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    const DiffuserSettings& settings = storage.getSettings();

    doc["enabled"] = settings.nightModeEnabled;
    doc["start"] = settings.nightModeStart;
    doc["end"] = settings.nightModeEnd;
    doc["brightness"] = settings.nightModeBrightness;

    String response;
    if (serializeJson(doc, response) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", response);
}

void WebServer::handleSaveNightMode(AsyncWebServerRequest* request) {
    bool enabled = false;
    uint8_t start = 22;
    uint8_t end = 7;
    uint8_t brightness = 10;

    if (request->hasParam("enabled", true)) {
        enabled = request->getParam("enabled", true)->value() == "true";
    }
    if (request->hasParam("start", true)) {
        int val = request->getParam("start", true)->value().toInt();
        start = constrain(val, 0, 23);  // Valid hour range
    }
    if (request->hasParam("end", true)) {
        int val = request->getParam("end", true)->value().toInt();
        end = constrain(val, 0, 23);  // Valid hour range
    }
    if (request->hasParam("brightness", true)) {
        int val = request->getParam("brightness", true)->value().toInt();
        brightness = constrain(val, 0, 100);  // Valid percentage
    }

    storage.setNightMode(enabled, start, end, brightness);

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Night mode settings saved\"}");
}

// =====================================================
// Hardware Diagnostics
// =====================================================

void WebServer::handleDiagnostic(AsyncWebServerRequest* request) {
    // Use StaticJsonDocument on stack to avoid heap allocation and fragmentation
    // ESP8266 has 4KB stack which can handle 384 bytes
    StaticJsonDocument<384> doc;

    // Fan status - connected if we detect RPM when running
    uint16_t rpm = fanController.getRPM();
    bool fanConnected = (fanController.isOn() && rpm > 0) || !fanController.isOn();
    doc["fan"]["connected"] = fanConnected;
    doc["fan"]["on"] = fanController.isOn();
    doc["fan"]["speed"] = fanController.getSpeed();
    doc["fan"]["rpm"] = rpm;
    doc["fan"]["pwm"] = fanController.getCurrentPWMValue();
    doc["fan"]["invert"] = fanController.isInvertPWM();
    doc["fan"]["min_pwm"] = fanController.getMinPWM();
    doc["fan"]["calibrating"] = fanController.isCalibrating();

    // LED status
    doc["led"]["connected"] = true;  // Cannot detect, assume connected
    doc["led"]["mode"] = (int)ledController.getMode();
    doc["led"]["brightness"] = ledController.getBrightness();

    // Button status
    doc["buttons"]["front_pressed"] = buttonHandler.isFrontPressed();
    doc["buttons"]["rear_pressed"] = buttonHandler.isRearPressed();

    // Pin configuration
#ifdef PLATFORM_ESP8266
    doc["pins"]["platform"] = "ESP8266";
#else
    doc["pins"]["platform"] = "ESP32";
#endif
    doc["pins"]["fan_pwm"] = FAN_PWM_PIN;
    doc["pins"]["fan_tacho"] = FAN_TACHO_PIN;
    doc["pins"]["led"] = LED_DATA_PIN;
    doc["pins"]["btn_front"] = BUTTON_FRONT_PIN;
    doc["pins"]["btn_rear"] = BUTTON_REAR_PIN;

    String response;
    if (serializeJson(doc, response) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", response);
}

void WebServer::handleDiagnosticLed(AsyncWebServerRequest* request) {
    if (request->hasParam("action", true)) {
        String action = request->getParam("action", true)->value();

        if (action == "test") {
            // Just show a quick color, don't block
            ledController.setColor(LED_COLOR_PURPLE);
            ledController.setMode(LedMode::BLINK_FAST);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"LED test mode (purple blink)\"}");
        } else if (action == "red") {
            ledController.setColor(LED_COLOR_RED);
            ledController.setMode(LedMode::ON);
            request->send(200, "application/json", "{\"success\":true,\"color\":\"red\"}");
        } else if (action == "green") {
            ledController.setColor(LED_COLOR_GREEN);
            ledController.setMode(LedMode::ON);
            request->send(200, "application/json", "{\"success\":true,\"color\":\"green\"}");
        } else if (action == "blue") {
            ledController.setColor(LED_COLOR_BLUE);
            ledController.setMode(LedMode::ON);
            request->send(200, "application/json", "{\"success\":true,\"color\":\"blue\"}");
        } else if (action == "off") {
            ledController.off();
            request->send(200, "application/json", "{\"success\":true,\"color\":\"off\"}");
        } else if (action == "reset") {
            // Return to normal state using priority system
            updateLedStatus();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"LED reset to normal\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Unknown action\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Missing action parameter\"}");
    }
}

void WebServer::handleDiagnosticFan(AsyncWebServerRequest* request) {
    if (request->hasParam("action", true)) {
        String action = request->getParam("action", true)->value();

        if (action == "test") {
            // Just turn on at 50% to test, don't block with delays
            fanController.setSpeed(50);
            fanController.turnOn();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Fan test: running at 50%\"}");
        } else if (action == "on") {
            fanController.turnOn();
            request->send(200, "application/json", "{\"success\":true,\"fan\":\"on\"}");
        } else if (action == "off") {
            fanController.turnOff();
            request->send(200, "application/json", "{\"success\":true,\"fan\":\"off\"}");
        } else if (action == "speed") {
            if (request->hasParam("value", true)) {
                int speed = request->getParam("value", true)->value().toInt();
                fanController.setSpeed(speed);
                if (!fanController.isOn()) fanController.turnOn();

                StaticJsonDocument<128> doc;
                doc["success"] = true;
                doc["speed"] = speed;
                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
            } else {
                request->send(400, "application/json", "{\"error\":\"Missing speed value\"}");
            }
        } else if (action == "rawpwm") {
            // Direct PWM value test (0-255)
            if (request->hasParam("value", true)) {
                int pwm = request->getParam("value", true)->value().toInt();
                pwm = constrain(pwm, 0, 255);
                fanController.setRawPWM(pwm);

                StaticJsonDocument<128> doc;
                doc["success"] = true;
                doc["raw_pwm"] = pwm;
                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
            } else {
                request->send(400, "application/json", "{\"error\":\"Missing PWM value (0-255)\"}");
            }
        } else if (action == "invert") {
            // Toggle PWM inversion
            bool newInvert = !fanController.isInvertPWM();
            if (request->hasParam("value", true)) {
                newInvert = request->getParam("value", true)->value() == "true";
            }
            fanController.setInvertPWM(newInvert);

            StaticJsonDocument<128> doc;
            doc["success"] = true;
            doc["invert"] = newInvert;
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else if (action == "calibrate") {
            // Start auto-calibration
            fanController.startCalibration();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration started\"}");
        } else if (action == "setmin") {
            // Manually set minimum PWM
            if (request->hasParam("value", true)) {
                int minPwm = request->getParam("value", true)->value().toInt();
                minPwm = constrain(minPwm, 0, 255);
                fanController.setMinPWM(minPwm);

                StaticJsonDocument<128> doc;
                doc["success"] = true;
                doc["min_pwm"] = minPwm;
                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
            } else {
                request->send(400, "application/json", "{\"error\":\"Missing min PWM value\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Unknown action\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Missing action parameter\"}");
    }
}

void WebServer::handleDiagnosticButtons(AsyncWebServerRequest* request) {
    StaticJsonDocument<128> doc;

    doc["front"]["pressed"] = buttonHandler.isFrontPressed();
    doc["front"]["pin"] = BUTTON_FRONT_PIN;
    doc["rear"]["pressed"] = buttonHandler.isRearPressed();
    doc["rear"]["pin"] = BUTTON_REAR_PIN;

    String response;
    if (serializeJson(doc, response) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", response);
}

// ==========================================
// Update Checker Handlers
// ==========================================

void WebServer::handleUpdateCheck(AsyncWebServerRequest* request) {
    // Schedule update check in loop() to avoid blocking async callback
    _pendingUpdateCheck = true;
    _pendingActionTime = millis();
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Checking for updates...\"}");
}

void WebServer::handleUpdateStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<384> doc;
    const UpdateInfo& info = updateChecker.getInfo();

    doc["available"] = info.available;
    doc["current"] = info.currentVersion;
    doc["latest"] = info.latestVersion;
    doc["release_url"] = info.releaseUrl;
    doc["state"] = (int)updateChecker.getState();
    doc["progress"] = info.downloadProgress;
    doc["error"] = info.errorMessage;
    doc["last_check"] = info.lastCheckTime;

    #ifndef PLATFORM_ESP8266
    doc["can_auto_update"] = true;
    doc["download_url"] = info.downloadUrl;
    #else
    doc["can_auto_update"] = false;
    #endif

    String response;
    if (serializeJson(doc, response) == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    request->send(200, "application/json", response);
}

#ifndef PLATFORM_ESP8266
void WebServer::handleStartUpdate(AsyncWebServerRequest* request) {
    if (!updateChecker.isUpdateAvailable()) {
        request->send(400, "application/json", "{\"error\":\"No update available\"}");
        return;
    }

    if (updateChecker.getState() != UpdateCheckState::IDLE) {
        request->send(400, "application/json", "{\"error\":\"Update already in progress\"}");
        return;
    }

    // Schedule OTA update in loop() to avoid blocking async callback
    _pendingOTAUpdate = true;
    _pendingActionTime = millis();
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Starting update download...\"}");
}
#endif
