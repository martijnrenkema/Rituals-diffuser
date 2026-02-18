#include "mqtt_handler.h"
#include "config.h"
#include "fan_controller.h"
#include "wifi_manager.h"
#include "storage.h"
#include "logger.h"
#include "update_checker.h"
#include <ArduinoJson.h>

// RFID support for all platforms with RC522_ENABLED
#if defined(RC522_ENABLED)
#include "rfid_handler.h"
#endif

// WiFi library is included via mqtt_handler.h

// External function from main.cpp for LED priority system
extern void updateLedStatus();

MQTTHandler mqttHandler;
MQTTHandler* MQTTHandler::_instance = nullptr;

// Shared buffers for MQTT payload/topic construction (avoids heap fragmentation)
// Only used in publish functions which are called sequentially via state machine
// Fan discovery is largest payload (~614 bytes with 12-char MAC ID)
static char _mqttBuf[768];
static char _mqttTopic[96];

void MQTTHandler::begin() {
    _instance = this;

    // Set socket timeout to prevent blocking for 15+ seconds when broker is offline
    // This limits the connect() blocking time to ~3 seconds
    _wifiClient.setTimeout(3000);

    _mqttClient.setClient(_wifiClient);
    _mqttClient.setCallback(mqttCallback);
    _mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    _mqttClient.setSocketTimeout(3);  // 3 second socket timeout for PubSubClient operations
    // ESP8266 has limited RAM, but fan discovery needs ~613 bytes + MQTT header (~50 bytes)
    // Total MQTT packet: header + topic length + topic + payload = ~663 bytes
    #ifdef PLATFORM_ESP8266
    _mqttClient.setBufferSize(768);
    #else
    _mqttClient.setBufferSize(1536);  // Larger buffer for discovery payloads
    #endif

    // Generate unique device ID from MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[13];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _deviceId = id;

    Serial.println("[MQTT] Handler initialized");
}

void MQTTHandler::loop() {
    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnect >= MQTT_RECONNECT_INTERVAL) {
            _lastReconnect = now;
            if (_host.length() > 0 && wifiManager.isConnected()) {
                Serial.println("[MQTT] Attempting connection...");
                String clientId = "rituals-" + _deviceId;

                // Build LWT topic
                char base[48];
                snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, _deviceId.c_str());
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/availability", base);

                if (_mqttClient.connect(clientId.c_str(), _user.c_str(), _password.c_str(),
                                        _mqttTopic, 0, true, "offline")) {
                    Serial.println("[MQTT] Connected");
                    logger.infof("MQTT connected to %s:%d", _host.c_str(), _port);

                    publishAvailability(true);

                    // Start discovery state machine (non-blocking)
                    if (!_discoveryPublished) {
                        _publishState = MqttPublishState::DISC_FAN;
                        _lastPublishStep = millis();
                        Serial.println("[MQTT] Starting discovery publish...");
                    } else {
                        // Just publish state
                        _publishState = MqttPublishState::STATE_FAN;
                        _lastPublishStep = millis();
                    }

                    // Subscribe to command topics using shared buffer
                    const char* subSuffixes[] = {
                        "/fan/set", "/fan/speed/set", "/fan/preset/set",
                        "/interval/set", "/interval_on/set", "/interval_off/set"
                    };
                    for (size_t i = 0; i < sizeof(subSuffixes) / sizeof(subSuffixes[0]); i++) {
                        snprintf(_mqttTopic, sizeof(_mqttTopic), "%s%s", base, subSuffixes[i]);
                        _mqttClient.subscribe(_mqttTopic);
                    }
                } else {
                    Serial.printf("[MQTT] Connection failed, rc=%d\n", _mqttClient.state());
                    logger.errorf("MQTT connection failed (rc=%d)", _mqttClient.state());
                }
            }
        }
    } else {
        _mqttClient.loop();

        // Process non-blocking publish state machine
        processPublishStateMachine();

        // Handle state publish requests (interrupt-safe flag check)
        // Check flag inside critical section to prevent race condition
        noInterrupts();
        bool shouldPublish = _statePublishPending && _publishState == MqttPublishState::IDLE;
        if (shouldPublish) {
            _statePublishPending = false;
        }
        interrupts();
        if (shouldPublish) {
            _publishState = MqttPublishState::STATE_FAN;
            _lastPublishStep = millis();
        }

        // Publish state periodically
        unsigned long now = millis();
        if (now - _lastStatePublish >= 30000 && _publishState == MqttPublishState::IDLE) {
            _publishState = MqttPublishState::STATE_FAN;
            _lastPublishStep = millis();
            _lastStatePublish = now;
        }
    }
}

