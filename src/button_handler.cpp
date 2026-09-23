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
    _front.pressTime = millis();
    _rear.pressTime = millis();

    Serial.println("[BTN] Button handler initialized");
    Serial.printf("[BTN] Front (SW2): GPIO%d, Rear (SW1): GPIO%d\n", BUTTON_FRONT_PIN, BUTTON_REAR_PIN);
}

void ButtonHandler::loop() {
    handleButton(_front);
    handleButton(_rear);
}

void ButtonHandler::onFrontButton(ButtonCallback callback) {
    _front.callback = callback;
}

void ButtonHandler::onRearButton(ButtonCallback callback) {
    _rear.callback = callback;
}

bool ButtonHandler::isFrontPressed() {
    return digitalRead(BUTTON_FRONT_PIN) == LOW;
}

bool ButtonHandler::isRearPressed() {
    return digitalRead(BUTTON_REAR_PIN) == LOW;
}

void ButtonHandler::handleButton(Button& btn) {
    bool currentState = digitalRead(btn.pin);
    unsigned long now = millis();

    // Button pressed (transition HIGH -> LOW)
    if (btn.lastState == HIGH && currentState == LOW) {
        btn.pressTime = now;
        btn.longPressFired = false;
        btn.warningFired = false;
    }

    if (currentState == LOW && !btn.longPressFired) {
        unsigned long held = now - btn.pressTime;

        // Held long enough to warn (e.g. "factory reset coming")
        if (btn.warnMs > 0 && !btn.warningFired && held >= btn.warnMs) {
            btn.warningFired = true;
            Serial.printf("[BTN] GPIO%d hold warning\n", btn.pin);
            if (btn.callback) btn.callback(ButtonEvent::HOLD_WARNING);
        }

        if (held >= btn.longPressMs) {
            btn.longPressFired = true;
            Serial.printf("[BTN] GPIO%d long press\n", btn.pin);
            if (btn.callback) btn.callback(ButtonEvent::LONG_PRESS);
        }
    }

    // Button released (transition LOW -> HIGH)
    if (btn.lastState == LOW && currentState == HIGH && !btn.longPressFired) {
        if (btn.warningFired) {
            // Released during the warning phase: abort without action
            Serial.printf("[BTN] GPIO%d hold cancelled\n", btn.pin);
            if (btn.callback) btn.callback(ButtonEvent::HOLD_CANCELLED);
        } else if (now - btn.pressTime >= BUTTON_DEBOUNCE_MS) {
            Serial.printf("[BTN] GPIO%d short press\n", btn.pin);
            if (btn.callback) btn.callback(ButtonEvent::SHORT_PRESS);
        }
    }

    btn.lastState = currentState;
}
