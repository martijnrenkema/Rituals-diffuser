#include "led_controller.h"
#include "config.h"

LedController ledController;

void LedController::begin() {
#ifdef PLATFORM_ESP8266
    // Initialize FastLED for WS2812 on ESP8266
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(_leds, NUM_LEDS);
    Serial.println("[LED] WS2812 initialized on GPIO15");
#else
    // Initialize FastLED for WS2812 on ESP32
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(_leds, NUM_LEDS);
    Serial.printf("[LED] WS2812 initialized on GPIO%d\n", LED_DATA_PIN);
#endif
    FastLED.setBrightness(50);  // Start at 50% brightness
    _leds[0] = CRGB::Black;
    FastLED.show();
}

void LedController::loop() {
    unsigned long now = millis();

    switch (_mode) {
        case LedMode::OFF:
            // Only update if color actually changed
            if (_lastShownColor != CRGB::Black) {
                _leds[0] = CRGB::Black;
                FastLED.show();
                _lastShownColor = CRGB::Black;
            }
            break;

        case LedMode::ON:
            // Only update if color actually changed
            if (_lastShownColor != CRGB(_currentColor)) {
                _leds[0] = _currentColor;
                FastLED.show();
                _lastShownColor = _currentColor;
            }
            break;

        case LedMode::BLINK_FAST:
            if (now - _lastToggle >= LED_BLINK_FAST) {
                _ledState = !_ledState;
                _leds[0] = _ledState ? CRGB(_currentColor) : CRGB::Black;
                FastLED.show();
                _lastToggle = now;
            }
            break;

        case LedMode::BLINK_SLOW:
            if (now - _lastToggle >= LED_BLINK_SLOW) {
                _ledState = !_ledState;
                _leds[0] = _ledState ? CRGB(_currentColor) : CRGB::Black;
                FastLED.show();
                _lastToggle = now;
            }
            break;

        case LedMode::PULSE:
            // Smooth pulsing effect - adjusted for 20ms main loop
            // Step size 10 instead of 5 to maintain smooth animation speed
            if (now - _lastToggle >= 20) {
                if (_pulseDirection) {
                    // Going up - check BEFORE adding to prevent uint8_t overflow
                    if (_pulseValue >= 245) {
                        _pulseValue = 255;
                        _pulseDirection = false;
                    } else {
                        _pulseValue += 10;
                    }
                } else {
                    // Going down - check BEFORE subtracting to prevent underflow
                    if (_pulseValue <= 20) {
                        _pulseValue = 10;
                        _pulseDirection = true;
                    } else {
                        _pulseValue -= 10;
                    }
                }
                FastLED.setBrightness(_pulseValue);
                _leds[0] = _currentColor;
                FastLED.show();
                _lastToggle = now;
            }
            break;

        case LedMode::BREATHE_SLOW:
            // Very slow breathing effect (~4 sec cycle) for Timer+Interval combined
            // 30ms interval, step 4 = ~4 seconds per full breath cycle
            if (now - _lastToggle >= 30) {
                if (_pulseDirection) {
                    // Going up - check BEFORE adding to prevent uint8_t overflow
                    if (_pulseValue >= 251) {
                        _pulseValue = 255;
                        _pulseDirection = false;
                    } else {
                        _pulseValue += 4;
                    }
                } else {
                    // Going down - check BEFORE subtracting to prevent underflow
                    if (_pulseValue <= 24) {
                        _pulseValue = 20;
                        _pulseDirection = true;
                    } else {
                        _pulseValue -= 4;
                    }
                }
                FastLED.setBrightness(_pulseValue);
                _leds[0] = _currentColor;
                FastLED.show();
                _lastToggle = now;
            }
            break;

        case LedMode::OTA:
            // Fast alternating for OTA
            if (now - _lastToggle >= 50) {
                _ledState = !_ledState;
                _leds[0] = _ledState ? CRGB(LED_COLOR_PURPLE) : CRGB::Black;
                FastLED.show();
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

        // BREATHE_SLOW starts at max brightness and fades down first
        if (mode == LedMode::BREATHE_SLOW) {
            _pulseValue = 255;
            _pulseDirection = false;  // Start going down
        } else {
            _pulseValue = 0;
            _pulseDirection = true;
        }

        // Restore saved brightness (preserves night mode setting)
        // PULSE and BREATHE_SLOW modes override brightness for their animation
        if (mode != LedMode::PULSE && mode != LedMode::BREATHE_SLOW) {
            // Prevent "brightness trap" - if brightness is 0 and we're turning ON,
            // use a safe default so LED is visible
            if (_brightness == 0 && mode != LedMode::OFF) {
                _brightness = 128;  // 50% as safe default
                Serial.println("[LED] Brightness was 0, reset to 50%");
            }
            FastLED.setBrightness(_brightness);
        }

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
    _currentColor = color;
    if (_mode == LedMode::ON) {
        _leds[0] = color;
        FastLED.show();
    }
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _currentColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (_mode == LedMode::ON) {
        _leds[0] = CRGB(r, g, b);
        FastLED.show();
    }
}

void LedController::showConnected() {
    setColor(LED_COLOR_GREEN);
    setMode(LedMode::ON);
}

void LedController::showConnecting() {
    setColor(LED_COLOR_CYAN);
    setMode(LedMode::BLINK_FAST);
}

void LedController::showAPMode() {
    setColor(LED_COLOR_ORANGE);
    setMode(LedMode::PULSE);
}

void LedController::showFanRunning() {
    setColor(LED_COLOR_GREEN);
    setMode(LedMode::ON);
}

void LedController::showIntervalMode() {
    setColor(LED_COLOR_PURPLE);
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
    FastLED.show();
}

void LedController::setBrightness(uint8_t percent) {
    // Map percent (0-100) to FastLED brightness (0-255)
    _brightness = map(constrain(percent, 0, 100), 0, 100, 0, 255);

    // If brightness is 0, turn LED completely off
    if (_brightness == 0) {
        _leds[0] = CRGB::Black;
    }

    FastLED.setBrightness(_brightness);
    FastLED.show();
    Serial.printf("[LED] Brightness set to %d%%\n", percent);
}

uint8_t LedController::getBrightness() {
    return map(_brightness, 0, 255, 0, 100);
}
