#ifndef SYNC_OTA_H
#define SYNC_OTA_H

#include "config.h"  // For PLATFORM_ESP8266 detection

#ifdef PLATFORM_ESP8266

// Flag to signal main loop to switch to sync OTA mode
extern volatile bool requestSyncOTAMode;

// Run the synchronous OTA server (blocking - takes over from main loop)
void runSyncOTAServer();

#endif // PLATFORM_ESP8266

#endif // SYNC_OTA_H
