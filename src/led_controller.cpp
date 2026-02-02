#include "led_controller.h"
#include "config.h"

LedController ledController;

LedController::~LedController() {
#ifdef PLATFORM_ESP8266
    if (_strip != nullptr) {
        delete _strip;
        _strip = nullptr;
    }
#endif
}

void LedController::begin() {
#ifdef PLATFORM_ESP8266
    // Clean up existing instance if begin() is called again (prevents memory leak)
    if (_strip != nullptr) {
        delete _strip;
        _strip = nullptr;
    }
    // NeoPixelBus for ESP8266 - BitBang method is more compatible with GPIO15
    _strip = new NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod>(NUM_LEDS, LED_DATA_PIN);
    _strip->Begin();
    _strip->SetPixelColor(0, RgbColor(0, 0, 0));
    _strip->Show();
    Serial.println("[LED] NeoPixelBus initialized on GPIO15 (BitBang method)");
#else
    // FastLED for ESP32 - brightness is handled in showLed() via RGB scaling
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(_leds, NUM_LEDS);
    FastLED.setBrightness(255);  // Full brightness, we scale RGB values instead
    _leds[0] = CRGB::Black;
    FastLED.show();
    Serial.printf("[LED] FastLED initialized on GPIO%d\n", LED_DATA_PIN);
#endif
    _brightness = 128;  // 50% - applied via RGB scaling in showLed()
}

void LedController::showLed() {
    // Apply brightness to RGB values for both platforms
    // This ensures consistent behavior and proper night mode support
    uint8_t r = ((uint16_t)_r * _brightness) / 255;
    uint8_t g = ((uint16_t)_g * _brightness) / 255;
    uint8_t b = ((uint16_t)_b * _brightness) / 255;

#ifdef PLATFORM_ESP8266
    if (_strip) {
        _strip->SetPixelColor(0, RgbColor(r, g, b));
        _strip->Show();
    }
#else
    _leds[0] = CRGB(r, g, b);
    FastLED.show();
#endif
}

