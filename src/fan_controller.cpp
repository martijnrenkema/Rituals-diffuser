#include "fan_controller.h"
#include "config.h"
#include "storage.h"
#include "logger.h"

// Helper macro for LEDC write - new API uses pin, old API uses channel
#if defined(PLATFORM_ESP32) && ESP_ARDUINO_VERSION_MAJOR >= 3
    #define FAN_LEDC_WRITE(duty) ledcWrite(FAN_PWM_PIN, duty)
#elif defined(PLATFORM_ESP32)
    #define FAN_LEDC_WRITE(duty) ledcWrite(PWM_CHANNEL, duty)
#endif

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
    // Arduino-ESP32 v3.x uses new LEDC API, v2.x uses old API
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
        // New API: ledcAttach(pin, freq, resolution) returns true on success
        if (!ledcAttach(FAN_PWM_PIN, PWM_FREQUENCY, PWM_RESOLUTION)) {
            Serial.println("[FAN] ERROR: Failed to attach LEDC to pin");
        }
    #else
        // Old API: separate setup and attach
        ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
    #endif
    FAN_LEDC_WRITE(0);

    // Setup tachometer input with interrupt
    pinMode(FAN_TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tachoISR, FALLING);
#endif

    // Load calibration and failsafe from storage
    _minPWM = storage.getFanMinPWM();
    _autoOffHours = storage.getAutoOffHours();
    Serial.printf("[FAN] Controller initialized (minPWM: %d)\n", _minPWM);
}

