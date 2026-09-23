#ifndef STATE_LOCK_H
#define STATE_LOCK_H

#include <Arduino.h>
#include "config.h"

// Serializes access to shared device state (fan, LED, storage, MQTT client)
// between the main loop and the AsyncTCP task that runs HTTP handlers.
//
// ESP32: HTTP handlers run in their own FreeRTOS task, so a recursive mutex is
// used. ESP8266: everything runs on one core in cooperative context, so the
// lock compiles to nothing.
//
// Usage (RAII):
//   StateLock lock(1000);
//   if (!lock.locked()) { /* busy */ }

#ifdef PLATFORM_ESP32
    #include <freertos/FreeRTOS.h>
    #include <freertos/semphr.h>

    void stateLockInit();
    SemaphoreHandle_t stateLockHandle();

    class StateLock {
    public:
        explicit StateLock(uint32_t timeoutMs = portMAX_DELAY) {
            SemaphoreHandle_t h = stateLockHandle();
            TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
            _locked = h != nullptr && xSemaphoreTakeRecursive(h, ticks) == pdTRUE;
        }
        ~StateLock() {
            if (_locked) xSemaphoreGiveRecursive(stateLockHandle());
        }
        bool locked() const { return _locked; }
        StateLock(const StateLock&) = delete;
        StateLock& operator=(const StateLock&) = delete;
    private:
        bool _locked;
    };
#else
    inline void stateLockInit() {}

    class StateLock {
    public:
        explicit StateLock(uint32_t = 0) {}
        bool locked() const { return true; }
    };
#endif

#endif // STATE_LOCK_H
