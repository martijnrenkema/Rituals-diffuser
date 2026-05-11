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

// Same as rfidGetLastScent() but returns a const char* into the static buffer
// (no heap allocation). Use this in hot paths like the MQTT state publisher.
const char* rfidGetLastScentCStr();

// Raw page-4 hex of the last scanned cartridge (8 hex chars). Useful to identify
// unknown cartridges so a new entry can be added to the scent table.
const char* rfidGetLastScentCode();

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
