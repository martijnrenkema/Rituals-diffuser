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
#ifdef PLATFORM_ESP8266
    // Use char arrays instead of String to reduce static RAM overhead
    char _ssid[33];         // Max SSID 32 + null
    char _password[48];     // Most passwords < 48
    char _apName[24];       // Short AP names
#else
    String _ssid;
    String _password;
    String _apName;
#endif
    StateChangeCallback _callback = nullptr;

    static const uint8_t MAX_RECONNECT_ATTEMPTS = 3;
    static const unsigned long AP_RETRY_INTERVAL = 300000; // 5 min: retry WiFi while in AP mode
    unsigned long _lastAPRetry = 0;
    unsigned long _apRetryConnectStart = 0;  // Separate timestamp for AP background retry

    // DNS server for captive portal
    DNSServer _dnsServer;
    static const byte DNS_PORT = 53;
    bool _dnsStarted = false;

    void setState(WifiStatus state);
    void generateAPName();
};

extern WiFiManager wifiManager;

#endif // WIFI_MANAGER_H
