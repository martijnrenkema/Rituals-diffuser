#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

class FanController {
public:
    void begin();
    void loop();

    // Fan control
    void setSpeed(uint8_t percent);
    uint8_t getSpeed();
    void turnOn();
    void turnOff();
    bool isOn();

    // Timer
    void setTimer(uint16_t minutes);
    void cancelTimer();
    uint16_t getRemainingMinutes();
    bool isTimerActive();

    // Interval mode
    void setIntervalMode(bool enabled);
    bool isIntervalMode();
    void setIntervalTimes(uint8_t onSeconds, uint8_t offSeconds);
    uint8_t getIntervalOnTime();
    uint8_t getIntervalOffTime();

    // RPM (only available on ESP32 with tacho)
    uint16_t getRPM();

    // Runtime statistics
    uint32_t getSessionRuntimeMinutes();
    uint32_t getTotalRuntimeMinutes();

    // Callback for state changes
    typedef void (*StateChangeCallback)(bool on, uint8_t speed);
    void onStateChange(StateChangeCallback callback);

private:
    uint8_t _speed = 0;
    uint8_t _targetSpeed = 0;
    bool _isOn = false;

    // Timer
    unsigned long _timerEndTime = 0;
    bool _timerActive = false;

    // Interval mode
    bool _intervalMode = false;
    uint8_t _intervalOnTime = 30;
    uint8_t _intervalOffTime = 30;
    unsigned long _intervalNextToggle = 0;
    bool _intervalCurrentlyOn = true;

    // RPM measurement via tachometer (GPIO5/TP17)
    static volatile uint32_t _tachoCount;
    static void IRAM_ATTR tachoISR();
    unsigned long _lastRpmCalc = 0;
    uint16_t _rpm = 0;

    // Soft start
    unsigned long _softStartTime = 0;
    uint8_t _softStartTarget = 0;

    // Runtime tracking
    unsigned long _sessionStartTime = 0;
    unsigned long _lastRuntimeSave = 0;
    uint32_t _sessionRuntime = 0;  // Minutes this session

    // Callback
    StateChangeCallback _stateCallback = nullptr;

    void applyPWM(uint8_t percent);
    void notifyStateChange();
    void updateRuntimeStats();

public:
    // Debug/diagnostic functions
    void setRawPWM(uint8_t value);      // Set raw 0-255 PWM value
    void setInvertPWM(bool invert);     // Invert PWM signal
    bool isInvertPWM() { return _invertPWM; }
    uint8_t getCurrentPWMValue() { return _currentPWM; }

    // Calibration
    void startCalibration();            // Start auto-calibration
    bool isCalibrating() { return _calibrating; }
    uint8_t getMinPWM() { return _minPWM; }
    void setMinPWM(uint8_t value);      // Manually set minimum PWM

private:
    bool _invertPWM = false;
    uint8_t _currentPWM = 0;

    // Calibration
    bool _calibrating = false;
    uint8_t _calibrationPWM = 0;
    unsigned long _calibrationStart = 0;
    unsigned long _lastCalibrationStep = 0;
    uint8_t _minPWM = 0;                // Minimum PWM to start fan (stored in NVS)
};

extern FanController fanController;

#endif // FAN_CONTROLLER_H
