#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

#ifdef PLATFORM_ESP8266
    // Use NeoPixelBus for ESP8266 - more stable on GPIO15 with WiFi
    #include <NeoPixelBus.h>
#else
    // Use FastLED for ESP32
    #include <FastLED.h>
#endif

enum class LedMode {
    OFF,
    ON,
    BLINK_FAST,     // WiFi connecting
    BLINK_SLOW,     // AP mode
    PULSE,          // Timer active
    BREATHE_SLOW,   // Timer + Interval combined (very slow breath)
    OTA             // OTA update in progress
};

class LedController {
public:
    ~LedController();
    void begin();
    void loop();

    void setMode(LedMode mode);
    LedMode getMode();

    void on();
    void off();

    // Color control
    void setColor(uint32_t color);
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    // Status shortcuts
    void showConnected();       // Green - WiFi connected, idle
    void showConnecting();      // Cyan blinking - WiFi connecting
    void showAPMode();          // Orange blinking - AP mode
    void showFanRunning();      // Green - Fan active
    void showIntervalMode();    // Purple - Interval mode active
    void showOTA();             // Purple fast blink - OTA update
    void showError();           // Red - Error

    // Brightness control (for night mode)
    void setBrightness(uint8_t percent);
    uint8_t getBrightness();

private:
    LedMode _mode = LedMode::OFF;
    unsigned long _lastToggle = 0;
    bool _ledState = false;
    uint8_t _pulseValue = 0;
    bool _pulseDirection = true;

    uint32_t _currentColor = LED_COLOR_BLUE;
    uint8_t _brightness = 255;
    uint8_t _r = 0, _g = 0, _b = 0;  // Current RGB values
    bool _needsUpdate = true;  // Flag to force LED update

#ifdef PLATFORM_ESP8266
    // NeoPixelBus for ESP8266
    NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod>* _strip = nullptr;
#else
    // FastLED for ESP32
    CRGB _leds[NUM_LEDS];
    CRGB _lastShownColor = CRGB::Black;
#endif

    void updateLed();
    void showLed();  // Actually send data to LED
};

extern LedController ledController;

#endif // LED_CONTROLLER_H
