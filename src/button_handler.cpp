#include "button_handler.h"
#include "config.h"

ButtonHandler buttonHandler;

void ButtonHandler::begin() {
#ifdef PLATFORM_ESP8266
    // GPIO16 needs special handling - no internal pullup
    pinMode(BUTTON_FRONT_PIN, INPUT);

    // GPIO3 (RX) - configure as input with pullup
    // Note: This disables serial RX, but we can still TX for debugging
    pinMode(BUTTON_REAR_PIN, INPUT_PULLUP);

    Serial.println("[BTN] Button handler initialized");
    Serial.println("[BTN] Front: GPIO16, Rear: GPIO3");
#endif
}

void ButtonHandler::loop() {
#ifdef PLATFORM_ESP8266
    handleButton(BUTTON_FRONT_PIN, _frontLastState, _frontPressTime,
                 _frontLongPressFired, _frontCallback);
    handleButton(BUTTON_REAR_PIN, _rearLastState, _rearPressTime,
                 _rearLongPressFired, _rearCallback);
#endif
}

void ButtonHandler::onFrontButton(ButtonCallback callback) {
#ifdef PLATFORM_ESP8266
    _frontCallback = callback;
#endif
}

void ButtonHandler::onRearButton(ButtonCallback callback) {
#ifdef PLATFORM_ESP8266
    _rearCallback = callback;
#endif
}

bool ButtonHandler::isFrontPressed() {
#ifdef PLATFORM_ESP8266
    return digitalRead(BUTTON_FRONT_PIN) == LOW;
#else
    return false;
#endif
}

bool ButtonHandler::isRearPressed() {
#ifdef PLATFORM_ESP8266
    return digitalRead(BUTTON_REAR_PIN) == LOW;
#else
    return false;
#endif
}

#ifdef PLATFORM_ESP8266
void ButtonHandler::handleButton(uint8_t pin, bool& lastState, unsigned long& pressTime,
                                  bool& longPressFired, ButtonCallback callback) {
    bool currentState = digitalRead(pin);
    unsigned long now = millis();

    // Button pressed (transition HIGH -> LOW)
    if (lastState == HIGH && currentState == LOW) {
        pressTime = now;
        longPressFired = false;
    }

    // Button held down - check for long press
    if (currentState == LOW && !longPressFired) {
        if (now - pressTime >= BUTTON_LONG_PRESS_MS) {
            longPressFired = true;
            if (callback) {
                callback(ButtonEvent::LONG_PRESS);
            }
            Serial.printf("[BTN] GPIO%d long press\n", pin);
        }
    }

    // Button released (transition LOW -> HIGH)
    if (lastState == LOW && currentState == HIGH) {
        // Only fire short press if long press wasn't triggered
        if (!longPressFired && (now - pressTime >= BUTTON_DEBOUNCE_MS)) {
            if (callback) {
                callback(ButtonEvent::SHORT_PRESS);
            }
            Serial.printf("[BTN] GPIO%d short press\n", pin);
        }
    }

    lastState = currentState;
}
#endif
