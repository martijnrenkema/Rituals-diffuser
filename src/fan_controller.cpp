#include "fan_controller.h"
#include "config.h"
#include "storage.h"

FanController fanController;

// Tachometer interrupt voor RPM meting
volatile uint32_t FanController::_tachoCount = 0;

void IRAM_ATTR FanController::tachoISR() {
    if (_tachoCount < 60000) _tachoCount++;  // Prevent overflow from spurious interrupts
}

void FanController::begin() {
#ifdef PLATFORM_ESP8266
    // ESP8266: Setup PWM pin
    pinMode(FAN_PWM_PIN, OUTPUT);
    analogWriteFreq(PWM_FREQUENCY);
    analogWriteRange(PWM_RANGE);
    analogWrite(FAN_PWM_PIN, 0);

    // Setup tachometer input with interrupt
    pinMode(FAN_TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tachoISR, FALLING);
#else
    // ESP32: Setup LEDC PWM
    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    // Setup tachometer input with interrupt
    pinMode(FAN_TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tachoISR, FALLING);
#endif

    // Load calibration from storage
    _minPWM = storage.getFanMinPWM();
    Serial.printf("[FAN] Controller initialized (minPWM: %d)\n", _minPWM);
}

void FanController::loop() {
    unsigned long now = millis();

    // Calculate RPM every second (or faster during calibration)
    unsigned long rpmInterval = _calibrating ? 400 : 1000;
    if (now - _lastRpmCalc >= rpmInterval) {
        noInterrupts();
        uint32_t count = _tachoCount;
        _tachoCount = 0;
        interrupts();

        // RPM = (pulses per interval / pulses per revolution) * 60 * (1000/interval)
        // Simplified: RPM = count * 60000 / (pulses_per_rev * interval_ms)
        if (rpmInterval > 0) {
            _rpm = (count * 60000UL) / (TACHO_PULSES_PER_REV * rpmInterval);
        }
        _lastRpmCalc = now;

        if (_calibrating) {
            Serial.printf("[FAN] RPM calc: count=%lu, rpm=%d\n", count, _rpm);
        }
    }

    // Handle calibration - takes over fan control completely
    if (_calibrating) {
        // Timeout after 60 seconds to prevent infinite calibration
        if (now - _calibrationStart >= 60000) {
            _calibrating = false;
            _isOn = false;
#ifdef PLATFORM_ESP8266
            analogWrite(FAN_PWM_PIN, 0);
#else
            ledcWrite(PWM_CHANNEL, 0);
#endif
            _currentPWM = 0;
            Serial.println("[FAN] Calibration timeout - aborted after 60s");
            return;
        }

        // Wait at least 800ms between steps to allow RPM to stabilize
        if (now - _lastCalibrationStep >= 800) {
            _lastCalibrationStep = now;

            Serial.printf("[FAN] Calibrating... PWM=%d, RPM=%d\n", _calibrationPWM, _rpm);

            if (_rpm > 200) {
                // Fan is spinning! Found the minimum PWM (threshold 200 RPM to avoid noise)
                _minPWM = _calibrationPWM;
                storage.setFanMinPWM(_minPWM);
                _calibrating = false;
                _isOn = false;

                // Turn off fan after calibration
#ifdef PLATFORM_ESP8266
                analogWrite(FAN_PWM_PIN, 0);
#else
                ledcWrite(PWM_CHANNEL, 0);
#endif
                _currentPWM = 0;

                Serial.printf("[FAN] Calibration complete! minPWM = %d\n", _minPWM);
            } else if (_calibrationPWM < 250) {
                // Increase PWM and try again
                _calibrationPWM += 5;
#ifdef PLATFORM_ESP8266
                analogWrite(FAN_PWM_PIN, _calibrationPWM);
#else
                ledcWrite(PWM_CHANNEL, _calibrationPWM);
#endif
                _currentPWM = _calibrationPWM;
            } else {
                // Reached max PWM, something is wrong
                _calibrating = false;
                _isOn = false;
#ifdef PLATFORM_ESP8266
                analogWrite(FAN_PWM_PIN, 0);
#else
                ledcWrite(PWM_CHANNEL, 0);
#endif
                _currentPWM = 0;
                Serial.println("[FAN] Calibration failed - no RPM detected");
            }
        }
        return;  // Skip normal fan logic during calibration
    }

    // Update runtime statistics every minute
    updateRuntimeStats();

    // Handle soft start
    if (_softStartTime > 0) {
        unsigned long elapsed = now - _softStartTime;
        if (elapsed >= FAN_SOFT_START_MS) {
            applyPWM(_softStartTarget);
            _softStartTime = 0;
        } else {
            uint8_t currentPwm = map(elapsed, 0, FAN_SOFT_START_MS, 0, _softStartTarget);
            applyPWM(currentPwm);
        }
    }

    // Handle timer
    if (_timerActive && now >= _timerEndTime) {
        Serial.println("[FAN] Timer expired");
        turnOff();
        _timerActive = false;
    }

    // Handle interval mode
    if (_isOn && _intervalMode && now >= _intervalNextToggle) {
        if (_intervalCurrentlyOn) {
            // Turn off (but keep _isOn true for interval cycling)
            applyPWM(0);
            _intervalCurrentlyOn = false;
            _intervalNextToggle = now + (_intervalOffTime * 1000UL);
        } else {
            // Turn back on
            applyPWM(_speed);
            _intervalCurrentlyOn = true;
            _intervalNextToggle = now + (_intervalOnTime * 1000UL);
        }
    }
}

