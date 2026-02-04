#include "ota_handler.h"
#include "config.h"
#include "storage.h"

#ifdef FEATURE_ARDUINO_OTA

#ifdef PLATFORM_ESP8266
    #include <ArduinoOTA.h>
#else
    #include <ArduinoOTA.h>
#endif

OTAHandler otaHandler;

void OTAHandler::begin() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(storage.getOTAPassword());  // Use stored or default password

    ArduinoOTA.onStart([this]() {
        String type;
#ifdef PLATFORM_ESP8266
        if (ArduinoOTA.getCommand() == U_FLASH) {
#else
        if (ArduinoOTA.getCommand() == U_FLASH) {
#endif
            type = "firmware";
        } else {
            type = "filesystem";
        }
        Serial.println("[OTA] Start updating " + type);

        if (_startCallback) {
            _startCallback();
        }
    });

    ArduinoOTA.onEnd([this]() {
        Serial.println("\n[OTA] Update complete");

        if (_endCallback) {
            _endCallback();
        }
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        int percent = (total > 0) ? ((progress * 100) / total) : 0;
        Serial.printf("[OTA] Progress: %u%%\r", percent);

        if (_progressCallback) {
            _progressCallback(percent);
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Service started");
}

void OTAHandler::loop() {
    ArduinoOTA.handle();
}

#else // FEATURE_ARDUINO_OTA not defined

// Stub implementation when ArduinoOTA is disabled
OTAHandler otaHandler;

void OTAHandler::begin() {
    Serial.println("[OTA] ArduinoOTA disabled (using web OTA only)");
}

void OTAHandler::loop() {
    // No-op
}

#endif // FEATURE_ARDUINO_OTA

void OTAHandler::onProgress(OTACallback callback) {
    _progressCallback = callback;
}

void OTAHandler::onStart(void (*callback)()) {
    _startCallback = callback;
}

void OTAHandler::onEnd(void (*callback)()) {
    _endCallback = callback;
}