void MQTTHandler::processPublishStateMachine() {
    if (_publishState == MqttPublishState::IDLE) return;
    if (!_mqttClient.connected()) {
        _publishState = MqttPublishState::IDLE;
        return;
    }

    unsigned long now = millis();
    if (now - _lastPublishStep < PUBLISH_STEP_DELAY) return;

    _lastPublishStep = now;

    // Build base topic into stack buffer (avoids String allocation every step)
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, _deviceId.c_str());

    switch (_publishState) {
        // Discovery states
        case MqttPublishState::DISC_FAN:
            publishFanDiscovery();
            _publishState = MqttPublishState::DISC_INTERVAL_SWITCH;
            break;

        case MqttPublishState::DISC_INTERVAL_SWITCH:
            publishIntervalSwitchDiscovery();
            _publishState = MqttPublishState::DISC_INTERVAL_ON;
            break;

        case MqttPublishState::DISC_INTERVAL_ON:
            publishIntervalOnTimeDiscovery();
            _publishState = MqttPublishState::DISC_INTERVAL_OFF;
            break;

        case MqttPublishState::DISC_INTERVAL_OFF:
            publishIntervalOffTimeDiscovery();
            _publishState = MqttPublishState::DISC_REMAINING;
            break;

        case MqttPublishState::DISC_REMAINING:
            publishRemainingTimeSensorDiscovery();
            _publishState = MqttPublishState::DISC_RPM;
            break;

        case MqttPublishState::DISC_RPM:
            publishRPMSensorDiscovery();
            _publishState = MqttPublishState::DISC_WIFI;
            break;

        case MqttPublishState::DISC_WIFI:
            publishWiFiSensorDiscovery();
            _publishState = MqttPublishState::DISC_RUNTIME;
            break;

        case MqttPublishState::DISC_RUNTIME:
            publishTotalRuntimeSensorDiscovery();
            _publishState = MqttPublishState::DISC_UPDATE_AVAILABLE;
            break;

        case MqttPublishState::DISC_UPDATE_AVAILABLE:
            publishUpdateAvailableBinarySensorDiscovery();
            _publishState = MqttPublishState::DISC_LATEST_VERSION;
            break;

        case MqttPublishState::DISC_LATEST_VERSION:
            publishLatestVersionSensorDiscovery();
            _publishState = MqttPublishState::DISC_CURRENT_VERSION;
            break;

        case MqttPublishState::DISC_CURRENT_VERSION:
            publishCurrentVersionSensorDiscovery();
            _publishState = MqttPublishState::DISC_SCENT;
            break;

        case MqttPublishState::DISC_SCENT:
            #if defined(RC522_ENABLED)
            publishScentSensorDiscovery();
            #endif
            _publishState = MqttPublishState::DISC_CARTRIDGE;
            break;

        case MqttPublishState::DISC_CARTRIDGE:
            #if defined(RC522_ENABLED)
            publishCartridgeBinarySensorDiscovery();
            #endif
            _publishState = MqttPublishState::DISC_DONE;
            break;

        case MqttPublishState::DISC_DONE:
            Serial.println("[MQTT] Discovery published");
            _discoveryPublished = true;
            // Continue to state publish
            _publishState = MqttPublishState::STATE_FAN;
            break;

        // State publish states - use snprintf to avoid String heap allocations
        case MqttPublishState::STATE_FAN:
            snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/fan/state", base);
            _mqttClient.publish(_mqttTopic, fanController.isOn() ? "ON" : "OFF", true);
            _publishState = MqttPublishState::STATE_SPEED;
            break;

        case MqttPublishState::STATE_SPEED:
            {
                char val[8];
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/fan/speed", base);
                snprintf(val, sizeof(val), "%d", fanController.getSpeed());
                _mqttClient.publish(_mqttTopic, val, true);
            }
            _publishState = MqttPublishState::STATE_PRESET;
            break;

        case MqttPublishState::STATE_PRESET:
            {
                const char* preset = "Cont";
                if (fanController.isTimerActive()) {
                    uint16_t remaining = fanController.getRemainingMinutes();
                    if (remaining <= 30) preset = "30m";
                    else if (remaining <= 60) preset = "60m";
                    else if (remaining <= 90) preset = "90m";
                    else preset = "120m";
                }
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/fan/preset", base);
                _mqttClient.publish(_mqttTopic, preset, true);
            }
            _publishState = MqttPublishState::STATE_INTERVAL;
            break;

        case MqttPublishState::STATE_INTERVAL:
            snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/interval/state", base);
            _mqttClient.publish(_mqttTopic, fanController.isIntervalMode() ? "ON" : "OFF", true);
            _publishState = MqttPublishState::STATE_INTERVAL_TIMES;
            break;

        case MqttPublishState::STATE_INTERVAL_TIMES:
            {
                char val[8];
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/interval_on/state", base);
                snprintf(val, sizeof(val), "%d", fanController.getIntervalOnTime());
                _mqttClient.publish(_mqttTopic, val, true);
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/interval_off/state", base);
                snprintf(val, sizeof(val), "%d", fanController.getIntervalOffTime());
                _mqttClient.publish(_mqttTopic, val, true);
            }
            _publishState = MqttPublishState::STATE_REMAINING;
            break;

        case MqttPublishState::STATE_REMAINING:
            {
                char val[8];
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/remaining_time", base);
                snprintf(val, sizeof(val), "%u", fanController.getRemainingMinutes());
                _mqttClient.publish(_mqttTopic, val, true);
            }
            _publishState = MqttPublishState::STATE_RPM_WIFI;
            break;

        case MqttPublishState::STATE_RPM_WIFI:
            {
                char val[8];
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/rpm", base);
                snprintf(val, sizeof(val), "%u", fanController.getRPM());
                _mqttClient.publish(_mqttTopic, val, true);
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/wifi_signal", base);
                snprintf(val, sizeof(val), "%d", wifiManager.getRSSI());
                _mqttClient.publish(_mqttTopic, val, true);
            }
            _publishState = MqttPublishState::STATE_RUNTIME;
            break;

        case MqttPublishState::STATE_RUNTIME:
            {
                float totalHours = fanController.getTotalRuntimeMinutes() / 60.0;
                char val[10];
                snprintf(val, sizeof(val), "%.1f", totalHours);
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/total_runtime", base);
                _mqttClient.publish(_mqttTopic, val, true);
            }
            _publishState = MqttPublishState::STATE_UPDATE;
            break;

        case MqttPublishState::STATE_UPDATE:
            snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/update_available", base);
            _mqttClient.publish(_mqttTopic, updateChecker.isUpdateAvailable() ? "ON" : "OFF", true);
            // Only publish latest_version if we have a valid value (not empty)
            {
                const char* latestVer = updateChecker.getLatestVersion();
                if (latestVer && latestVer[0] != '\0') {
                    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/latest_version", base);
                    _mqttClient.publish(_mqttTopic, latestVer, true);
                }
            }
            snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/current_version", base);
            _mqttClient.publish(_mqttTopic, updateChecker.getCurrentVersion(), true);
            _publishState = MqttPublishState::STATE_SCENT;
            break;

        case MqttPublishState::STATE_SCENT:
            #if defined(RC522_ENABLED)
            {
                // Publish scent name
                String scent = rfidIsCartridgePresent() ? rfidGetLastScent() : "No cartridge";
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/scent", base);
                _mqttClient.publish(_mqttTopic, scent.c_str(), true);
                // Publish cartridge present state
                snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/cartridge_present", base);
                _mqttClient.publish(_mqttTopic, rfidIsCartridgePresent() ? "ON" : "OFF", true);
            }
            #endif
            _publishState = MqttPublishState::STATE_DONE;
            break;

        case MqttPublishState::STATE_DONE:
            _publishState = MqttPublishState::IDLE;
            break;

        default:
            _publishState = MqttPublishState::IDLE;
            break;
    }

    // Give system time after each publish
    _mqttClient.loop();
}