void FanController::setSpeed(uint8_t percent) {
    if (percent > 100) percent = 100;
    _speed = percent;
    _targetSpeed = percent;

    // Cancel soft start if active - direct speed change
    _softStartTime = 0;

    if (_isOn) {
        if (_intervalMode && !_intervalCurrentlyOn) {
            // In interval off phase, don't apply yet
        } else {
            applyPWM(percent);
        }
    }
    notifyStateChange();
}

uint8_t FanController::getSpeed() {
    return _speed;
}

void FanController::turnOn() {
    if (!_isOn || _speed == 0) {
        _isOn = true;
        if (_speed == 0) _speed = 50; // Default to 50% if not set

        // Start session timer
        _sessionStartTime = millis();
        _lastRuntimeSave = millis();

        // Soft start
        _softStartTime = millis();
        _softStartTarget = _speed;

        // Reset interval mode
        if (_intervalMode) {
            _intervalCurrentlyOn = true;
            _intervalNextToggle = millis() + (_intervalOnTime * 1000UL);
        }

        Serial.printf("[FAN] Turned ON at %d%%\n", _speed);
        notifyStateChange();
    }
}

void FanController::turnOff() {
    // Save remaining runtime since last periodic save (avoid double counting)
    if (_isOn && _lastRuntimeSave > 0) {
        uint32_t minutesSinceLastSave = (millis() - _lastRuntimeSave) / 60000;
        if (minutesSinceLastSave > 0) {
            storage.addRuntimeMinutes(minutesSinceLastSave);
            _sessionRuntime += minutesSinceLastSave;
        }
    }

    _isOn = false;
    applyPWM(0);
    _softStartTime = 0;
    _sessionStartTime = 0;

    // Cancel timer when fan is turned off
    if (_timerActive) {
        _timerActive = false;
        Serial.println("[FAN] Timer cancelled (fan turned off)");
    }

    Serial.println("[FAN] Turned OFF");
    notifyStateChange();
}

bool FanController::isOn() {
    return _isOn;
}

void FanController::setTimer(uint16_t minutes) {
    if (minutes > 0) {
        _timerEndTime = millis() + (minutes * 60000UL);
        _timerActive = true;
        if (!_isOn) turnOn();
        Serial.printf("[FAN] Timer set for %d minutes\n", minutes);
    }
}

void FanController::cancelTimer() {
    _timerActive = false;
    Serial.println("[FAN] Timer cancelled");
}

uint16_t FanController::getRemainingMinutes() {
    if (!_timerActive) return 0;
    unsigned long now = millis();
    if (now >= _timerEndTime) return 0;
    // Round down - shows actual complete minutes remaining
    return (_timerEndTime - now) / 60000;
}

bool FanController::isTimerActive() {
    return _timerActive;
}

void FanController::setIntervalMode(bool enabled) {
    _intervalMode = enabled;
    if (enabled) {
        // Interval mode requires fan to be on
        if (!_isOn) {
            _isOn = true;
            if (_speed == 0) _speed = 50;  // Default speed if not set
        }
        _intervalCurrentlyOn = true;
        _intervalNextToggle = millis() + (_intervalOnTime * 1000UL);
        applyPWM(_speed);
        notifyStateChange();
    } else if (_isOn) {
        // Interval mode off, but fan stays on at constant speed
        applyPWM(_speed);
        notifyStateChange();
    }
    Serial.printf("[FAN] Interval mode: %s\n", enabled ? "ON" : "OFF");
}

bool FanController::isIntervalMode() {
    return _intervalMode;
}

