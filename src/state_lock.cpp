#include "state_lock.h"

#ifdef PLATFORM_ESP32

static SemaphoreHandle_t stateMutex = nullptr;

void stateLockInit() {
    if (stateMutex == nullptr) {
        stateMutex = xSemaphoreCreateRecursiveMutex();
    }
}

SemaphoreHandle_t stateLockHandle() {
    return stateMutex;
}

#endif
