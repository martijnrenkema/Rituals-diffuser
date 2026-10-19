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

// Cartridge usage in the original Rituals firmware's format.
// Page 6: cumulative usage in full-speed minutes. Page 7: Unix time of first
// use. Read from the tag on insertion; the minutes then grow while the fan
// runs. Written back to the tag only when enabled in the settings
// (storage.isRfidWriteUsage()) and the tag allows it.
uint32_t rfidGetUsageMinutes();
uint32_t rfidGetFirstUse();

// Current cartridge accepts usage writes (NTAG21x, pages 6/7 unlocked and not
// password protected, no repeated write failures)
bool rfidIsTagWritable();

// Convert 8-char page-4 hex into the original firmware's (lossy) code format,
// e.g. "44616F00" -> "044616f0". out must hold at least 10 chars.
void rfidFormatLegacyCode(const char* hex, char* out, size_t outSize);

// Hex dump of tag pages 0-47 from the last new-cartridge read (ESP32 only,
// empty on ESP8266). Pages that failed to read are omitted from the end.
const char* rfidGetLastPagesHex();

// Is er recent een tag gedetecteerd?
bool rfidHasTag();

// Is de cartridge NU aanwezig? (niet timeout)
bool rfidIsCartridgePresent();

// Tijd sinds laatste tag detectie (ms)
unsigned long rfidTimeSinceLastTag();

// Look up the scent name from the page-4 hex
ScentInfo rfidLookupScent(const String& uid);

// Is de RC522 geïnitialiseerd en werkend?
bool rfidIsConnected();

// Debug: Get the version register value read during init (0 if not read)
uint8_t rfidGetVersionReg();

// Detected reader chip: "RC522", "PN512" or "none"
const char* rfidGetChipName();

#endif // RC522_ENABLED

#endif // RFID_HANDLER_H