void MQTTHandler::connect(const char* host, uint16_t port, const char* user, const char* password) {
    _host = host;
    _port = port;
    _user = user;
    _password = password;

    _mqttClient.setServer(host, port);
    _discoveryPublished = false;
    _lastReconnect = 0; // Force immediate connection attempt

    Serial.printf("[MQTT] Configured: %s:%d\n", host, port);
}

void MQTTHandler::disconnect() {
    if (_mqttClient.connected()) {
        publishAvailability(false);
        _mqttClient.disconnect();
    }
}

bool MQTTHandler::isConnected() {
    return _mqttClient.connected();
}

void MQTTHandler::mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Use bounded buffer to prevent stack overflow from large payloads
    char message[256];
    if (length >= sizeof(message)) {
        Serial.println("[MQTT] Message too large, ignoring");
        return;
    }
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("[MQTT] Received: %s = %s\n", topic, message);

    if (_instance) {
        _instance->handleMessage(topic, message);
    }
}

void MQTTHandler::handleMessage(const char* topic, const char* payload) {
    String t = topic;
    String p = payload;

    if (t.endsWith("/fan/set")) {
        // ON/OFF command
        if (p == "ON") {
            fanController.turnOn();
        } else if (p == "OFF") {
            fanController.turnOff();
        }
    } else if (t.endsWith("/fan/speed/set")) {
        // Speed percentage - validate input is actually numeric
        int speed = p.toInt();
        // toInt() returns 0 for non-numeric strings, only accept if payload was "0" or valid number
        bool isValidNumber = (speed > 0) || (p == "0");
        if (isValidNumber) {
            fanController.setSpeed(speed);
            if (speed > 0 && !fanController.isOn()) {
                fanController.turnOn();
            }
        } else {
            Serial.printf("[MQTT] Invalid speed value: %s\n", p.c_str());
        }
    } else if (t.endsWith("/fan/preset/set")) {
        // Timer preset (short names to save MQTT buffer space)
        if (p == "30m") {
            fanController.setTimer(30);
        } else if (p == "60m") {
            fanController.setTimer(60);
        } else if (p == "90m") {
            fanController.setTimer(90);
        } else if (p == "120m") {
            fanController.setTimer(120);
        } else if (p == "Cont") {
            fanController.cancelTimer();
            if (!fanController.isOn()) fanController.turnOn();
        }
        updateLedStatus();
    } else if (t.endsWith("/interval/set")) {
        // Interval mode switch
        fanController.setIntervalMode(p == "ON");
        updateLedStatus();
    } else if (t.endsWith("/interval_on/set")) {
        // Interval on time - validate input
        int newOnTime = p.toInt();
        if (newOnTime > 0) {
            fanController.setIntervalTimes(newOnTime, fanController.getIntervalOffTime());
            // Persist to storage so settings survive reboot
            storage.setIntervalMode(fanController.isIntervalMode(), newOnTime, fanController.getIntervalOffTime());
        }
    } else if (t.endsWith("/interval_off/set")) {
        // Interval off time - validate input
        int newOffTime = p.toInt();
        if (newOffTime > 0) {
            fanController.setIntervalTimes(fanController.getIntervalOnTime(), newOffTime);
            // Persist to storage so settings survive reboot
            storage.setIntervalMode(fanController.isIntervalMode(), fanController.getIntervalOnTime(), newOffTime);
        }
    }

    // Request state publish (non-blocking)
    requestStatePublish();

    // Notify callback
    if (_commandCallback) {
        _commandCallback(topic, payload);
    }
}