void FanController::loop() {
    unsigned long now = millis();

    accumulateUsage(now);

    // Calculate RPM every second (or faster during calibration)
    unsigned long rpmInterval = _calibrating ? 400 : 1000;
    unsigned long elapsed = now - _lastRpmCalc;
    if (elapsed >= rpmInterval) {
        noInterrupts();
        uint32_t count = _tachoCount;
        _tachoCount = 0;
        interrupts();

        // RPM = count * 60000 / (pulses_per_rev * elapsed_ms). Divide by the
        // time that actually passed: the loop can be late (blocking HTTPS,
        // MQTT connect), and dividing by the nominal interval would then
        // overstate the RPM. 64-bit intermediate prevents overflow.
        if (TACHO_PULSES_PER_REV > 0) {
            uint64_t divisor = (uint64_t)TACHO_PULSES_PER_REV * elapsed;
            uint64_t rpm = (uint64_t)count * 60000UL / divisor;
            _rpm = rpm > 65535 ? 65535 : (uint16_t)rpm;
        }
        _lastRpmCalc = now;

        if (_calibrating) {
            Serial.printf("[FAN] RPM calc: count=%lu, rpm=%d\n", (unsigned long)count, _rpm);
        } else {
            checkStall();
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
            FAN_LEDC_WRITE(0);
#endif
            _currentPWM = 0;
            Serial.println("[FAN] Calibration timeout - aborted after 60s");
            notifyStateChange();  // Update LED + MQTT to reflect fan is now off
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
                FAN_LEDC_WRITE(0);
#endif
                _currentPWM = 0;

                Serial.printf("[FAN] Calibration complete! minPWM = %d\n", _minPWM);
                notifyStateChange();  // Update LED + MQTT to reflect fan is now off
            } else if (_calibrationPWM < 250) {
                // Increase PWM and try again
                _calibrationPWM += 5;
#ifdef PLATFORM_ESP8266
                analogWrite(FAN_PWM_PIN, _calibrationPWM);
#else
                FAN_LEDC_WRITE(_calibrationPWM);
#endif
                _currentPWM = _calibrationPWM;
            } else {
                // Reached max PWM, something is wrong
                _calibrating = false;
                _isOn = false;
#ifdef PLATFORM_ESP8266
                analogWrite(FAN_PWM_PIN, 0);
#else
                FAN_LEDC_WRITE(0);
#endif
                _currentPWM = 0;
                Serial.println("[FAN] Calibration failed - no RPM detected");
                notifyStateChange();  // Update LED + MQTT to reflect fan is now off
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

    // Handle timer (subtraction handles millis() overflow correctly)
    if (_timerActive && (now - _timerStartTime >= _timerDuration)) {
        Serial.println("[FAN] Timer expired");
        turnOff();
        _timerActive = false;
    }

    // Auto-off failsafe: without a timer, switch off after the configured
    // hours without any interaction
    if (_isOn && !_timerActive && _autoOffHours > 0 &&
        now - _lastInteraction >= _autoOffHours * AUTO_OFF_UNIT_MS) {
        Serial.printf("[FAN] Auto-off after %u h without interaction\n", _autoOffHours);
        logger.infof("Auto-off after %u h without interaction", _autoOffHours);
        turnOff();
        return;
    }

    // Handle interval mode (subtraction handles millis() overflow correctly)
    if (_isOn && _intervalMode && (now - _intervalToggleStart >= _intervalToggleDuration)) {
        if (_intervalCurrentlyOn) {
            // Turn off (but keep _isOn true for interval cycling)
            applyPWM(0);
            _intervalCurrentlyOn = false;
            _intervalToggleStart = now;
            _intervalToggleDuration = _intervalOffTime * 1000UL;
        } else {
            // Turn back on
            applyPWM(_speed);
            _intervalCurrentlyOn = true;
            _intervalToggleStart = now;
            _intervalToggleDuration = _intervalOnTime * 1000UL;
        }
    }
}

void FanController::setSpeed(uint8_t percent) {
    // Block external speed changes while auto-calibration owns the PWM output -
    // otherwise the loop's calibration ramp races against applyPWM() here and
    // leaves _isOn/_currentPWM in inconsistent state.
    if (_calibrating) {
        Serial.println("[FAN] setSpeed ignored: calibration in progress");
        return;
    }

    if (percent > 100) percent = 100;
    _speed = percent;
    touch();

    // Cancel soft start if active - direct speed change
    _softStartTime = 0;

    // Speed 0 on a running fan = turn off
    if (percent == 0 && _isOn) {
        turnOff();
        return;  // turnOff() calls notifyStateChange()
    }

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
    if (_calibrating) {
        Serial.println("[FAN] turnOn ignored: calibration in progress");
        return;
    }
    touch();  // Also restarts the failsafe period when already on
    if (!_isOn || _speed == 0) {
        _isOn = true;
        if (_speed == 0) _speed = 50; // Default to 50% if not set

        // Start session timer
        _sessionStartTime = millis();
        _lastRuntimeSave = millis();

        // Soft start
        _softStartTime = millis();
        _softStartTarget = _speed;

        // Reset interval mode timing
        if (_intervalMode) {
            _intervalCurrentlyOn = true;
            _intervalToggleStart = millis();
            _intervalToggleDuration = _intervalOnTime * 1000UL;
        }

        Serial.printf("[FAN] Turned ON at %d%%\n", _speed);
        notifyStateChange();
    }
}

void FanController::turnOff() {
    if (_calibrating) {
        Serial.println("[FAN] turnOff ignored: calibration in progress");
        return;
    }

    // Save remaining runtime since last periodic save (avoid double counting)
    if (_isOn && _lastRuntimeSave > 0) {
        uint32_t minutesSinceLastSave = (millis() - _lastRuntimeSave) / 60000;
        if (minutesSinceLastSave > 0) {
            storage.addRuntimeMinutes(minutesSinceLastSave);
            _sessionRuntime += minutesSinceLastSave;
        }
    }
    // Flush any pending runtime to flash (ensures no data loss on ESP8266)
    storage.flushRuntime();

    _isOn = false;
    applyPWM(0);
    _softStartTime = 0;
    _sessionStartTime = 0;

    // Switched off on purpose: a previous stall report no longer applies
    if (_stalled) {
        _stalled = false;
        logger.info("Fan stall cleared (fan turned off)");
    }
    _lowRpmSamples = 0;

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
    touch();
    if (minutes > 0) {
        _timerStartTime = millis();
        _timerDuration = minutes * 60000UL;
        _timerActive = true;
        if (!_isOn) turnOn();
        Serial.printf("[FAN] Timer set for %d minutes\n", minutes);
    }
}

void FanController::cancelTimer() {
    touch();
    _timerActive = false;
    Serial.println("[FAN] Timer cancelled");
}

uint16_t FanController::getRemainingMinutes() {
    if (!_timerActive) return 0;
    unsigned long elapsed = millis() - _timerStartTime;
    if (elapsed >= _timerDuration) return 0;
    // Round up, like the auto-off countdown: "1 min" until it happens
    return (_timerDuration - elapsed + 59999) / 60000;
}

bool FanController::isTimerActive() {
    return _timerActive;
}

void FanController::setIntervalMode(bool enabled) {
    bool wasEnabled = _intervalMode;
    _intervalMode = enabled;
    touch();

    if (enabled) {
        // Initialize interval timing - but DON'T auto-start the fan
        // The fan state (on/off) is independent from interval mode setting
        if (_isOn) {
            // Fan is already running, start interval cycling
            _intervalCurrentlyOn = true;
            _intervalToggleStart = millis();
            _intervalToggleDuration = _intervalOnTime * 1000UL;
            applyPWM(_speed);
        }
        // If fan is off, interval mode is just "armed" and will activate when fan turns on
    } else if (_isOn && wasEnabled) {
        // Interval mode turned off while fan is running - ensure continuous operation
        applyPWM(_speed);
    }

    // Always notify state change so LED and MQTT update correctly
    notifyStateChange();

    Serial.printf("[FAN] Interval mode: %s%s\n", enabled ? "ON" : "OFF",
                  (enabled && !_isOn) ? " (will activate when fan starts)" : "");
}

bool FanController::isIntervalMode() {
    return _intervalMode;
}

void FanController::setIntervalTimes(uint8_t onSeconds, uint8_t offSeconds) {
    touch();
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

void FanController::setAutoOffHours(uint8_t hours) {
    _autoOffHours = constrain(hours, 1, AUTO_OFF_MAX_HOURS);
    touch();
}

uint16_t FanController::getAutoOffRemainingMinutes() {
    if (!_isOn || _timerActive || _autoOffHours == 0) return 0;
    unsigned long limit = _autoOffHours * AUTO_OFF_UNIT_MS;
    unsigned long elapsed = millis() - _lastInteraction;
    if (elapsed >= limit) return 0;
    return (limit - elapsed + 59999) / 60000;  // Round up: "1 min" until it happens
}

uint16_t FanController::getRPM() {
    return _rpm;
}

// Called once per RPM sample. Only judged while the output drives the fan and
// the spin-up grace period has passed; during an interval pause the previous
// verdict is kept, so a stalled fan isn't reported again every cycle.
void FanController::checkStall() {
    if (!_outputOn || millis() - _outputOnSince < FAN_STALL_GRACE_MS) {
        _lowRpmSamples = 0;
        return;
    }

    if (_rpm < FAN_STALL_RPM) {
        if (_lowRpmSamples < FAN_STALL_SAMPLES) _lowRpmSamples++;
    } else {
        _lowRpmSamples = 0;
    }

    bool stalled = _lowRpmSamples >= FAN_STALL_SAMPLES;
    if (stalled == _stalled) return;
    _stalled = stalled;

    if (stalled) {
        Serial.printf("[FAN] Fan is not turning (RPM %u at %u%%)\n", _rpm, _outputPercent);
        logger.errorf("Fan is not turning (RPM %u at %u%%)", _rpm, _outputPercent);
    } else {
        Serial.printf("[FAN] Fan turning again (RPM %u)\n", _rpm);
        logger.infof("Fan turning again (RPM %u)", _rpm);
    }
    notifyStateChange();  // LED + MQTT
}

void FanController::accumulateUsage(unsigned long now) {
    unsigned long dt = now - _lastUsageTick;
    _lastUsageTick = now;
    if (_outputOn && !_calibrating && _outputPercent > 1) {
        _usageWeightedMs += (uint64_t)dt * (_outputPercent - 1);
    }
}

uint32_t FanController::takeFullSpeedSeconds() {
    // 1 full-speed second = 1000 ms at 100% = 1000 * 99 weighted units
    const uint64_t unit = 1000ULL * 99;
    uint32_t seconds = (uint32_t)(_usageWeightedMs / unit);
    _usageWeightedMs -= (uint64_t)seconds * unit;
    return seconds;
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

    // Track the effective output for stall detection and usage accounting
    accumulateUsage(millis());  // Close the period at the old speed
    bool driving = percent > 0;
    if (driving && !_outputOn) {
        _outputOnSince = millis();
        _lowRpmSamples = 0;
    }
    _outputOn = driving;
    _outputPercent = percent;

    Serial.printf("[FAN] PWM: %d%% -> raw=%d (min=%d, invert=%s)\n",
                  percent, pwmValue, _minPWM, _invertPWM ? "yes" : "no");

#ifdef PLATFORM_ESP8266
    // ESP8266: GPIO4 = PWM speed control
    analogWrite(FAN_PWM_PIN, pwmValue);
#else
    // ESP32: LEDC PWM speed control
    FAN_LEDC_WRITE(pwmValue);
#endif
}

void FanController::setRawPWM(uint8_t value) {
    _currentPWM = value;
    Serial.printf("[FAN] Raw PWM set to: %d\n", value);

    // Diagnostic output outside the normal speed mapping: don't judge stalls
    // or count cartridge usage on it
    accumulateUsage(millis());
    _outputOn = false;
    _lowRpmSamples = 0;

#ifdef PLATFORM_ESP8266
    analogWrite(FAN_PWM_PIN, value);
#else
    FAN_LEDC_WRITE(value);
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

    // Turn the fan off through the normal path so pending runtime is saved,
    // the timer is cancelled and LED/MQTT get notified
    if (_isOn) {
        turnOff();
    }
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
    FAN_LEDC_WRITE(0);
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
    // Storage already contains everything up to _lastRuntimeSave (saved every
    // 30 min while running), so only add the part that isn't stored yet.
    uint32_t unsaved = 0;
    if (_isOn && _lastRuntimeSave > 0) {
        unsaved = (millis() - _lastRuntimeSave) / 60000;
    }
    return storage.getTotalRuntimeMinutes() + unsaved;
}
