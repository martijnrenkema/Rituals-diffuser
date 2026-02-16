#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include "logger.h"

// WiFi library is included via wifi_manager.h

WiFiManager wifiManager;

void WiFiManager::begin() {
    generateAPName();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Check if WiFi is already connected (can happen after OTA update/restart
    // when SDK auto-reconnects faster than our state machine)
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Already connected (SDK auto-reconnect)");
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        _state = WifiStatus::CONNECTED;
        // Note: callback not called here as it's not registered yet in setup()
    }

    Serial.println("[WIFI] Manager initialized");
}

void WiFiManager::loop() {
    unsigned long now = millis();

    switch (_state) {
        case WifiStatus::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _reconnectAttempts = 0;
                setState(WifiStatus::CONNECTED);
                Serial.printf("[WIFI] Connected to %s\n", _ssid);
                Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
                logger.infof("WiFi connected: %s (%s)", _ssid, WiFi.localIP().toString().c_str());
            } else if (now - _connectStartTime >= WIFI_CONNECT_TIMEOUT) {
                _reconnectAttempts++;
                Serial.printf("[WIFI] Connection timeout (attempt %d/%d)\n", _reconnectAttempts, MAX_RECONNECT_ATTEMPTS);
                logger.warnf("WiFi timeout (attempt %d/%d)", _reconnectAttempts, MAX_RECONNECT_ATTEMPTS);

                if (_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
                    Serial.println("[WIFI] Max attempts reached, starting AP mode as fallback");
                    logger.error("WiFi max attempts reached, starting AP mode");
                    startAP();
                } else {
                    setState(WifiStatus::DISCONNECTED);
                    _lastReconnectAttempt = now;
                }
            }
            break;

        case WifiStatus::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Connection lost, will attempt reconnect");
                logger.error("WiFi connection lost");
                setState(WifiStatus::DISCONNECTED);
                _lastReconnectAttempt = now;
            }
            break;

        case WifiStatus::DISCONNECTED:
            // Auto reconnect if we have credentials
            if (_ssid[0] != '\0' && now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
                Serial.println("[WIFI] Attempting reconnect...");
                connect(_ssid, _password);
            }
            break;

        case WifiStatus::AP_MODE:
            // Start DNS server on first loop iteration (after webserver is ready)
            if (!_dnsStarted) {
                _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
                _dnsStarted = true;
                Serial.println("[WIFI] DNS server started for captive portal");
            }

            // Process DNS requests for captive portal
            _dnsServer.processNextRequest();

            // Periodically try to reconnect to saved WiFi while in AP mode
            if (_ssid[0] != '\0' && now - _lastAPRetry >= AP_RETRY_INTERVAL) {
                _lastAPRetry = now;
                Serial.println("[WIFI] AP mode: trying saved WiFi in background...");
                // Switch to AP_STA mode to allow WiFi connection while keeping AP active
                WiFi.mode(WIFI_AP_STA);
                WiFi.begin(_ssid, _password);
                _apRetryConnectStart = now;  // Use separate timestamp for AP retry
            }

            // Check if background reconnect succeeded or timed out
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[WIFI] Reconnected to WiFi!");
                Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
                logger.infof("WiFi reconnected from AP: %s", WiFi.localIP().toString().c_str());
                _reconnectAttempts = 0;
                stopAP();
                setState(WifiStatus::CONNECTED);
            } else if (WiFi.getMode() == WIFI_AP_STA && now - _apRetryConnectStart >= 30000) {
                // Background reconnect timed out after 30s, switch back to pure AP mode
                Serial.println("[WIFI] Background reconnect timeout, staying in AP mode");
                WiFi.mode(WIFI_AP);
            }
            break;
    }
}

void WiFiManager::connect(const char* ssid, const char* password) {
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    _ssid[sizeof(_ssid) - 1] = '\0';
    strncpy(_password, password, sizeof(_password) - 1);
    _password[sizeof(_password) - 1] = '\0';

    // Stop AP if running
    if (_state == WifiStatus::AP_MODE) {
        stopAP();
    }

    // Check if already connected to this network (common after OTA/restart)
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
        Serial.printf("[WIFI] Already connected to %s\n", ssid);
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        setState(WifiStatus::CONNECTED);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    _connectStartTime = millis();
    setState(WifiStatus::CONNECTING);
    Serial.printf("[WIFI] Connecting to %s...\n", ssid);
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    setState(WifiStatus::DISCONNECTED);
    Serial.println("[WIFI] Disconnected");
}

bool WiFiManager::isConnected() {
    return _state == WifiStatus::CONNECTED && WiFi.status() == WL_CONNECTED;
}

void WiFiManager::startAP() {
    // Disconnect any existing connections first
    WiFi.disconnect(true);
    delay(100);

    // Use pure AP mode for better compatibility on ESP8266
    WiFi.mode(WIFI_AP);
    delay(100);  // ESP8266 needs time after mode change

    // Configure AP with explicit IP settings for reliability
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);

    const char* apPassword = storage.getAPPassword();

    // Start the AP
    bool apStarted = WiFi.softAP(_apName, apPassword, 1, false, 4);

    if (!apStarted) {
        Serial.println("[WIFI] ERROR: Failed to start AP!");
        logger.error("Failed to start AP");
        return;
    }

    // Wait for AP to be fully ready
    delay(500);

    // Verify AP IP is valid before starting DNS
    IPAddress currentIP = WiFi.softAPIP();
    if (currentIP == IPAddress(0, 0, 0, 0)) {
        Serial.println("[WIFI] ERROR: AP IP is 0.0.0.0!");
        logger.error("AP IP invalid");
        return;
    }

    setState(WifiStatus::AP_MODE);
    Serial.printf("[WIFI] AP started: %s\n", _apName);
    Serial.printf("[WIFI] AP Password: %s\n", apPassword);
    Serial.printf("[WIFI] AP IP: %s\n", currentIP.toString().c_str());
    logger.infof("AP mode started: %s", _apName);

    // DNS server will be started after webserver is ready (in main.cpp)
    // This prevents race condition where DNS redirects before webserver is listening
}

void WiFiManager::stopAP() {
    _dnsServer.stop();
    _dnsStarted = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("[WIFI] AP stopped");
}

bool WiFiManager::isAPMode() {
    return _state == WifiStatus::AP_MODE;
}

WifiStatus WiFiManager::getState() {
    return _state;
}

String WiFiManager::getSSID() {
    if (_state == WifiStatus::AP_MODE) {
        return String(_apName);
    }
    return WiFi.SSID();
}

String WiFiManager::getIP() {
    if (_state == WifiStatus::AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

int8_t WiFiManager::getRSSI() {
    if (_state == WifiStatus::CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

String WiFiManager::getMacAddress() {
    return WiFi.macAddress();
}

String WiFiManager::getAPName() {
    return String(_apName);
}

void WiFiManager::onStateChange(StateChangeCallback callback) {
    _callback = callback;
}

void WiFiManager::setState(WifiStatus state) {
    if (_state != state) {
        _state = state;
        if (_callback) {
            _callback(state);
        }
    }
}

void WiFiManager::generateAPName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    snprintf(_apName, sizeof(_apName), "%s%s", WIFI_AP_SSID_PREFIX, suffix);
}
