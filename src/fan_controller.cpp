#include "fan_controller.h"
#include "config.h"
#include "storage.h"

FanController fanController;

#ifdef PLATFORM_ESP32
volatile uint32_t FanController::_tachoCount = 0;

void IRAM_ATTR FanController::tachoISR() {
    _tachoCount++;
}
#endif

void FanController::begin() {
#ifdef PLATFORM_ESP8266
    // ESP8266: Setup PWM pins
    pinMode(FAN_PWM_PIN, OUTPUT);
    pinMode(FAN_SPEED_PIN, OUTPUT);
    analogWriteFreq(PWM_FREQUENCY);
    analogWriteRange(PWM_RANGE);
    analogWrite(FAN_PWM_PIN, 0);
    analogWrite(FAN_SPEED_PIN, 0);
#else
    // ESP32: Setup LEDC PWM
    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    // Setup tachometer input with interrupt
    pinMode(FAN_TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tachoISR, FALLING);
#endif

    Serial.println("[FAN] Controller initialized");
}

void FanController::loop() {
    unsigned long now = millis();

#ifdef PLATFORM_ESP32
    // Calculate RPM every second (ESP32 only)
    if (now - _lastRpmCalc >= 1000) {
        noInterrupts();
        uint32_t count = _tachoCount;
        _tachoCount = 0;
        interrupts();

        // RPM = (pulses per second / pulses per revolution) * 60
        _rpm = (count / TACHO_PULSES_PER_REV) * 60;
        _lastRpmCalc = now;
    }
#endif

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
    // Save final runtime before turning off
    if (_isOn && _sessionStartTime > 0) {
        uint32_t sessionMinutes = (millis() - _sessionStartTime) / 60000;
        if (sessionMinutes > 0) {
            storage.addRuntimeMinutes(sessionMinutes);
            _sessionRuntime += sessionMinutes;
        }
    }

    _isOn = false;
    applyPWM(0);
    _softStartTime = 0;
    _sessionStartTime = 0;
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
    return (_timerEndTime - now) / 60000;
}

bool FanController::isTimerActive() {
    return _timerActive;
}

void FanController::setIntervalMode(bool enabled) {
    _intervalMode = enabled;
    if (enabled && _isOn) {
        _intervalCurrentlyOn = true;
        _intervalNextToggle = millis() + (_intervalOnTime * 1000UL);
        applyPWM(_speed);
    } else if (!enabled && _isOn) {
        applyPWM(_speed);
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
    uint8_t pwmValue = map(percent, 0, 100, 0, 255);

#ifdef PLATFORM_ESP8266
    // Rituals Genie: GPIO4 = on/off, GPIO5 = speed
    if (percent > 0) {
        digitalWrite(FAN_PWM_PIN, HIGH);  // Fan on
        analogWrite(FAN_SPEED_PIN, pwmValue);  // Speed control
    } else {
        digitalWrite(FAN_PWM_PIN, LOW);   // Fan off
        analogWrite(FAN_SPEED_PIN, 0);
    }
#else
    ledcWrite(PWM_CHANNEL, pwmValue);
#endif
}

void FanController::notifyStateChange() {
    if (_stateCallback) {
        _stateCallback(_isOn, _speed);
    }
}

void FanController::updateRuntimeStats() {
    if (!_isOn || _sessionStartTime == 0) return;

    unsigned long now = millis();

    // Save runtime every 5 minutes while running
    if (now - _lastRuntimeSave >= 300000) {  // 5 minutes
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

uint32_t FanController::getCartridgeRuntimeMinutes() {
    return storage.getCartridgeRuntimeMinutes() + getSessionRuntimeMinutes();
}
