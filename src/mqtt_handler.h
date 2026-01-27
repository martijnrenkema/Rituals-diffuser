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

// Non-blocking publish states
enum class MqttPublishState {
    IDLE,
    // Discovery states
    DISC_FAN,
    DISC_INTERVAL_SWITCH,
    DISC_INTERVAL_ON,
    DISC_INTERVAL_OFF,
    DISC_REMAINING,
    DISC_RPM,
    DISC_WIFI,
    DISC_RUNTIME,
    DISC_UPDATE_AVAILABLE,
    DISC_LATEST_VERSION,
    DISC_CURRENT_VERSION,
    DISC_SCENT,           // RFID scent sensor
    DISC_CARTRIDGE,       // RFID cartridge present binary sensor
    DISC_DONE,
    // State publish states
    STATE_FAN,
    STATE_SPEED,
    STATE_PRESET,
    STATE_INTERVAL,
    STATE_INTERVAL_TIMES,
    STATE_REMAINING,
    STATE_RPM_WIFI,
    STATE_RUNTIME,
    STATE_UPDATE,
    STATE_SCENT,          // RFID scent state
    STATE_DONE
};

class MQTTHandler {
public:
    void begin();
    void loop();

    // Connection
    void connect(const char* host, uint16_t port, const char* user, const char* password);
    void disconnect();
    bool isConnected();

    // Home Assistant Discovery (non-blocking, starts state machine)
    void publishDiscovery();
    void removeDiscovery();

    // State publishing (non-blocking, starts state machine)
    void publishState();
    void publishAvailability(bool online);

    // Request state publish (safe to call from any context, interrupt-safe)
    void requestStatePublish() {
        noInterrupts();
        _statePublishPending = true;
        interrupts();
    }

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
    unsigned long _lastPublishStep = 0;
    bool _discoveryPublished = false;
    volatile bool _statePublishPending = false;  // Flag for pending state publish request

    // Non-blocking state machine
    MqttPublishState _publishState = MqttPublishState::IDLE;
    static const unsigned long PUBLISH_STEP_DELAY = 50; // ms between publishes

    CommandCallback _commandCallback = nullptr;

    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MQTTHandler* _instance;

    void handleMessage(const char* topic, const char* payload);
    void processPublishStateMachine();

    void publishFanDiscovery();
    void publishIntervalSwitchDiscovery();
    void publishIntervalOnTimeDiscovery();
    void publishIntervalOffTimeDiscovery();
    void publishRemainingTimeSensorDiscovery();
    void publishRPMSensorDiscovery();
    void publishWiFiSensorDiscovery();
    void publishTotalRuntimeSensorDiscovery();
    void publishUpdateAvailableBinarySensorDiscovery();
    void publishLatestVersionSensorDiscovery();
    void publishCurrentVersionSensorDiscovery();
    void publishScentSensorDiscovery();
    void publishCartridgeBinarySensorDiscovery();

    String getBaseTopic();
    String getDeviceJson();
};

extern MQTTHandler mqttHandler;

#endif // MQTT_HANDLER_H
