#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebServer {
public:
    void begin();
    void stop();

    // Callback for settings changes
    typedef void (*SettingsCallback)();
    void onSettingsChanged(SettingsCallback callback);

private:
    AsyncWebServer* _server = nullptr;
    SettingsCallback _settingsCallback = nullptr;

    void setupRoutes();
    void handleRoot(AsyncWebServerRequest* request);
    void handleStatus(AsyncWebServerRequest* request);
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
};

extern WebServer webServer;

#endif // WEB_SERVER_H
