#include "web_server.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "fan_controller.h"
#include "mqtt_handler.h"
#include "rfid_handler.h"
#include "logger.h"
#include <ArduinoJson.h>

#ifdef PLATFORM_ESP8266
    #include <FS.h>
#else
    #include <SPIFFS.h>
#endif

// Update object is provided automatically by Arduino core

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

    // Fix: Check for allocation failure
    _server = new AsyncWebServer(WEBSERVER_PORT);
    if (_server == nullptr) {
        Serial.println("[WEB] CRITICAL: Server allocation failed");
        return;
    }

    // Initialize WebSocket - Fix: Check for allocation failure
    _ws = new AsyncWebSocket("/ws");
    if (_ws == nullptr) {
        Serial.println("[WEB] CRITICAL: WebSocket allocation failed");
        delete _server;
        _server = nullptr;
        return;
    }

    _ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server->addHandler(_ws);

    generateSessionToken();
    setupRoutes();
    _server->begin();

    Serial.println("[WEB] Server started on port 80");
    Serial.println("[WEB] WebSocket available at ws://[ip]/ws");
    Serial.printf("[WEB] Session token: %s\n", _sessionToken.c_str());
    Serial.println("[WEB] WARNING: Add 'X-Auth-Token' header to requests for protected endpoints");
}

void WebServer::loop() {
    if (_ws != nullptr) {
        _ws->cleanupClients();
    }
}

void WebServer::generateSessionToken() {
    // Generate random session token for CSRF protection
    uint8_t mac[6];
    WiFi.macAddress(mac);
    uint32_t random_val = ESP.getCycleCount() ^ millis();

    char token[17];
    snprintf(token, sizeof(token), "%02X%02X%02X%08X",
             mac[3], mac[4], mac[5], random_val);
    _sessionToken = String(token);
}

bool WebServer::checkAuth(AsyncWebServerRequest* request) {
    // Check for auth token in header
    if (request->hasHeader("X-Auth-Token")) {
        String token = request->header("X-Auth-Token");
        if (token == _sessionToken) {
            return true;
        }
    }

    // For initial setup in AP mode, allow access without token
    // This allows first-time configuration
    if (wifiManager.isAPMode()) {
        return true;
    }

    return false;
}

void WebServer::stop() {
    if (_server != nullptr) {
        _server->end();
        delete _server;
        _server = nullptr;
    }
    // Fix #1: Prevent WebSocket memory leak
    if (_ws != nullptr) {
        delete _ws;
        _ws = nullptr;
    }
}

void WebServer::onSettingsChanged(SettingsCallback callback) {
    _settingsCallback = callback;
}