void MQTTHandler::onCommand(CommandCallback callback) {
    _commandCallback = callback;
}

String MQTTHandler::getBaseTopic() {
    // Include device ID for unique topics when multiple diffusers are present
    return String(MQTT_TOPIC_PREFIX) + "_" + _deviceId;
}

String MQTTHandler::getDeviceJson() {
    StaticJsonDocument<512> device;
    device["identifiers"][0] = "rituals_diffuser_" + _deviceId;
    device["name"] = "Rituals Diffuser";
    device["model"] = "Perfume Genie 2.0";
    device["manufacturer"] = "Rituals (Custom FW)";
    device["sw_version"] = FIRMWARE_VERSION;

    String output;
    serializeJson(device, output);
    return output;
}

void MQTTHandler::publishDiscovery() {
    // Start the discovery state machine (non-blocking)
    if (_publishState == MqttPublishState::IDLE) {
        _publishState = MqttPublishState::DISC_FAN;
        _lastPublishStep = millis();
        Serial.println("[MQTT] Publishing Home Assistant discovery...");
    }
}

void MQTTHandler::publishFanDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/fan/rd_%s/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Diffuser\","
        "\"uniq_id\":\"rd_%s\","
        "\"stat_t\":\"%s/fan/state\","
        "\"cmd_t\":\"%s/fan/set\","
        "\"pct_stat_t\":\"%s/fan/speed\","
        "\"pct_cmd_t\":\"%s/fan/speed/set\","
        "\"pr_mode_stat_t\":\"%s/fan/preset\","
        "\"pr_mode_cmd_t\":\"%s/fan/preset/set\","
        "\"pr_modes\":[\"30m\",\"60m\",\"90m\",\"120m\",\"Cont\"],"
        "\"avty_t\":\"%s/availability\","
        "\"spd_rng_min\":1,\"spd_rng_max\":100,"
        "\"dev\":{\"ids\":[\"rituals_%s\"],"
        "\"name\":\"Rituals Diffuser\",\"mf\":\"Rituals\",\"mdl\":\"Genie 2.0\"}}",
        id, base, base, base, base, base, base, base, id);

    Serial.printf("[MQTT] Fan discovery: %d bytes\n", (int)strlen(_mqttBuf));
    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Fan discovery publish FAILED - buffer too small?");
    }
}

