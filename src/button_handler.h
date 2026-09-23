#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "config.h"

enum class ButtonEvent {
    NONE,
    SHORT_PRESS,
    LONG_PRESS,
    HOLD_WARNING,   // Held past the warning threshold (only if a warning time is set)
    HOLD_CANCELLED  // Released after HOLD_WARNING but before LONG_PRESS
};

class ButtonHandler {
public:
    void begin();
    void loop();

    // Callbacks
    typedef void (*ButtonCallback)(ButtonEvent event);
    void onFrontButton(ButtonCallback callback);
    void onRearButton(ButtonCallback callback);

    // Direct state access
    bool isFrontPressed();
    bool isRearPressed();

private:
    struct Button {
        uint8_t pin;
        unsigned long longPressMs;
        unsigned long warnMs;       // 0 = no HOLD_WARNING / HOLD_CANCELLED events
        bool lastState;
        unsigned long pressTime;
        bool longPressFired;
        bool warningFired;
        ButtonCallback callback;
    };

    // Front button (Connect): short = fan toggle, long (3s) = AP mode
    Button _front = {BUTTON_FRONT_PIN, BUTTON_LONG_PRESS_MS, 0, HIGH, 0, false, false, nullptr};
    // Rear button: short = restart, hold 1s = warning, hold 5s = factory reset
    Button _rear = {BUTTON_REAR_PIN, BUTTON_RESET_PRESS_MS, BUTTON_RESET_WARN_MS, HIGH, 0, false, false, nullptr};

    void handleButton(Button& btn);
};

extern ButtonHandler buttonHandler;

#endif // BUTTON_HANDLER_H