void WebServer::setupRoutes() {
    // Add security headers to all responses
    DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
    DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
    DefaultHeaders::Instance().addHeader("X-XSS-Protection", "1; mode=block");
    DefaultHeaders::Instance().addHeader("Referrer-Policy", "no-referrer");

    // Serve static files from SPIFFS
    _server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // API endpoints
    // Auth token endpoint (for CSRF protection)
    _server->on("/api/auth", HTTP_GET, [this](AsyncWebServerRequest* request) {
        StaticJsonDocument<128> doc;
        doc["token"] = _sessionToken;
        doc["info"] = "Include this token in X-Auth-Token header for protected endpoints";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

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

    // Backup/Restore endpoints
    _server->on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleBackup(request);
    });

    _server->on("/api/restore", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200);
    }, NULL, [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
        // Fix #2: Buffer all chunks, not just the last one
        if (index == 0) {
            Serial.printf("[WEB] Restore started, total size: %u\n", total);
            _restoreBuffer = "";  // Clear buffer for new upload
            _restoreBuffer.reserve(total);  // Pre-allocate memory
        }

        // Append this chunk to buffer
        for (size_t i = 0; i < len; i++) {
            _restoreBuffer += (char)data[i];
        }

        // When all chunks received, process complete data
        if (index + len == total) {
            handleRestore(request, (uint8_t*)_restoreBuffer.c_str(), _restoreBuffer.length());
            _restoreBuffer = "";  // Clear buffer after processing
        }
    });

    // Logs API endpoint
    _server->on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleLogs(request);
    });

    // Web OTA Update endpoint
    _server->on("/update", HTTP_POST, [](AsyncWebServerRequest* request) {
        bool success = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
            success ? "{\"success\":true,\"message\":\"Update successful. Rebooting...\"}" :
                     "{\"success\":false,\"message\":\"Update failed\"}");
        response->addHeader("Connection", "close");
        request->send(response);

        if (success) {
            Serial.println("[WEB] OTA Update successful, rebooting...");
            delay(1000);
            ESP.restart();
        }
    }, [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t *data,
         size_t len, bool final) {
        if (index == 0) {
            Serial.printf("[WEB] OTA Update started: %s\n", filename.c_str());

            #ifdef PLATFORM_ESP8266
            Update.runAsync(true);
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace, U_FLASH)) {
            #else
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            #endif
                Update.printError(Serial);
                return;
            }
        }

        if (len) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
                return;
            }
        }

        if (final) {
            if (Update.end(true)) {
                Serial.printf("[WEB] OTA Update completed: %u bytes\n", index + len);
            } else {
                Update.printError(Serial);
            }
        }
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

    // Input validation
    if (ssid.length() == 0 || ssid.length() > 32) {
        request->send(400, "application/json", "{\"error\":\"SSID must be 1-32 characters\"}");
        return;
    }
    if (password.length() > 0 && password.length() < 8) {
        request->send(400, "application/json", "{\"error\":\"WiFi password must be at least 8 characters\"}");
        return;
    }
    if (password.length() > 63) {
        request->send(400, "application/json", "{\"error\":\"WiFi password too long (max 63)\"}");
        return;
    }

    storage.setWiFi(ssid.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi saved, connecting...\"}");

    // Connect to new network (async web server handles response independently)
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

    // Input validation
    if (host.length() == 0 || host.length() > 64) {
        request->send(400, "application/json", "{\"error\":\"MQTT host must be 1-64 characters\"}");
        return;
    }
    if (port == 0 || port > 65535) {
        request->send(400, "application/json", "{\"error\":\"Invalid MQTT port (1-65535)\"}");
        return;
    }
    if (user.length() > 32) {
        request->send(400, "application/json", "{\"error\":\"MQTT user too long (max 32)\"}");
        return;
    }
    if (password.length() > 64) {
        request->send(400, "application/json", "{\"error\":\"MQTT password too long (max 64)\"}");
        return;
    }

    storage.setMQTT(host.c_str(), port, user.c_str(), password.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"MQTT saved, connecting...\"}");

    // Reconnect MQTT (async web server handles response independently)
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
        // Input validation
        if (speed < 0 || speed > 100) {
            response["success"] = false;
            response["error"] = "Speed must be 0-100";
            String output;
            serializeJson(response, output);
            return request->send(400, "application/json", output);
        }
        fanController.setSpeed(speed);
        storage.setFanSpeed(speed, true);  // Commit user-initiated changes
    }

    if (request->hasParam("timer", true)) {
        int timer = request->getParam("timer", true)->value().toInt();
        // Input validation
        if (timer < 0 || timer > 1440) {  // Max 24 hours
            response["success"] = false;
            response["error"] = "Timer must be 0-1440 minutes";
            String output;
            serializeJson(response, output);
            return request->send(400, "application/json", output);
        }
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
        // Input validation
        if (onTime < 10 || onTime > 120 || offTime < 10 || offTime > 120) {
            response["success"] = false;
            response["error"] = "Interval times must be 10-120 seconds";
            String output;
            serializeJson(response, output);
            return request->send(400, "application/json", output);
        }
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

    // Reset storage and restart (async web server handles response independently)
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
    // Security: Never expose default passwords via API

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

    // Input validation
    if (start > 23 || end > 23) {
        request->send(400, "application/json", "{\"error\":\"Hours must be 0-23\"}");
        return;
    }
    if (brightness > 100) {
        request->send(400, "application/json", "{\"error\":\"Brightness must be 0-100\"}");
        return;
    }

    storage.setNightMode(enabled, start, end, brightness);

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Night mode settings saved\"}");
}
}

// WebSocket event handler
void WebServer::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch(type) {
        case WS_EVT_CONNECT:
            // Fix: Limit max WebSocket clients to prevent DoS attacks
            if (_ws->count() > 5) {
                Serial.printf("[WS] Client limit reached, rejecting client #%u\n", client->id());
                client->close();
                break;
            }
            Serial.printf("[WS] Client #%u connected from %s\n", client->id(),
                         client->remoteIP().toString().c_str());
            // Send current state to new client
            broadcastState();
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServer::handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        // Fix #5: Prevent buffer overflow - don't write past data buffer
        // Create String from data with explicit length (no null terminator needed)
        String message;
        message.reserve(len + 1);
        for (size_t i = 0; i < len; i++) {
            message += (char)data[i];
        }

        // Parse JSON command
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            Serial.println("[WS] JSON parse error");
            return;
        }

        // Handle commands
        if (doc.containsKey("fan")) {
            String cmd = doc["fan"];
            if (cmd == "on") fanController.turnOn();
            else if (cmd == "off") fanController.turnOff();
            broadcastState();
        }
        if (doc.containsKey("speed")) {
            fanController.setSpeed(doc["speed"]);
            broadcastState();
        }
        if (doc.containsKey("request") && doc["request"] == "state") {
            broadcastState();
        }
    }
}

