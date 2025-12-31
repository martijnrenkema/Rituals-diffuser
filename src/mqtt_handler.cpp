#include "mqtt_handler.h"
#include "config.h"
#include "fan_controller.h"
#include "wifi_manager.h"
#include "storage.h"
#include "logger.h"
#include <ArduinoJson.h>

// WiFi library is included via mqtt_handler.h

// External function from main.cpp for LED priority system
extern void updateLedStatus();

MQTTHandler mqttHandler;
MQTTHandler* MQTTHandler::_instance = nullptr;

void MQTTHandler::begin() {
    _instance = this;
    _mqttClient.setClient(_wifiClient);
    _mqttClient.setCallback(mqttCallback);
    _mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    _mqttClient.setBufferSize(1536);  // Larger buffer for discovery payloads

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

                if (_mqttClient.connect(clientId.c_str(), _user.c_str(), _password.c_str(),
                                        (getBaseTopic() + "/availability").c_str(), 0, true, "offline")) {
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

                    // Subscribe to command topics
                    _mqttClient.subscribe((getBaseTopic() + "/fan/set").c_str());
                    _mqttClient.subscribe((getBaseTopic() + "/fan/speed/set").c_str());
                    _mqttClient.subscribe((getBaseTopic() + "/fan/preset/set").c_str());
                    _mqttClient.subscribe((getBaseTopic() + "/interval/set").c_str());
                    _mqttClient.subscribe((getBaseTopic() + "/interval_on/set").c_str());
                    _mqttClient.subscribe((getBaseTopic() + "/interval_off/set").c_str());
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

        // Handle state publish requests (queued to avoid losing rapid changes)
        if (_statePublishPending > 0 && _publishState == MqttPublishState::IDLE) {
            _statePublishPending = 0;  // Clear all pending requests, we'll publish current state
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
    String base = getBaseTopic();

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
            _publishState = MqttPublishState::DISC_DONE;
            break;

        case MqttPublishState::DISC_DONE:
            Serial.println("[MQTT] Discovery published");
            _discoveryPublished = true;
            // Continue to state publish
            _publishState = MqttPublishState::STATE_FAN;
            break;

        // State publish states
        case MqttPublishState::STATE_FAN:
            _mqttClient.publish((base + "/fan/state").c_str(), fanController.isOn() ? "ON" : "OFF", true);
            _publishState = MqttPublishState::STATE_SPEED;
            break;

        case MqttPublishState::STATE_SPEED:
            _mqttClient.publish((base + "/fan/speed").c_str(), String(fanController.getSpeed()).c_str(), true);
            _publishState = MqttPublishState::STATE_PRESET;
            break;

        case MqttPublishState::STATE_PRESET:
            {
                String preset = "Continuous";
                if (fanController.isTimerActive()) {
                    uint16_t remaining = fanController.getRemainingMinutes();
                    if (remaining <= 30) preset = "30 min";
                    else if (remaining <= 60) preset = "60 min";
                    else if (remaining <= 90) preset = "90 min";
                    else preset = "120 min";
                }
                _mqttClient.publish((base + "/fan/preset").c_str(), preset.c_str(), true);
            }
            _publishState = MqttPublishState::STATE_INTERVAL;
            break;

        case MqttPublishState::STATE_INTERVAL:
            _mqttClient.publish((base + "/interval/state").c_str(), fanController.isIntervalMode() ? "ON" : "OFF", true);
            _publishState = MqttPublishState::STATE_INTERVAL_TIMES;
            break;

        case MqttPublishState::STATE_INTERVAL_TIMES:
            _mqttClient.publish((base + "/interval_on/state").c_str(), String(fanController.getIntervalOnTime()).c_str(), true);
            _mqttClient.publish((base + "/interval_off/state").c_str(), String(fanController.getIntervalOffTime()).c_str(), true);
            _publishState = MqttPublishState::STATE_REMAINING;
            break;

        case MqttPublishState::STATE_REMAINING:
            _mqttClient.publish((base + "/remaining_time").c_str(), String(fanController.getRemainingMinutes()).c_str(), true);
            _publishState = MqttPublishState::STATE_RPM_WIFI;
            break;

        case MqttPublishState::STATE_RPM_WIFI:
            _mqttClient.publish((base + "/rpm").c_str(), String(fanController.getRPM()).c_str(), true);
            _mqttClient.publish((base + "/wifi_signal").c_str(), String(wifiManager.getRSSI()).c_str(), true);
            _publishState = MqttPublishState::STATE_RUNTIME;
            break;

        case MqttPublishState::STATE_RUNTIME:
            {
                float totalHours = fanController.getTotalRuntimeMinutes() / 60.0;
                char buf[10];
                snprintf(buf, sizeof(buf), "%.1f", totalHours);
                _mqttClient.publish((base + "/total_runtime").c_str(), buf, true);
            }
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
        // Speed percentage
        int speed = p.toInt();
        fanController.setSpeed(speed);
        if (speed > 0 && !fanController.isOn()) {
            fanController.turnOn();
        }
    } else if (t.endsWith("/fan/preset/set")) {
        // Timer preset
        if (p == "30 min") {
            fanController.setTimer(30);
        } else if (p == "60 min") {
            fanController.setTimer(60);
        } else if (p == "90 min") {
            fanController.setTimer(90);
        } else if (p == "120 min") {
            fanController.setTimer(120);
        } else if (p == "Continuous") {
            fanController.cancelTimer();
            if (!fanController.isOn()) fanController.turnOn();
        }
        updateLedStatus();
    } else if (t.endsWith("/interval/set")) {
        // Interval mode switch
        fanController.setIntervalMode(p == "ON");
        updateLedStatus();
    } else if (t.endsWith("/interval_on/set")) {
        // Interval on time
        int newOnTime = p.toInt();
        fanController.setIntervalTimes(newOnTime, fanController.getIntervalOffTime());
        // Persist to storage so settings survive reboot
        storage.setIntervalMode(fanController.isIntervalMode(), newOnTime, fanController.getIntervalOffTime());
    } else if (t.endsWith("/interval_off/set")) {
        // Interval off time
        int newOffTime = p.toInt();
        fanController.setIntervalTimes(fanController.getIntervalOnTime(), newOffTime);
        // Persist to storage so settings survive reboot
        storage.setIntervalMode(fanController.isIntervalMode(), fanController.getIntervalOnTime(), newOffTime);
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
    device["sw_version"] = "1.4.0";

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
    String b = getBaseTopic();  // "rituals_diffuser"
    String id = "rd_" + _deviceId;  // Shorter ID

    // Compact JSON payload
    String p = "{\"name\":\"Diffuser\",";
    p += "\"uniq_id\":\"" + id + "\",";
    p += "\"stat_t\":\"" + b + "/fan/state\",";
    p += "\"cmd_t\":\"" + b + "/fan/set\",";
    p += "\"pct_stat_t\":\"" + b + "/fan/speed\",";
    p += "\"pct_cmd_t\":\"" + b + "/fan/speed/set\",";
    p += "\"pr_mode_stat_t\":\"" + b + "/fan/preset\",";
    p += "\"pr_mode_cmd_t\":\"" + b + "/fan/preset/set\",";
    p += "\"pr_modes\":[\"30 min\",\"60 min\",\"90 min\",\"120 min\",\"Continuous\"],";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"spd_rng_min\":1,\"spd_rng_max\":100,";
    p += "\"dev\":{\"ids\":[\"rituals_" + _deviceId + "\"],";
    p += "\"name\":\"Rituals Diffuser\",\"mf\":\"Rituals (Custom FW)\",\"mdl\":\"Perfume Genie 2.0\"}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/fan/rd_" + _deviceId + "/config";

    Serial.printf("[MQTT] Fan discovery: %d bytes\n", p.length());
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishIntervalSwitchDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Interval Mode\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_int\",";
    p += "\"stat_t\":\"" + b + "/interval/state\",";
    p += "\"cmd_t\":\"" + b + "/interval/set\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"ic\":\"mdi:timer-sand\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/switch/rd_" + _deviceId + "_int/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishIntervalOnTimeDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Interval On\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_ion\",";
    p += "\"stat_t\":\"" + b + "/interval_on/state\",";
    p += "\"cmd_t\":\"" + b + "/interval_on/set\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"min\":10,\"max\":120,\"step\":5,";
    p += "\"unit_of_meas\":\"s\",\"ic\":\"mdi:timer\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/number/rd_" + _deviceId + "_ion/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishIntervalOffTimeDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Interval Off\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_ioff\",";
    p += "\"stat_t\":\"" + b + "/interval_off/state\",";
    p += "\"cmd_t\":\"" + b + "/interval_off/set\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"min\":10,\"max\":120,\"step\":5,";
    p += "\"unit_of_meas\":\"s\",\"ic\":\"mdi:timer-off\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/number/rd_" + _deviceId + "_ioff/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishRemainingTimeSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Time Left\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_rem\",";
    p += "\"stat_t\":\"" + b + "/remaining_time\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"unit_of_meas\":\"min\",\"ic\":\"mdi:clock-outline\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_rem/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishRPMSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Fan RPM\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_rpm\",";
    p += "\"stat_t\":\"" + b + "/rpm\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"unit_of_meas\":\"RPM\",\"ic\":\"mdi:fan\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_rpm/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishWiFiSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"WiFi Signal\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_wifi\",";
    p += "\"stat_t\":\"" + b + "/wifi_signal\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_wifi/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::publishTotalRuntimeSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Total Runtime\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_trun\",";
    p += "\"stat_t\":\"" + b + "/total_runtime\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"unit_of_meas\":\"h\",\"ic\":\"mdi:clock-check\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_trun/config";
    _mqttClient.publish(topic.c_str(), p.c_str(), true);
}

void MQTTHandler::removeDiscovery() {
    String pre = String(MQTT_DISCOVERY_PREFIX);
    String id = "rd_" + _deviceId;

    _mqttClient.publish((pre + "/fan/" + id + "/config").c_str(), "", true);
    _mqttClient.publish((pre + "/switch/" + id + "_int/config").c_str(), "", true);
    _mqttClient.publish((pre + "/number/" + id + "_ion/config").c_str(), "", true);
    _mqttClient.publish((pre + "/number/" + id + "_ioff/config").c_str(), "", true);
    _mqttClient.publish((pre + "/sensor/" + id + "_rem/config").c_str(), "", true);
    _mqttClient.publish((pre + "/sensor/" + id + "_rpm/config").c_str(), "", true);
    _mqttClient.publish((pre + "/sensor/" + id + "_wifi/config").c_str(), "", true);

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
    _mqttClient.publish((getBaseTopic() + "/availability").c_str(), online ? "online" : "offline", true);
}
