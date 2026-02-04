#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

class WebServer {
public:
    void begin();
    void loop();  // Process pending actions from callbacks
    void stop();

    // Callback for settings changes
    typedef void (*SettingsCallback)();
    void onSettingsChanged(SettingsCallback callback);

private:
    AsyncWebServer* _server = nullptr;
    SettingsCallback _settingsCallback = nullptr;

    // Deferred action flags (to avoid blocking in async callbacks)
    // Use char arrays instead of String to avoid heap fragmentation
    bool _pendingWifiConnect = false;
#ifdef PLATFORM_ESP8266
    char _pendingWifiSsid[33];      // Max SSID length + null
    char _pendingWifiPassword[48];  // Most passwords < 48 chars
    bool _pendingMqttConnect = false;
    char _pendingMqttHost[48];      // Most hostnames < 48 chars
    uint16_t _pendingMqttPort = 1883;
    char _pendingMqttUser[24];      // Most usernames < 24 chars
    char _pendingMqttPassword[48];  // Most passwords < 48 chars
#else
    char _pendingWifiSsid[33];      // Max SSID length + null
    char _pendingWifiPassword[65];  // Max password length + null
    bool _pendingMqttConnect = false;
    char _pendingMqttHost[65];      // Max hostname length + null
    uint16_t _pendingMqttPort = 1883;
    char _pendingMqttUser[33];      // Max username length + null
    char _pendingMqttPassword[65];  // Max password length + null
#endif
    bool _pendingReset = false;
    bool _pendingRestart = false;
    bool _pendingUpdateCheck = false;
    #ifndef PLATFORM_ESP8266
    bool _pendingOTAUpdate = false;
    #endif
    unsigned long _pendingActionTime = 0;

    void setupRoutes();
    void handleStatus(AsyncWebServerRequest* request);
    void handleStatusLite(AsyncWebServerRequest* request);
    void handleSaveWifi(AsyncWebServerRequest* request);
    void handleSaveMqtt(AsyncWebServerRequest* request);
    void handleFanControl(AsyncWebServerRequest* request);
    void handleReset(AsyncWebServerRequest* request);
    void handleSavePasswords(AsyncWebServerRequest* request);
    void handleGetPasswords(AsyncWebServerRequest* request);
    void handleGetNightMode(AsyncWebServerRequest* request);
    void handleSaveNightMode(AsyncWebServerRequest* request);

    // Hardware diagnostics
    void handleDiagnostic(AsyncWebServerRequest* request);
    void handleDiagnosticLed(AsyncWebServerRequest* request);
    void handleDiagnosticFan(AsyncWebServerRequest* request);
    void handleDiagnosticButtons(AsyncWebServerRequest* request);

    // Update checker
    void handleUpdateCheck(AsyncWebServerRequest* request);
    void handleUpdateStatus(AsyncWebServerRequest* request);
    #ifndef PLATFORM_ESP8266
    void handleStartUpdate(AsyncWebServerRequest* request);
    #endif
};

extern WebServer webServer;

#endif // WEB_SERVER_H
