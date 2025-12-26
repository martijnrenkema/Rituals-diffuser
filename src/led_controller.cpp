#include "led_controller.h"
#include "config.h"

LedController ledController;

void LedController::begin() {
#ifdef PLATFORM_ESP8266
    // Initialize FastLED for WS2812
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(_leds, NUM_LEDS);
    FastLED.setBrightness(50);  // Start at 50% brightness
    _leds[0] = CRGB::Black;
    FastLED.show();
    Serial.println("[LED] WS2812 initialized on GPIO15");
#else
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.println("[LED] Simple LED initialized");
#endif
}

void LedController::loop() {
    unsigned long now = millis();

    switch (_mode) {
        case LedMode::OFF:
#ifdef PLATFORM_ESP8266
            _leds[0] = CRGB::Black;
            FastLED.show();
#else
            digitalWrite(LED_PIN, LOW);
#endif
            break;

        case LedMode::ON:
#ifdef PLATFORM_ESP8266
            _leds[0] = _currentColor;
            FastLED.show();
#else
            digitalWrite(LED_PIN, HIGH);
#endif
            break;

        case LedMode::BLINK_FAST:
            if (now - _lastToggle >= LED_BLINK_FAST) {
                _ledState = !_ledState;
#ifdef PLATFORM_ESP8266
                _leds[0] = _ledState ? CRGB(_currentColor) : CRGB::Black;
                FastLED.show();
#else
                digitalWrite(LED_PIN, _ledState);
#endif
                _lastToggle = now;
            }
            break;

        case LedMode::BLINK_SLOW:
            if (now - _lastToggle >= LED_BLINK_SLOW) {
                _ledState = !_ledState;
#ifdef PLATFORM_ESP8266
                _leds[0] = _ledState ? CRGB(_currentColor) : CRGB::Black;
                FastLED.show();
#else
                digitalWrite(LED_PIN, _ledState);
#endif
                _lastToggle = now;
            }
            break;

        case LedMode::PULSE:
            // Smooth pulsing effect
            if (now - _lastToggle >= 10) {
                if (_pulseDirection) {
                    _pulseValue += 5;
                    if (_pulseValue >= 255) {
                        _pulseValue = 255;
                        _pulseDirection = false;
                    }
                } else {
                    _pulseValue -= 5;
                    if (_pulseValue <= 10) {
                        _pulseValue = 10;
                        _pulseDirection = true;
                    }
                }
#ifdef PLATFORM_ESP8266
                FastLED.setBrightness(_pulseValue);
                _leds[0] = _currentColor;
                FastLED.show();
#else
                analogWrite(LED_PIN, _pulseValue);
#endif
                _lastToggle = now;
            }
            break;

        case LedMode::OTA:
            // Fast alternating for OTA
            if (now - _lastToggle >= 50) {
                _ledState = !_ledState;
#ifdef PLATFORM_ESP8266
                _leds[0] = _ledState ? CRGB(LED_COLOR_PURPLE) : CRGB::Black;
                FastLED.show();
#else
                digitalWrite(LED_PIN, _ledState);
#endif
                _lastToggle = now;
            }
            break;
    }
}

void LedController::setMode(LedMode mode) {
    if (_mode != mode) {
        _mode = mode;
        _lastToggle = 0;
        _ledState = false;
        _pulseValue = 0;
        _pulseDirection = true;

#ifdef PLATFORM_ESP8266
        FastLED.setBrightness(50);  // Reset brightness
#else
        // Reset to digital output if not pulsing
        if (mode != LedMode::PULSE) {
            pinMode(LED_PIN, OUTPUT);
        }
#endif

        Serial.printf("[LED] Mode changed to %d\n", (int)mode);
    }
}

LedMode LedController::getMode() {
    return _mode;
}

void LedController::on() {
    setMode(LedMode::ON);
}

void LedController::off() {
    setMode(LedMode::OFF);
}

void LedController::setColor(uint32_t color) {
#ifdef PLATFORM_ESP8266
    _currentColor = color;
    if (_mode == LedMode::ON) {
        _leds[0] = color;
        FastLED.show();
    }
#endif
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
#ifdef PLATFORM_ESP8266
    _currentColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (_mode == LedMode::ON) {
        _leds[0] = CRGB(r, g, b);
        FastLED.show();
    }
#endif
}

void LedController::showConnected() {
    setColor(LED_COLOR_BLUE);
    setMode(LedMode::ON);
}

void LedController::showConnecting() {
    setColor(LED_COLOR_CYAN);
    setMode(LedMode::BLINK_FAST);
}

void LedController::showAPMode() {
    setColor(LED_COLOR_ORANGE);
    setMode(LedMode::BLINK_SLOW);
}

void LedController::showFanRunning() {
    setColor(LED_COLOR_GREEN);
    setMode(LedMode::ON);
}

void LedController::showOTA() {
    setColor(LED_COLOR_PURPLE);
    setMode(LedMode::OTA);
}

void LedController::showError() {
    setColor(LED_COLOR_RED);
    setMode(LedMode::BLINK_FAST);
}

void LedController::updateLed() {
#ifdef PLATFORM_ESP8266
    FastLED.show();
#endif
}

void LedController::setBrightness(uint8_t percent) {
#ifdef PLATFORM_ESP8266
    // Map percent (0-100) to FastLED brightness (0-255)
    _brightness = map(constrain(percent, 0, 100), 0, 100, 0, 255);
    FastLED.setBrightness(_brightness);
    FastLED.show();
    Serial.printf("[LED] Brightness set to %d%%\n", percent);
#endif
}

uint8_t LedController::getBrightness() {
#ifdef PLATFORM_ESP8266
    return map(_brightness, 0, 255, 0, 100);
#else
    return 100;
#endif
}
