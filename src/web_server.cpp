#include "web_server.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "fan_controller.h"
#include "led_controller.h"
#include "button_handler.h"
#include "mqtt_handler.h"
#include "rfid_handler.h"
#include <ArduinoJson.h>

#ifdef PLATFORM_ESP8266
    #include <FS.h>
#else
    #include <SPIFFS.h>
#endif

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

    _server->on("/api/rfid", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetRFID(request);
    });

    _server->on("/api/rfid", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleRFIDAction(request);
    });

    _server->on("/api/night", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetNightMode(request);
    });

    _server->on("/api/night", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSaveNightMode(request);
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
    DiffuserSettings settings = storage.load();

    StaticJsonDocument<1536> doc;

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
    doc["stats"]["cartridge_runtime"] = storage.getCartridgeRuntimeMinutes() / 60.0;  // hours

    // RFID status
    doc["rfid"]["configured"] = rfidHandler.isConfigured();
    doc["rfid"]["scanning"] = rfidHandler.isScanning();
    doc["rfid"]["tag_present"] = rfidHandler.isTagPresent();
    doc["rfid"]["cartridge"] = rfidHandler.getCartridgeName();

    // Night mode
    doc["night"]["enabled"] = settings.nightModeEnabled;
    doc["night"]["start"] = settings.nightModeStart;
    doc["night"]["end"] = settings.nightModeEnd;
    doc["night"]["brightness"] = settings.nightModeBrightness;

    String response;
    serializeJson(doc, response);

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

    // Connect to new network
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
        port = request->getParam("port", true)->value().toInt();
    }
    if (request->hasParam("user", true)) {
        user = request->getParam("user", true)->value();
    }
    if (request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
    }

    storage.setMQTT(host.c_str(), port, user.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"MQTT saved, connecting...\"}");

    // Reconnect MQTT
    delay(500);
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
    }

    if (request->hasParam("interval", true)) {
        bool interval = request->getParam("interval", true)->value() == "true";
        fanController.setIntervalMode(interval);
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
    serializeJson(response, output);
    request->send(200, "application/json", output);

    // Publish state to MQTT
    mqttHandler.publishState();
}

void WebServer::handleReset(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Resetting...\"}");

    delay(500);
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
    StaticJsonDocument<256> doc;
    // Don't return actual passwords, just indicate if custom ones are set
    doc["ota_custom"] = strlen(storage.load().otaPassword) > 0;
    doc["ap_custom"] = strlen(storage.load().apPassword) > 0;
    doc["ota_default"] = OTA_PASSWORD;
    doc["ap_default"] = WIFI_AP_PASSWORD;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleGetRFID(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;

    doc["configured"] = rfidHandler.isConfigured();
    doc["scanning"] = rfidHandler.isScanning();
    doc["tag_present"] = rfidHandler.isTagPresent();
    doc["cartridge"] = rfidHandler.getCartridgeName();
    doc["uid"] = rfidHandler.getTagUID();
    doc["runtime"] = storage.getCartridgeRuntimeMinutes() / 60.0;  // hours

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleRFIDAction(AsyncWebServerRequest* request) {
    if (request->hasParam("action", true)) {
        String action = request->getParam("action", true)->value();

        if (action == "scan") {
            rfidHandler.startScan();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Scanning for RFID reader...\"}");
        } else if (action == "clear") {
            rfidHandler.clearConfig();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"RFID configuration cleared\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Unknown action\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Missing action parameter\"}");
    }
}

void WebServer::handleGetNightMode(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    DiffuserSettings settings = storage.load();

    doc["enabled"] = settings.nightModeEnabled;
    doc["start"] = settings.nightModeStart;
    doc["end"] = settings.nightModeEnd;
    doc["brightness"] = settings.nightModeBrightness;

    String response;
    serializeJson(doc, response);
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
        start = request->getParam("start", true)->value().toInt();
    }
    if (request->hasParam("end", true)) {
        end = request->getParam("end", true)->value().toInt();
    }
    if (request->hasParam("brightness", true)) {
        brightness = request->getParam("brightness", true)->value().toInt();
    }

    storage.setNightMode(enabled, start, end, brightness);

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Night mode settings saved\"}");
}