void MQTTHandler::publishIntervalSwitchDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/switch/rd_%s_int/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Interval Mode\","
        "\"uniq_id\":\"rd_%s_int\","
        "\"stat_t\":\"%s/interval/state\","
        "\"cmd_t\":\"%s/interval/set\","
        "\"avty_t\":\"%s/availability\","
        "\"ic\":\"mdi:timer-sand\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Interval switch discovery publish FAILED");
    }
}

void MQTTHandler::publishIntervalOnTimeDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/number/rd_%s_ion/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Interval On\","
        "\"uniq_id\":\"rd_%s_ion\","
        "\"stat_t\":\"%s/interval_on/state\","
        "\"cmd_t\":\"%s/interval_on/set\","
        "\"avty_t\":\"%s/availability\","
        "\"min\":10,\"max\":120,\"step\":5,"
        "\"unit_of_meas\":\"s\",\"ic\":\"mdi:timer\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Interval on time discovery publish FAILED");
    }
}

void MQTTHandler::publishIntervalOffTimeDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/number/rd_%s_ioff/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Interval Off\","
        "\"uniq_id\":\"rd_%s_ioff\","
        "\"stat_t\":\"%s/interval_off/state\","
        "\"cmd_t\":\"%s/interval_off/set\","
        "\"avty_t\":\"%s/availability\","
        "\"min\":10,\"max\":120,\"step\":5,"
        "\"unit_of_meas\":\"s\",\"ic\":\"mdi:timer-off\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Interval off time discovery publish FAILED");
    }
}

