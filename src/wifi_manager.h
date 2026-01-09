#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <DNSServer.h>
#include "config.h"

#ifdef PLATFORM_ESP8266
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
#endif

enum class WifiStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AP_MODE
};

class WiFiManager {
public:
    void begin();
    void loop();

    // Connection
    void connect(const char* ssid, const char* password);
    void disconnect();
    bool isConnected();

    // AP Mode
    void startAP();
    void stopAP();
    bool isAPMode();

    // Status
    WifiStatus getState();
    String getSSID();
    String getIP();
    int8_t getRSSI();
    String getMacAddress();
    String getAPName();

    // Callback
    typedef void (*StateChangeCallback)(WifiStatus status);
    void onStateChange(StateChangeCallback callback);

private:
    WifiStatus _state = WifiStatus::DISCONNECTED;
    unsigned long _connectStartTime = 0;
    unsigned long _lastReconnectAttempt = 0;
    uint8_t _reconnectAttempts = 0;
    String _ssid;
    String _password;
    String _apName;
    StateChangeCallback _callback = nullptr;

    static const uint8_t MAX_RECONNECT_ATTEMPTS = 3;
    static const unsigned long AP_RETRY_INTERVAL = 300000; // 5 min: retry WiFi while in AP mode
    unsigned long _lastAPRetry = 0;

    // DNS server for captive portal
    DNSServer _dnsServer;
    static const byte DNS_PORT = 53;

    void setState(WifiStatus state);
    void generateAPName();
};

extern WiFiManager wifiManager;

#endif // WIFI_MANAGER_H
