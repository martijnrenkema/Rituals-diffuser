#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

#ifdef PLATFORM_ESP8266
    #define FASTLED_ESP8266_RAW_PIN_ORDER
    #include <FastLED.h>
#endif

enum class LedMode {
    OFF,
    ON,
    BLINK_FAST,     // WiFi connecting
    BLINK_SLOW,     // AP mode
    PULSE,          // Timer active
    OTA             // OTA update in progress
};

class LedController {
public:
    void begin();
    void loop();

    void setMode(LedMode mode);
    LedMode getMode();

    void on();
    void off();

    // Color control (ESP8266 with WS2812)
    void setColor(uint32_t color);
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    // Status shortcuts
    void showConnected();       // Blue - WiFi connected, idle
    void showConnecting();      // Cyan blinking - WiFi connecting
    void showAPMode();          // Orange blinking - AP mode
    void showFanRunning();      // Green - Fan active
    void showOTA();             // Purple fast blink - OTA update
    void showError();           // Red - Error

private:
    LedMode _mode = LedMode::OFF;
    unsigned long _lastToggle = 0;
    bool _ledState = false;
    uint8_t _pulseValue = 0;
    bool _pulseDirection = true;

#ifdef PLATFORM_ESP8266
    CRGB _leds[NUM_LEDS];
    uint32_t _currentColor = LED_COLOR_BLUE;
    uint8_t _brightness = 255;
#endif

    void updateLed();
};

extern LedController ledController;

#endif // LED_CONTROLLER_H