// =====================================================
// Hardware Diagnostics
// =====================================================

void WebServer::handleDiagnostic(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> doc;

    // Fan status
    doc["fan"]["connected"] = true;  // Assume connected if we can control it
    doc["fan"]["on"] = fanController.isOn();
    doc["fan"]["speed"] = fanController.getSpeed();

    // LED status
    doc["led"]["connected"] = true;
    doc["led"]["mode"] = (int)ledController.getMode();
    doc["led"]["brightness"] = ledController.getBrightness();

    // Button status
    doc["buttons"]["front_pressed"] = buttonHandler.isFrontPressed();
    doc["buttons"]["rear_pressed"] = buttonHandler.isRearPressed();

    // RFID status
    doc["rfid"]["connected"] = rfidHandler.isConfigured();
    doc["rfid"]["tag_present"] = rfidHandler.isTagPresent();
    doc["rfid"]["cartridge"] = rfidHandler.getCartridgeName();

    // Pin configuration
#ifdef PLATFORM_ESP8266
    doc["pins"]["platform"] = "ESP8266";
    doc["pins"]["fan_pwm"] = FAN_PWM_PIN;
    doc["pins"]["fan_speed"] = FAN_SPEED_PIN;
    doc["pins"]["led"] = LED_DATA_PIN;
    doc["pins"]["btn_front"] = BUTTON_FRONT_PIN;
    doc["pins"]["btn_rear"] = BUTTON_REAR_PIN;
#else
    doc["pins"]["platform"] = "ESP32";
    doc["pins"]["fan_pwm"] = FAN_PWM_PIN;
    doc["pins"]["fan_speed"] = FAN_SPEED_PIN;
    doc["pins"]["led"] = LED_DATA_PIN;
    doc["pins"]["btn_front"] = BUTTON_FRONT_PIN;
    doc["pins"]["btn_rear"] = BUTTON_REAR_PIN;
    doc["pins"]["rfid_sck"] = RFID_SCK_PIN;
    doc["pins"]["rfid_miso"] = RFID_MISO_PIN;
    doc["pins"]["rfid_mosi"] = RFID_MOSI_PIN;
    doc["pins"]["rfid_ss"] = RFID_SS_PIN;
    doc["pins"]["rfid_rst"] = RFID_RST_PIN;
#endif

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServer::handleDiagnosticLed(AsyncWebServerRequest* request) {
    if (request->hasParam("action", true)) {
        String action = request->getParam("action", true)->value();

        if (action == "test") {
            // Run LED color test sequence
            request->send(200, "application/json", "{\"success\":true,\"message\":\"LED test started\"}");

            // Cycle through colors
            ledController.setColor(LED_COLOR_RED);
            ledController.setMode(LedMode::ON);
            delay(500);
            ledController.setColor(LED_COLOR_GREEN);
            delay(500);
            ledController.setColor(LED_COLOR_BLUE);
            delay(500);
            ledController.setColor(LED_COLOR_PURPLE);
            delay(500);
            ledController.setColor(LED_COLOR_ORANGE);
            delay(500);
            ledController.setColor(LED_COLOR_CYAN);
            delay(500);
            ledController.setColor(LED_COLOR_WHITE);
            delay(500);

            // Return to normal state
            if (fanController.isOn()) {
                ledController.showFanRunning();
            } else if (wifiManager.isConnected()) {
                ledController.showConnected();
            } else {
                ledController.showAPMode();
            }
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
            // Return to normal state
            if (fanController.isOn()) {
                ledController.showFanRunning();
            } else if (wifiManager.isConnected()) {
                ledController.showConnected();
            } else {
                ledController.showAPMode();
            }
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
            // Run fan test sequence
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Fan test started\"}");

            // Test at different speeds
            fanController.setSpeed(25);
            fanController.turnOn();
            delay(1000);
            fanController.setSpeed(50);
            delay(1000);
            fanController.setSpeed(75);
            delay(1000);
            fanController.setSpeed(100);
            delay(1000);
            fanController.turnOff();
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
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
