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
    // Update uptime counter every second
    unsigned long now = millis();
    if (now - _lastUptimeUpdate >= 1000) {
        _uptimeSeconds += (now - _lastUptimeUpdate) / 1000;
        _lastUptimeUpdate = now - (now - _lastUptimeUpdate) % 1000;
    }

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
            _publishState = MqttPublishState::DISC_UPTIME;
            break;

        case MqttPublishState::DISC_UPTIME:
            publishUptimeSensorDiscovery();
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
                String preset = "Cont";
                if (fanController.isTimerActive()) {
                    uint16_t remaining = fanController.getRemainingMinutes();
                    if (remaining <= 30) preset = "30m";
                    else if (remaining <= 60) preset = "60m";
                    else if (remaining <= 90) preset = "90m";
                    else preset = "120m";
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
            _publishState = MqttPublishState::STATE_UPDATE;
            break;

        case MqttPublishState::STATE_UPDATE:
            _mqttClient.publish((base + "/update_available").c_str(), updateChecker.isUpdateAvailable() ? "ON" : "OFF", true);
            // Only publish latest_version if we have a valid value (not empty)
            {
                const char* latestVer = updateChecker.getLatestVersion();
                if (latestVer && latestVer[0] != '\0') {
                    _mqttClient.publish((base + "/latest_version").c_str(), latestVer, true);
                }
            }
            _mqttClient.publish((base + "/current_version").c_str(), updateChecker.getCurrentVersion(), true);
            _publishState = MqttPublishState::STATE_SCENT;
            break;

        case MqttPublishState::STATE_SCENT:
            #if defined(RC522_ENABLED)
            {
                // Publish scent name
                String scent = rfidIsCartridgePresent() ? rfidGetLastScent() : "No cartridge";
                _mqttClient.publish((base + "/scent").c_str(), scent.c_str(), true);
                // Publish cartridge present state
                _mqttClient.publish((base + "/cartridge_present").c_str(), rfidIsCartridgePresent() ? "ON" : "OFF", true);
            }
            #endif
            _publishState = MqttPublishState::STATE_UPTIME;
            break;

        case MqttPublishState::STATE_UPTIME:
            _mqttClient.publish((base + "/uptime").c_str(), String(_uptimeSeconds).c_str(), true);
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
    String b = getBaseTopic();
    String id = "rd_" + _deviceId;

    // Compact JSON payload - keep under 640 bytes for ESP8266
    // Using shorter preset names to save ~30 bytes
    String p = "{\"name\":\"Diffuser\",";
    p += "\"uniq_id\":\"" + id + "\",";
    p += "\"stat_t\":\"" + b + "/fan/state\",";
    p += "\"cmd_t\":\"" + b + "/fan/set\",";
    p += "\"pct_stat_t\":\"" + b + "/fan/speed\",";
    p += "\"pct_cmd_t\":\"" + b + "/fan/speed/set\",";
    p += "\"pr_mode_stat_t\":\"" + b + "/fan/preset\",";
    p += "\"pr_mode_cmd_t\":\"" + b + "/fan/preset/set\",";
    p += "\"pr_modes\":[\"30m\",\"60m\",\"90m\",\"120m\",\"Cont\"],";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"spd_rng_min\":1,\"spd_rng_max\":100,";
    p += "\"dev\":{\"ids\":[\"rituals_" + _deviceId + "\"],";
    p += "\"name\":\"Rituals Diffuser\",\"mf\":\"Rituals\",\"mdl\":\"Genie 2.0\"}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/fan/rd_" + _deviceId + "/config";

    Serial.printf("[MQTT] Fan discovery: %d bytes\n", p.length());
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Fan discovery publish FAILED - buffer too small?");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Interval switch discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Interval on time discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Interval off time discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Remaining time sensor discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] RPM sensor discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] WiFi sensor discovery publish FAILED");
    }
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
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Total runtime sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishUpdateAvailableBinarySensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Update Available\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_upd\",";
    p += "\"stat_t\":\"" + b + "/update_available\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"dev_cla\":\"update\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/binary_sensor/rd_" + _deviceId + "_upd/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Update available sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishLatestVersionSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Latest Version\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_latver\",";
    p += "\"stat_t\":\"" + b + "/latest_version\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"ic\":\"mdi:package-up\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_latver/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Latest version sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishCurrentVersionSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Firmware Version\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_curver\",";
    p += "\"stat_t\":\"" + b + "/current_version\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"ic\":\"mdi:chip\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_curver/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Current version sensor discovery publish FAILED");
    }
}

#if defined(RC522_ENABLED)
void MQTTHandler::publishScentSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Scent Cartridge\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_scent\",";
    p += "\"stat_t\":\"" + b + "/scent\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"ic\":\"mdi:spray\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_scent/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Scent sensor discovery publish FAILED");
    }
}

void MQTTHandler::publishCartridgeBinarySensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Cartridge Present\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_cartridge\",";
    p += "\"stat_t\":\"" + b + "/cartridge_present\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"dev_cla\":\"presence\",";
    p += "\"ic\":\"mdi:tag-outline\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/binary_sensor/rd_" + _deviceId + "_cartridge/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Cartridge binary sensor discovery publish FAILED");
    }
}
#else
// Empty stubs for non-RFID builds
void MQTTHandler::publishScentSensorDiscovery() {}
void MQTTHandler::publishCartridgeBinarySensorDiscovery() {}
#endif

void MQTTHandler::publishUptimeSensorDiscovery() {
    String b = getBaseTopic();
    String devId = "rituals_" + _deviceId;

    String p = "{\"name\":\"Uptime\",";
    p += "\"uniq_id\":\"rd_" + _deviceId + "_uptime\",";
    p += "\"stat_t\":\"" + b + "/uptime\",";
    p += "\"avty_t\":\"" + b + "/availability\",";
    p += "\"unit_of_meas\":\"s\",\"dev_cla\":\"duration\",";
    p += "\"stat_cla\":\"total_increasing\",";
    p += "\"ic\":\"mdi:clock-start\",";
    p += "\"ent_cat\":\"diagnostic\",";
    p += "\"dev\":{\"ids\":[\"" + devId + "\"]}}";

    String topic = String(MQTT_DISCOVERY_PREFIX) + "/sensor/rd_" + _deviceId + "_uptime/config";
    if (!_mqttClient.publish(topic.c_str(), p.c_str(), true)) {
        Serial.println("[MQTT] Uptime sensor discovery publish FAILED");
    }
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
    // RFID entities
    _mqttClient.publish((pre + "/sensor/" + id + "_scent/config").c_str(), "", true);
    _mqttClient.publish((pre + "/binary_sensor/" + id + "_cartridge/config").c_str(), "", true);
    // Uptime
    _mqttClient.publish((pre + "/sensor/" + id + "_uptime/config").c_str(), "", true);

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
