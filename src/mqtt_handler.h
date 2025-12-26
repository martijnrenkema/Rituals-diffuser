#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include "config.h"

#ifdef PLATFORM_ESP8266
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
#endif

#include <PubSubClient.h>

class MQTTHandler {
public:
    void begin();
    void loop();

    // Connection
    void connect(const char* host, uint16_t port, const char* user, const char* password);
    void disconnect();
    bool isConnected();

    // Home Assistant Discovery
    void publishDiscovery();
    void removeDiscovery();

    // State publishing
    void publishState();
    void publishAvailability(bool online);

    // Callbacks
    typedef void (*CommandCallback)(const char* topic, const char* payload);
    void onCommand(CommandCallback callback);

private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;

    String _host;
    uint16_t _port = 1883;
    String _user;
    String _password;
    String _deviceId;

    unsigned long _lastReconnect = 0;
    unsigned long _lastStatePublish = 0;
    bool _discoveryPublished = false;

    CommandCallback _commandCallback = nullptr;

    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MQTTHandler* _instance;

    void handleMessage(const char* topic, const char* payload);
    void publishFanDiscovery();
    void publishIntervalSwitchDiscovery();
    void publishIntervalOnTimeDiscovery();
    void publishIntervalOffTimeDiscovery();
    void publishRemainingTimeSensorDiscovery();
    void publishRPMSensorDiscovery();
    void publishWiFiSensorDiscovery();

    String getBaseTopic();
    String getDeviceJson();
};

extern MQTTHandler mqttHandler;

#endif // MQTT_HANDLER_H
