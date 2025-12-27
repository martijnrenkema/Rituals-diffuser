#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebServer {
public:
    void begin();
    void stop();
    void loop();  // For WebSocket cleanup

    // Callback for settings changes
    typedef void (*SettingsCallback)();
    void onSettingsChanged(SettingsCallback callback);

    // WebSocket broadcast
    void broadcastState();

private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    SettingsCallback _settingsCallback = nullptr;
    String _sessionToken = "";
    unsigned long _lastBroadcast = 0;

    void setupRoutes();
    void generateSessionToken();
    bool checkAuth(AsyncWebServerRequest* request);
    void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
    void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                         AwsEventType type, void *arg, uint8_t *data, size_t len);

    void handleRoot(AsyncWebServerRequest* request);
    void handleStatus(AsyncWebServerRequest* request);
    void handleSaveWifi(AsyncWebServerRequest* request);
    void handleSaveMqtt(AsyncWebServerRequest* request);
    void handleFanControl(AsyncWebServerRequest* request);
    void handleReset(AsyncWebServerRequest* request);
    void handleSavePasswords(AsyncWebServerRequest* request);
    void handleGetPasswords(AsyncWebServerRequest* request);
    void handleGetRFID(AsyncWebServerRequest* request);
    void handleRFIDAction(AsyncWebServerRequest* request);
    void handleGetNightMode(AsyncWebServerRequest* request);
    void handleSaveNightMode(AsyncWebServerRequest* request);
    void handleBackup(AsyncWebServerRequest* request);
    void handleRestore(AsyncWebServerRequest* request, uint8_t *data, size_t len);
};

extern WebServer webServer;

#endif // WEB_SERVER_H
