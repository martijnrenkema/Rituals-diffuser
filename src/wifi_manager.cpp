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
                Serial.printf("[WIFI] Connected to %s\n", _ssid.c_str());
                Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
                logger.infof("WiFi connected: %s (%s)", _ssid.c_str(), WiFi.localIP().toString().c_str());
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
            if (_ssid.length() > 0 && now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
                Serial.println("[WIFI] Attempting reconnect...");
                connect(_ssid.c_str(), _password.c_str());
            }
            break;

        case WifiStatus::AP_MODE:
            // Process DNS requests for captive portal
            _dnsServer.processNextRequest();

            // Periodically try to reconnect to saved WiFi while in AP mode
            // Non-blocking: just check if already connected
            if (_ssid.length() > 0 && now - _lastAPRetry >= AP_RETRY_INTERVAL) {
                _lastAPRetry = now;
                Serial.println("[WIFI] AP mode: trying saved WiFi in background...");
                // Try to connect while keeping AP active (WIFI_AP_STA mode)
                WiFi.begin(_ssid.c_str(), _password.c_str());
                // Don't block - we'll check connection status next loop iteration
            }
            // Check if background reconnect succeeded (non-blocking check)
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[WIFI] Reconnected to WiFi!");
                Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
                logger.infof("WiFi reconnected from AP: %s", WiFi.localIP().toString().c_str());
                _reconnectAttempts = 0;
                // Stop AP mode to close the security hole
                stopAP();
                setState(WifiStatus::CONNECTED);
            }
            break;
    }
}

void WiFiManager::connect(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;

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
    WiFi.mode(WIFI_AP_STA);  // Both AP and STA mode for flexibility
    WiFi.softAP(_apName.c_str(), storage.getAPPassword());  // Use stored or default password

    // Start DNS server for captive portal - redirect all domains to AP IP
    _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.println("[WIFI] DNS server started for captive portal");

    setState(WifiStatus::AP_MODE);
    Serial.printf("[WIFI] AP started: %s\n", _apName.c_str());
    Serial.printf("[WIFI] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    logger.infof("AP mode started: %s", _apName.c_str());
}

void WiFiManager::stopAP() {
    _dnsServer.stop();
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
        return _apName;
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
    return _apName;
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
    _apName = String(WIFI_AP_SSID_PREFIX) + suffix;
}
