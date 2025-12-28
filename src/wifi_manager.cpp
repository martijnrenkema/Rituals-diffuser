#include "wifi_manager.h"
#include "config.h"
#include "storage.h"

// WiFi library is included via wifi_manager.h

WiFiManager wifiManager;

void WiFiManager::begin() {
    generateAPName();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
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
            } else if (now - _connectStartTime >= WIFI_CONNECT_TIMEOUT) {
                _reconnectAttempts++;
                Serial.printf("[WIFI] Connection timeout (attempt %d/%d)\n", _reconnectAttempts, MAX_RECONNECT_ATTEMPTS);

                if (_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
                    Serial.println("[WIFI] Max attempts reached, starting AP mode as fallback");
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
                _reconnectAttempts = 0;
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

    setState(WifiStatus::AP_MODE);
    Serial.printf("[WIFI] AP started: %s\n", _apName.c_str());
    Serial.printf("[WIFI] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::stopAP() {
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