void LedController::loop() {
    unsigned long now = millis();

    switch (_mode) {
        case LedMode::OFF:
            if (_needsUpdate) {
                _r = _g = _b = 0;
                showLed();
                _needsUpdate = false;
            }
            break;

        case LedMode::ON:
            if (_needsUpdate) {
                _r = (_currentColor >> 16) & 0xFF;
                _g = (_currentColor >> 8) & 0xFF;
                _b = _currentColor & 0xFF;
                showLed();
                _needsUpdate = false;
            }
            break;

        case LedMode::BLINK_FAST:
            if (now - _lastToggle >= LED_BLINK_FAST) {
                _ledState = !_ledState;
                if (_ledState) {
                    _r = (_currentColor >> 16) & 0xFF;
                    _g = (_currentColor >> 8) & 0xFF;
                    _b = _currentColor & 0xFF;
                } else {
                    _r = _g = _b = 0;
                }
                showLed();
                _lastToggle = now;
            }
            break;

        case LedMode::BLINK_SLOW:
            if (now - _lastToggle >= LED_BLINK_SLOW) {
                _ledState = !_ledState;
                if (_ledState) {
                    _r = (_currentColor >> 16) & 0xFF;
                    _g = (_currentColor >> 8) & 0xFF;
                    _b = _currentColor & 0xFF;
                } else {
                    _r = _g = _b = 0;
                }
                showLed();
                _lastToggle = now;
            }
            break;

        case LedMode::PULSE:
            if (now - _lastToggle >= 20) {
                if (_pulseDirection) {
                    if (_pulseValue >= 245) {
                        _pulseValue = 255;
                        _pulseDirection = false;
                    } else {
                        _pulseValue += 10;
                    }
                } else {
                    if (_pulseValue <= 20) {
                        _pulseValue = 10;
                        _pulseDirection = true;
                    } else {
                        _pulseValue -= 10;
                    }
                }
                // Apply pulse to color, respecting night mode brightness
                // Scale _pulseValue by _brightness so night mode dims the pulse effect
                uint8_t scaledPulse = ((uint16_t)_pulseValue * _brightness) / 255;
                _r = (((_currentColor >> 16) & 0xFF) * scaledPulse) / 255;
                _g = (((_currentColor >> 8) & 0xFF) * scaledPulse) / 255;
                _b = ((_currentColor & 0xFF) * scaledPulse) / 255;
#ifdef PLATFORM_ESP8266
                if (_strip) {
                    _strip->SetPixelColor(0, RgbColor(_r, _g, _b));
                    _strip->Show();
                }
#else
                _leds[0] = CRGB(_r, _g, _b);
                FastLED.show();
#endif
                _lastToggle = now;
            }
            break;

        case LedMode::BREATHE_SLOW:
            if (now - _lastToggle >= 30) {
                if (_pulseDirection) {
                    if (_pulseValue >= 251) {
                        _pulseValue = 255;
                        _pulseDirection = false;
                    } else {
                        _pulseValue += 4;
                    }
                } else {
                    if (_pulseValue <= 24) {
                        _pulseValue = 20;
                        _pulseDirection = true;
                    } else {
                        _pulseValue -= 4;
                    }
                }
                // Apply breath to color, respecting night mode brightness
                uint8_t scaledPulse = ((uint16_t)_pulseValue * _brightness) / 255;
                _r = (((_currentColor >> 16) & 0xFF) * scaledPulse) / 255;
                _g = (((_currentColor >> 8) & 0xFF) * scaledPulse) / 255;
                _b = ((_currentColor & 0xFF) * scaledPulse) / 255;
#ifdef PLATFORM_ESP8266
                if (_strip) {
                    _strip->SetPixelColor(0, RgbColor(_r, _g, _b));
                    _strip->Show();
                }
#else
                _leds[0] = CRGB(_r, _g, _b);
                FastLED.show();
#endif
                _lastToggle = now;
            }
            break;

        case LedMode::OTA:
            if (now - _lastToggle >= 50) {
                _ledState = !_ledState;
                if (_ledState) {
                    // Purple
                    _r = 0xFF;
                    _g = 0x00;
                    _b = 0xFF;
                } else {
                    _r = _g = _b = 0;
                }
                showLed();
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
        _needsUpdate = true;  // Force update on mode change

        if (mode == LedMode::BREATHE_SLOW) {
            _pulseValue = 255;
            _pulseDirection = false;
        } else {
            _pulseValue = 0;
            _pulseDirection = true;
        }

        // Ensure brightness is not zero when turning on (unless explicitly set)
        if (_brightness == 0 && mode != LedMode::OFF) {
            _brightness = 128;
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
    _needsUpdate = true;
    if (_mode == LedMode::ON) {
        _r = (color >> 16) & 0xFF;
        _g = (color >> 8) & 0xFF;
        _b = color & 0xFF;
        showLed();
    }
}

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _currentColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    _needsUpdate = true;
    if (_mode == LedMode::ON) {
        _r = r;
        _g = g;
        _b = b;
        showLed();
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
    showLed();
}

void LedController::setBrightness(uint8_t percent) {
    _brightness = map(constrain(percent, 0, 100), 0, 100, 0, 255);
    _needsUpdate = true;

    // Brightness is applied via RGB scaling in showLed() for both platforms
    // Force immediate update if LED is currently on
    if (_mode == LedMode::ON || _mode == LedMode::BLINK_FAST || _mode == LedMode::BLINK_SLOW) {
        showLed();
    }
    // PULSE and BREATHE_SLOW modes will pick up the new _brightness on next frame

    Serial.printf("[LED] Brightness set to %d%%\n", percent);
}

uint8_t LedController::getBrightness() {
    return map(_brightness, 0, 255, 0, 100);
}