void WebServer::broadcastState() {
    if (_ws == nullptr || _ws->count() == 0) return;

    // Throttle broadcasts to max 2 per second
    unsigned long now = millis();
    if (now - _lastBroadcast < 500) return;
    _lastBroadcast = now;

    DiffuserSettings settings = storage.load();
    StaticJsonDocument<768> doc;

    // Fan state
    doc["fan"]["on"] = fanController.isOn();
    doc["fan"]["speed"] = fanController.getSpeed();
    doc["fan"]["timer_active"] = fanController.isTimerActive();
    doc["fan"]["remaining_minutes"] = fanController.getRemainingMinutes();
    doc["fan"]["interval_mode"] = fanController.isIntervalMode();

    // WiFi state
    doc["wifi"]["connected"] = wifiManager.isConnected();
    doc["wifi"]["rssi"] = wifiManager.getRSSI();
    doc["wifi"]["ip"] = wifiManager.getIP();

    // MQTT state
    doc["mqtt"]["connected"] = mqttHandler.isConnected();

    // RFID state
    doc["rfid"]["tag_present"] = rfidHandler.isTagPresent();
    doc["rfid"]["cartridge"] = rfidHandler.getCartridgeName();

    // Runtime stats
    doc["runtime"]["total"] = fanController.getTotalRuntimeMinutes() / 60.0;
    doc["runtime"]["session"] = fanController.getSessionRuntimeMinutes();

    String output;
    serializeJson(doc, output);

    // Fix #7: Thread-safe null check before using WebSocket
    // Prevents race condition if stop() is called during broadcast
    if (_ws != nullptr) {
        _ws->textAll(output);
    }
}

// Backup configuration
void WebServer::handleBackup(AsyncWebServerRequest* request) {
    DiffuserSettings settings = storage.load();
    DynamicJsonDocument doc(2048);

    // WiFi
    doc["wifi"]["ssid"] = settings.wifiSsid;
    doc["wifi"]["password"] = settings.wifiPassword;

    // MQTT
    doc["mqtt"]["host"] = settings.mqttHost;
    doc["mqtt"]["port"] = settings.mqttPort;
    doc["mqtt"]["user"] = settings.mqttUser;
    doc["mqtt"]["password"] = settings.mqttPassword;

    // Device
    doc["device"]["name"] = settings.deviceName;

    // Fan
    doc["fan"]["speed"] = settings.fanSpeed;
    doc["fan"]["interval_enabled"] = settings.intervalEnabled;
    doc["fan"]["interval_on"] = settings.intervalOnTime;
    doc["fan"]["interval_off"] = settings.intervalOffTime;

    // Security
    doc["security"]["ota_password"] = settings.otaPassword;
    doc["security"]["ap_password"] = settings.apPassword;

    // RFID
    doc["rfid"]["configured"] = settings.rfidConfigured;
    if (settings.rfidConfigured) {
        doc["rfid"]["pins"]["sck"] = settings.rfidPinSCK;
        doc["rfid"]["pins"]["miso"] = settings.rfidPinMISO;
        doc["rfid"]["pins"]["mosi"] = settings.rfidPinMOSI;
        doc["rfid"]["pins"]["ss"] = settings.rfidPinSS;
        doc["rfid"]["pins"]["rst"] = settings.rfidPinRST;
    }

    // Night mode
    doc["night_mode"]["enabled"] = settings.nightModeEnabled;
    doc["night_mode"]["start"] = settings.nightModeStart;
    doc["night_mode"]["end"] = settings.nightModeEnd;
    doc["night_mode"]["brightness"] = settings.nightModeBrightness;

    // Metadata
    doc["backup_version"] = "1.3.0";
    doc["backup_timestamp"] = millis() / 1000;

    String output;
    serializeJson(doc, output);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output);
    response->addHeader("Content-Disposition", "attachment; filename=rituals-diffuser-backup.json");
    request->send(response);

    Serial.println("[WEB] Configuration backup downloaded");
}

