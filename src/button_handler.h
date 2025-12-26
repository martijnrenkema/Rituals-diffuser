#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "config.h"

enum class ButtonEvent {
    NONE,
    SHORT_PRESS,
    LONG_PRESS
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
#ifdef PLATFORM_ESP8266
    // Front button (Connect)
    bool _frontLastState = HIGH;
    unsigned long _frontPressTime = 0;
    bool _frontLongPressFired = false;
    ButtonCallback _frontCallback = nullptr;

    // Rear button
    bool _rearLastState = HIGH;
    unsigned long _rearPressTime = 0;
    bool _rearLongPressFired = false;
    ButtonCallback _rearCallback = nullptr;

    void handleButton(uint8_t pin, bool& lastState, unsigned long& pressTime,
                      bool& longPressFired, ButtonCallback callback);
#endif
};

extern ButtonHandler buttonHandler;

#endif // BUTTON_HANDLER_H
