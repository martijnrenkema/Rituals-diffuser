#ifndef RFID_HANDLER_H
#define RFID_HANDLER_H

#include <Arduino.h>
#include "config.h"

#ifdef RC522_ENABLED

// Geur informatie struct
struct ScentInfo {
    String name;
    String hexCode;
    bool valid;
};

// Initialiseer de RC522 RFID reader
bool rfidInit();

// Check voor nieuwe RFID tags (call regelmatig in loop)
void rfidLoop();

// Haal de laatst gedetecteerde tag UID op
String rfidGetLastUID();

// Haal de laatst gedetecteerde geur op
String rfidGetLastScent();

// Is er recent een tag gedetecteerd?
bool rfidHasTag();

// Is de cartridge NU aanwezig? (niet timeout)
bool rfidIsCartridgePresent();

// Tijd sinds laatste tag detectie (ms)
unsigned long rfidTimeSinceLastTag();

// Lookup geur naam op basis van UID
ScentInfo rfidLookupScent(const String& uid);

// Is de RC522 geïnitialiseerd en werkend?
bool rfidIsConnected();

// Debug: Get the version register value read during init (0 if not read)
uint8_t rfidGetVersionReg();

#endif // RC522_ENABLED

#endif // RFID_HANDLER_H