// Restore configuration
void WebServer::handleRestore(AsyncWebServerRequest* request, uint8_t *data, size_t len) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    DiffuserSettings settings;
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;

    // Restore WiFi
    if (doc.containsKey("wifi")) {
        strlcpy(settings.wifiSsid, doc["wifi"]["ssid"] | "", sizeof(settings.wifiSsid));
        strlcpy(settings.wifiPassword, doc["wifi"]["password"] | "", sizeof(settings.wifiPassword));
    }

    // Restore MQTT
    if (doc.containsKey("mqtt")) {
        strlcpy(settings.mqttHost, doc["mqtt"]["host"] | "", sizeof(settings.mqttHost));
        settings.mqttPort = doc["mqtt"]["port"] | 1883;
        strlcpy(settings.mqttUser, doc["mqtt"]["user"] | "", sizeof(settings.mqttUser));
        strlcpy(settings.mqttPassword, doc["mqtt"]["password"] | "", sizeof(settings.mqttPassword));
    }

    // Restore device
    if (doc.containsKey("device")) {
        strlcpy(settings.deviceName, doc["device"]["name"] | "Rituals Diffuser", sizeof(settings.deviceName));
    }

    // Restore fan - Fix #4: Add input validation
    if (doc.containsKey("fan")) {
        uint8_t speed = doc["fan"]["speed"] | 50;
        settings.fanSpeed = constrain(speed, 0, 100);  // Validate 0-100

        settings.intervalEnabled = doc["fan"]["interval_enabled"] | false;

        uint8_t onTime = doc["fan"]["interval_on"] | 30;
        uint8_t offTime = doc["fan"]["interval_off"] | 30;
        settings.intervalOnTime = constrain(onTime, 10, 120);  // Validate 10-120
        settings.intervalOffTime = constrain(offTime, 10, 120);
    }

    // Restore security - Fix #4: Validate password lengths
    if (doc.containsKey("security")) {
        const char* otaPass = doc["security"]["ota_password"] | "";
        const char* apPass = doc["security"]["ap_password"] | "";

        // Only restore if valid length (0 = default, or >= 8)
        if (strlen(otaPass) == 0 || strlen(otaPass) >= 8) {
            strlcpy(settings.otaPassword, otaPass, sizeof(settings.otaPassword));
        }
        if (strlen(apPass) == 0 || strlen(apPass) >= 8) {
            strlcpy(settings.apPassword, apPass, sizeof(settings.apPassword));
        }
    }

    // Restore night mode - Fix #4: Validate hour and brightness ranges
    if (doc.containsKey("night_mode")) {
        settings.nightModeEnabled = doc["night_mode"]["enabled"] | false;

        uint8_t start = doc["night_mode"]["start"] | 22;
        uint8_t end = doc["night_mode"]["end"] | 7;
        uint8_t brightness = doc["night_mode"]["brightness"] | 10;

        settings.nightModeStart = constrain(start, 0, 23);  // Validate 0-23
        settings.nightModeEnd = constrain(end, 0, 23);
        settings.nightModeBrightness = constrain(brightness, 0, 100);  // Validate 0-100
    }

    // Save restored settings
    storage.save(settings);

    request->send(200, "application/json",
                 "{\"success\":true,\"message\":\"Configuration restored. Restart device to apply.\"}");

    Serial.println("[WEB] Configuration restored from backup");
}

// Get system logs
void WebServer::handleLogs(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(4096);
    doc["version"] = "1.3.0";
    doc["uptime"] = millis() / 1000;  // seconds
    doc["count"] = logger.getLogCount();

    JsonArray logs = doc.createNestedArray("logs");

    for (int i = 0; i < logger.getLogCount(); i++) {
        const LogEntry& entry = logger.getLog(i);
        JsonObject log = logs.createNestedObject();

        log["timestamp"] = entry.timestamp;

        // Format timestamp as human-readable (MM:SS)
        unsigned long seconds = entry.timestamp / 1000;
        unsigned long minutes = seconds / 60;
        seconds = seconds % 60;
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", minutes % 60, seconds);
        log["time"] = timeStr;

        // Level as string
        const char* level = "INFO";
        switch (entry.level) {
            case LogLevel::INFO:    level = "INFO"; break;
            case LogLevel::WARNING: level = "WARN"; break;
            case LogLevel::ERROR:   level = "ERROR"; break;
            case LogLevel::DEBUG:   level = "DEBUG"; break;
        }
        log["level"] = level;
        log["message"] = entry.message;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