void FanController::setIntervalTimes(uint8_t onSeconds, uint8_t offSeconds) {
    _intervalOnTime = constrain(onSeconds, INTERVAL_MIN, INTERVAL_MAX);
    _intervalOffTime = constrain(offSeconds, INTERVAL_MIN, INTERVAL_MAX);
    Serial.printf("[FAN] Interval times: %ds ON, %ds OFF\n", _intervalOnTime, _intervalOffTime);
}

uint8_t FanController::getIntervalOnTime() {
    return _intervalOnTime;
}

uint8_t FanController::getIntervalOffTime() {
    return _intervalOffTime;
}

uint16_t FanController::getRPM() {
    return _rpm;
}

void FanController::onStateChange(StateChangeCallback callback) {
    _stateCallback = callback;
}

void FanController::applyPWM(uint8_t percent) {
    uint8_t pwmValue;

    if (percent == 0) {
        pwmValue = 0;  // Off is always 0
    } else {
        // Map 1-100% to minPWM-255
        pwmValue = map(percent, 1, 100, _minPWM, 255);
    }

    // Apply inversion if enabled
    if (_invertPWM) {
        pwmValue = 255 - pwmValue;
    }

    _currentPWM = pwmValue;

    Serial.printf("[FAN] PWM: %d%% -> raw=%d (min=%d, invert=%s)\n",
                  percent, pwmValue, _minPWM, _invertPWM ? "yes" : "no");

#ifdef PLATFORM_ESP8266
    // ESP8266: GPIO4 = PWM speed control
    analogWrite(FAN_PWM_PIN, pwmValue);
#else
    // ESP32: LEDC PWM speed control
    ledcWrite(PWM_CHANNEL, pwmValue);
#endif
}

void FanController::setRawPWM(uint8_t value) {
    _currentPWM = value;
    Serial.printf("[FAN] Raw PWM set to: %d\n", value);

#ifdef PLATFORM_ESP8266
    analogWrite(FAN_PWM_PIN, value);
#else
    ledcWrite(PWM_CHANNEL, value);
#endif
}

void FanController::setInvertPWM(bool invert) {
    _invertPWM = invert;
    Serial.printf("[FAN] PWM invert: %s\n", invert ? "enabled" : "disabled");

    // Re-apply current speed with new inversion setting
    if (_isOn) {
        applyPWM(_speed);
    }
}

void FanController::startCalibration() {
    if (_calibrating) return;

    Serial.println("[FAN] Starting calibration...");

    // Ensure fan is off and state is clean
    _isOn = false;
    _softStartTime = 0;
    _timerActive = false;

    // Reset tachometer counter for fresh measurement
    noInterrupts();
    _tachoCount = 0;
    interrupts();
    _rpm = 0;
    _lastRpmCalc = millis();

    // Start calibration
    _calibrating = true;
    _calibrationPWM = 0;
    _calibrationStart = millis();
    _lastCalibrationStep = millis() - 500;  // Allow first step soon

    // Start with PWM 0
#ifdef PLATFORM_ESP8266
    analogWrite(FAN_PWM_PIN, 0);
#else
    ledcWrite(PWM_CHANNEL, 0);
#endif
    _currentPWM = 0;
}

void FanController::setMinPWM(uint8_t value) {
    _minPWM = value;
    storage.setFanMinPWM(value);
    Serial.printf("[FAN] minPWM set to: %d\n", value);

    // Re-apply current speed with new minimum
    if (_isOn) {
        applyPWM(_speed);
    }
}

void FanController::notifyStateChange() {
    if (_stateCallback) {
        _stateCallback(_isOn, _speed);
    }
}

void FanController::updateRuntimeStats() {
    if (!_isOn || _sessionStartTime == 0) return;

    unsigned long now = millis();

    // Save runtime every 30 minutes while running (reduced frequency to minimize flash wear)
    if (now - _lastRuntimeSave >= 1800000) {  // 30 minutes
        uint32_t minutesSinceLastSave = (now - _lastRuntimeSave) / 60000;
        if (minutesSinceLastSave > 0) {
            storage.addRuntimeMinutes(minutesSinceLastSave);
            _sessionRuntime += minutesSinceLastSave;
            _lastRuntimeSave = now;
            Serial.printf("[FAN] Runtime saved: +%d min (total: %d min)\n",
                          minutesSinceLastSave, storage.getTotalRuntimeMinutes());
        }
    }
}

uint32_t FanController::getSessionRuntimeMinutes() {
    if (!_isOn || _sessionStartTime == 0) return 0;
    return (millis() - _sessionStartTime) / 60000;
}

uint32_t FanController::getTotalRuntimeMinutes() {
    return storage.getTotalRuntimeMinutes() + getSessionRuntimeMinutes();
}
