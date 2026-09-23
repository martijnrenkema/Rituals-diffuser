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

private:
    AsyncWebServer* _server = nullptr;

    // Deferred action flags (to avoid blocking in async callbacks)
    // Use char arrays instead of String to avoid heap fragmentation
    bool _pendingWifiConnect = false;
    char _pendingWifiSsid[33];      // Max SSID length + null
    char _pendingWifiPassword[65];  // Max password length + null
    bool _pendingMqttConnect = false;
    char _pendingMqttHost[65];      // Max hostname length + null
    uint16_t _pendingMqttPort = 1883;
    char _pendingMqttUser[33];      // Max username length + null
    char _pendingMqttPassword[65];  // Max password length + null
    bool _pendingReset = false;
    bool _pendingRestart = false;
    bool _pendingUpdateCheck = false;
    #ifndef PLATFORM_ESP8266
    bool _pendingOTAUpdate = false;
    #endif
    unsigned long _pendingActionTime = 0;

    #ifndef PLATFORM_ESP8266
    // Web OTA upload state (ESP32 only, one upload at a time). Written from
    // the upload handler, acted upon in loop() so shared state is only
    // touched there.
    volatile bool _uploadActive = false;
    volatile bool _uploadFailed = false;
    volatile bool _updateBegun = false;         // Our Update.begin() succeeded and isn't finished
    AsyncWebServerRequest* volatile _uploadRequest = nullptr;  // Request owning the active upload
    volatile bool _uploadIsFilesystem = false;
    volatile bool _uploadStartPending = false;
    volatile bool _uploadAbortPending = false;
    volatile unsigned long _lastUploadActivity = 0;

    void handleUploadChunk(AsyncWebServerRequest* request, bool filesystem, size_t index,
                           uint8_t* data, size_t len, bool final);
    void handleUploadDone(AsyncWebServerRequest* request);
    void abortUpload();
    #endif

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
