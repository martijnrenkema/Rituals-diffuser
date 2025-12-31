#include "web_server.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "fan_controller.h"
#include "led_controller.h"
#include "button_handler.h"
#include "mqtt_handler.h"
#include "logger.h"
#include <ArduinoJson.h>

// External function and flag from main.cpp for LED priority system
extern void updateLedStatus();
extern bool otaInProgress;

#ifdef PLATFORM_ESP8266
    #include <FS.h>
    #include <Updater.h>
    // ESP8266 uses different method names
    #define UPDATE_ERROR_STRING() Update.getErrorString()
    // Linker symbols for filesystem size
    extern "C" uint32_t _FS_start;
    extern "C" uint32_t _FS_end;
#else
    #include <SPIFFS.h>
    #include <Update.h>
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

    // Initialize SPIFFS
#ifdef PLATFORM_ESP8266
    if (!SPIFFS.begin()) {
#else
    if (!SPIFFS.begin(true)) {
#endif
        Serial.println("[WEB] SPIFFS mount failed");
    }

    _server = new AsyncWebServer(WEBSERVER_PORT);
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

void WebServer::onSettingsChanged(SettingsCallback callback) {
    _settingsCallback = callback;
}

void WebServer::setupRoutes() {
    // Serve static files from SPIFFS
    _server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // API endpoints
    _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStatus(request);
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

    // OTA Update - Firmware
    _server->on("/api/update/firmware", HTTP_POST,
        [](AsyncWebServerRequest* request) {
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
                delay(1000);
                ESP.restart();
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
                yield();
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
        [](AsyncWebServerRequest* request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse* response = request->beginResponse(
                success ? 200 : 500,
                "text/plain",
                success ? "OK" : "Update failed"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                delay(1000);
                ESP.restart();
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
                yield();
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

    // Captive portal redirect
    _server->onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    // Generate captive portal detection responses
    _server->on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    _server->on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    _server->on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
}

void WebServer::handleStatus(AsyncWebServerRequest* request) {
    const DiffuserSettings& settings = storage.getSettings();  // Use cache, no NVS read

    // Use DynamicJsonDocument to avoid stack overflow on ESP8266 (limited 4KB stack)
    DynamicJsonDocument doc(1024);  // Heap allocation, reduced size - actual payload is ~600 bytes

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

    // Statistics
    doc["stats"]["total_runtime"] = storage.getTotalRuntimeMinutes() / 60.0;  // hours
    doc["stats"]["session_runtime"] = fanController.getSessionRuntimeMinutes();  // minutes

    // Night mode
    doc["night"]["enabled"] = settings.nightModeEnabled;
    doc["night"]["start"] = settings.nightModeStart;
    doc["night"]["end"] = settings.nightModeEnd;
    doc["night"]["brightness"] = settings.nightModeBrightness;

    String response;
    size_t jsonSize = serializeJson(doc, response);
    if (jsonSize == 0) {
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }

    request->send(200, "application/json", response);
}

void WebServer::handleSaveWifi(AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String password = request->getParam("password", true)->value();

    storage.setWiFi(ssid.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi saved, connecting...\"}");

    // Wait for HTTP response to be fully sent before reconnecting WiFi
    // WiFi.begin() resets the radio which can interrupt the TCP connection
    // 500ms is enough for the browser to receive the complete response + TCP ACK
    delay(500);
    wifiManager.connect(ssid.c_str(), password.c_str());

    if (_settingsCallback) {
        _settingsCallback();
    }
}

void WebServer::handleSaveMqtt(AsyncWebServerRequest* request) {
    if (!request->hasParam("host", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing host parameter\"}");
        return;
    }

    String host = request->getParam("host", true)->value();
    uint16_t port = 1883;
    String user = "";
    String password = "";

    if (request->hasParam("port", true)) {
        int portVal = request->getParam("port", true)->value().toInt();
        // Validate MQTT port range (1-65535)
        if (portVal > 0 && portVal <= 65535) {
            port = portVal;
        }
    }
    if (request->hasParam("user", true)) {
        user = request->getParam("user", true)->value();
    }
    if (request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
    }

    storage.setMQTT(host.c_str(), port, user.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"MQTT saved, connecting...\"}");

    // Reconnect MQTT (small delay to let response send)
    delay(100);
    mqttHandler.disconnect();
    mqttHandler.connect(host.c_str(), port, user.c_str(), password.c_str());

    if (_settingsCallback) {
        _settingsCallback();
    }
}

void WebServer::handleFanControl(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> response;
    response["success"] = true;

    if (request->hasParam("power", true)) {
        String power = request->getParam("power", true)->value();
        if (power == "on") {
            fanController.turnOn();
        } else {
            fanController.turnOff();
        }
    }

    if (request->hasParam("speed", true)) {
        int speed = request->getParam("speed", true)->value().toInt();
        fanController.setSpeed(speed);
        storage.setFanSpeed(speed);
    }

    if (request->hasParam("timer", true)) {
        int timer = request->getParam("timer", true)->value().toInt();
        if (timer > 0) {
            fanController.setTimer(timer);
        } else {
            fanController.cancelTimer();
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
        int onTime = request->getParam("interval_on", true)->value().toInt();
        int offTime = request->getParam("interval_off", true)->value().toInt();
        fanController.setIntervalTimes(onTime, offTime);
        storage.setIntervalMode(fanController.isIntervalMode(), onTime, offTime);
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

    delay(100);
    storage.reset();
    ESP.restart();
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
    // Use DynamicJsonDocument to avoid stack pressure on ESP8266
    DynamicJsonDocument doc(512);

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
