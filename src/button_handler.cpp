#include "button_handler.h"
#include "config.h"

ButtonHandler buttonHandler;

void ButtonHandler::begin() {
#ifdef PLATFORM_ESP8266
    // ESP8266 GPIO16 does not support INPUT_PULLUP (only INPUT_PULLDOWN_16)
    // The Rituals Genie board has external pull-up resistors on button pins
    #if BUTTON_FRONT_PIN == 16
        pinMode(BUTTON_FRONT_PIN, INPUT);
    #else
        pinMode(BUTTON_FRONT_PIN, INPUT_PULLUP);
    #endif
    // GPIO3 (RX) works with INPUT_PULLUP
    pinMode(BUTTON_REAR_PIN, INPUT_PULLUP);
#else
    // ESP32: All GPIOs support INPUT_PULLUP
    pinMode(BUTTON_FRONT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_REAR_PIN, INPUT_PULLUP);
#endif

    // Initialize press times to prevent false long-press detection if button held during boot
    _frontPressTime = millis();
    _rearPressTime = millis();

    Serial.println("[BTN] Button handler initialized");
    Serial.printf("[BTN] Front (SW2): GPIO%d, Rear (SW1): GPIO%d\n", BUTTON_FRONT_PIN, BUTTON_REAR_PIN);
}

void ButtonHandler::loop() {
    handleButton(BUTTON_FRONT_PIN, _frontLastState, _frontPressTime,
                 _frontLongPressFired, _frontCallback);
    handleButton(BUTTON_REAR_PIN, _rearLastState, _rearPressTime,
                 _rearLongPressFired, _rearCallback);
}

void ButtonHandler::onFrontButton(ButtonCallback callback) {
    _frontCallback = callback;
}

void ButtonHandler::onRearButton(ButtonCallback callback) {
    _rearCallback = callback;
}

bool ButtonHandler::isFrontPressed() {
    return digitalRead(BUTTON_FRONT_PIN) == LOW;
}

bool ButtonHandler::isRearPressed() {
    return digitalRead(BUTTON_REAR_PIN) == LOW;
}

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