void MQTTHandler::publishRemainingTimeSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_rem/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Time Left\","
        "\"uniq_id\":\"rd_%s_rem\","
        "\"stat_t\":\"%s/remaining_time\","
        "\"avty_t\":\"%s/availability\","
        "\"unit_of_meas\":\"min\",\"ic\":\"mdi:clock-outline\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Remaining time sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishRPMSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_rpm/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Fan RPM\","
        "\"uniq_id\":\"rd_%s_rpm\","
        "\"stat_t\":\"%s/rpm\","
        "\"avty_t\":\"%s/availability\","
        "\"unit_of_meas\":\"RPM\",\"ic\":\"mdi:fan\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] RPM sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishWiFiSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_wifi/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"WiFi Signal\","
        "\"uniq_id\":\"rd_%s_wifi\","
        "\"stat_t\":\"%s/wifi_signal\","
        "\"avty_t\":\"%s/availability\","
        "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] WiFi sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishTotalRuntimeSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_trun/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Total Runtime\","
        "\"uniq_id\":\"rd_%s_trun\","
        "\"stat_t\":\"%s/total_runtime\","
        "\"avty_t\":\"%s/availability\","
        "\"unit_of_meas\":\"h\",\"ic\":\"mdi:clock-check\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Total runtime sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishUpdateAvailableBinarySensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/binary_sensor/rd_%s_upd/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Update Available\","
        "\"uniq_id\":\"rd_%s_upd\","
        "\"stat_t\":\"%s/update_available\","
        "\"avty_t\":\"%s/availability\","
        "\"dev_cla\":\"update\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Update available sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishLatestVersionSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_latver/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Latest Version\","
        "\"uniq_id\":\"rd_%s_latver\","
        "\"stat_t\":\"%s/latest_version\","
        "\"avty_t\":\"%s/availability\","
        "\"ic\":\"mdi:package-up\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Latest version sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishCurrentVersionSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_curver/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Firmware Version\","
        "\"uniq_id\":\"rd_%s_curver\","
        "\"stat_t\":\"%s/current_version\","
        "\"avty_t\":\"%s/availability\","
        "\"ic\":\"mdi:chip\","
        "\"ent_cat\":\"diagnostic\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Current version sensor discovery publish FAILED");
    }
}

#if defined(RC522_ENABLED)
void MQTTHandler::publishScentSensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/sensor/rd_%s_scent/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Scent Cartridge\","
        "\"uniq_id\":\"rd_%s_scent\","
        "\"stat_t\":\"%s/scent\","
        "\"avty_t\":\"%s/availability\","
        "\"ic\":\"mdi:spray\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Scent sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishCartridgeBinarySensorDiscovery() {
    const char* id = _deviceId.c_str();
    char base[48];
    snprintf(base, sizeof(base), "%s_%s", MQTT_TOPIC_PREFIX, id);

    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/binary_sensor/rd_%s_cartridge/config", MQTT_DISCOVERY_PREFIX, id);

    snprintf(_mqttBuf, sizeof(_mqttBuf),
        "{\"name\":\"Cartridge Present\","
        "\"uniq_id\":\"rd_%s_cartridge\","
        "\"stat_t\":\"%s/cartridge_present\","
        "\"avty_t\":\"%s/availability\","
        "\"dev_cla\":\"presence\","
        "\"ic\":\"mdi:tag-outline\","
        "\"dev\":{\"ids\":[\"rituals_%s\"]}}",
        id, base, base, id);

    if (!_mqttClient.publish(_mqttTopic, _mqttBuf, true)) {
        Serial.println("[MQTT] Cartridge binary sensor discovery publish FAILED");
    }
}
#else
// Empty stubs for non-RFID builds
void MQTTHandler::publishScentSensorDiscovery() {}
void MQTTHandler::publishCartridgeBinarySensorDiscovery() {}
#endif

void MQTTHandler::removeDiscovery() {
    const char* id = _deviceId.c_str();
    const char* pre = MQTT_DISCOVERY_PREFIX;

    // Remove all discovery configs by publishing empty payload
    const char* suffixes[] = {
        "fan/rd_%s/config",
        "switch/rd_%s_int/config",
        "number/rd_%s_ion/config",
        "number/rd_%s_ioff/config",
        "sensor/rd_%s_rem/config",
        "sensor/rd_%s_rpm/config",
        "sensor/rd_%s_wifi/config",
        "sensor/rd_%s_scent/config",
        "binary_sensor/rd_%s_cartridge/config"
    };

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), suffixes[i], id);
        snprintf(_mqttTopic, sizeof(_mqttTopic), "%s/%s", pre, suffix);
        _mqttClient.publish(_mqttTopic, "", true);
    }

    _discoveryPublished = false;
    Serial.println("[MQTT] Discovery removed");
}

void MQTTHandler::publishState() {
    // Start the state publish state machine (non-blocking)
    if (_publishState == MqttPublishState::IDLE) {
        _publishState = MqttPublishState::STATE_FAN;
        _lastPublishStep = millis();
    }
}

void MQTTHandler::publishAvailability(bool online) {
    snprintf(_mqttTopic, sizeof(_mqttTopic), "%s_%s/availability", MQTT_TOPIC_PREFIX, _deviceId.c_str());
    _mqttClient.publish(_mqttTopic, online ? "online" : "offline", true);
}
