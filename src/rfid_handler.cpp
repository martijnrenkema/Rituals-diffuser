#include "rfid_handler.h"

#if defined(RC522_ENABLED)

#include <SPI.h>
#include <MFRC522.h>
#include "mqtt_handler.h"  // For state publish on cartridge change
#include "fan_controller.h"
#include "storage.h"
#include "logger.h"
#include <time.h>

// RC522 instance
static MFRC522* mfrc522 = nullptr;
static bool rc522Connected = false;
static uint8_t rc522VersionReg = 0;  // Store version for debug
static bool isPn512 = false;          // NXP PN512 found instead of an RC522

// Some Genies ship with an NXP PN512 (VersionReg 0x82) instead of an RC522.
// The register map is compatible, but the PN512 only reads tags with its own
// analog front-end settings. Values taken from the MFRC522_PN512 library.
#define PN512_VERSION 0x82

static MFRC522::PCD_Register pn512Reg(uint8_t addr) {
    return static_cast<MFRC522::PCD_Register>(addr << 1);
}

static void applyPn512Config() {
    mfrc522->PCD_WriteRegister(MFRC522::RxThresholdReg, 0xFF);
    mfrc522->PCD_WriteRegister(MFRC522::ControlReg, 0x10);
    mfrc522->PCD_WriteRegister(MFRC522::DemodReg, 0x4D);
    mfrc522->PCD_WriteRegister(pn512Reg(0x25), 0x87);  // TxBitPhaseReg
    mfrc522->PCD_WriteRegister(MFRC522::RxSelReg, 0x84);
    mfrc522->PCD_WriteRegister(MFRC522::RFCfgReg, 0x48);
    mfrc522->PCD_WriteRegister(pn512Reg(0x23), 0x88);  // GsNOffReg
    mfrc522->PCD_WriteRegister(MFRC522::CWGsPReg, 0x20);
    mfrc522->PCD_WriteRegister(MFRC522::GsNReg, 0x88);  // GsNOnReg
    mfrc522->PCD_WriteRegister(MFRC522::ModGsPReg, 0x20);
    mfrc522->PCD_WriteRegister(MFRC522::BitFramingReg, 0x00);
    mfrc522->PCD_WriteRegister(MFRC522::WaterLevelReg, 64);
    mfrc522->PCD_WriteRegister(pn512Reg(0x1E), 0x00);  // TypeBReg
    mfrc522->PCD_WriteRegister(MFRC522::MfTxReg, 0x8A);
}

// PCD_Init() resets the analog settings, so every (re)init goes through here
static void initReader() {
    mfrc522->PCD_Init();
    if (isPn512) applyPn512Config();
}

// Laatste gedetecteerde tag
// Use fixed char arrays to avoid heap fragmentation
static char lastUID[24] = "";           // UID max ~14 chars for 7-byte UID + null
static char lastScent[48] = "";         // Scent name
static char lastScentCode[12] = "";     // 8 hex chars + null
static uint32_t lastUsageMinutes = 0;    // Page 6: full-speed minutes (tag value + our count)
static uint32_t lastFirstUse = 0;        // Page 7: first-use Unix time (0 = never set)

// Cartridge usage accounting, compatible with the original firmware:
// page 6 = cumulative full-speed minutes, page 7 = Unix time of first use,
// both big-endian u32. Written only when enabled in the settings.
#define USAGE_PAGE          6
#define FIRST_USE_PAGE      7
#define USAGE_WRITE_INTERVAL_MS 600000UL  // While blowing continuously: at most every 10 min
#define MAX_WRITE_FAILURES  3               // Then stop writing to this cartridge
#define CLOCK_VALID_EPOCH   1600000000UL    // time() below this: NTP not synced yet
static bool usageValid = false;          // Pages 6/7 of the current cartridge were read
static bool tagWritable = false;         // Current cartridge accepts writes to pages 6/7
static uint8_t writeFailures = 0;
static uint32_t usageOnTag = 0;          // Page-6 value last read from / written to the tag
static uint32_t pendingSeconds = 0;      // Full-speed seconds not yet counted as a minute
static unsigned long lastUsageWrite = 0; // millis() of the last page-6 write
#ifndef PLATFORM_ESP8266
static char lastPagesHex[48 * 8 + 1] = "";  // Read-only dump of pages 0-47 (ESP32 only)
#endif
static unsigned long lastTagTime = 0;
static unsigned long lastScanTime = 0;
static bool hasValidTag = false;
static bool cartridgePresent = false;

// Timeout: als geen tag gedetecteerd in 5 seconden, is cartridge verwijderd
#define CARTRIDGE_TIMEOUT_MS 5000
// Scan interval: check elke 1000ms of cartridge nog aanwezig is
#define SCAN_INTERVAL_MS 1000

#if ENABLE_SCENT
// Scent table, keyed on the raw page-4 bytes of the cartridge tag.
// Stored in PROGMEM: fixed-size entries so the whole table (incl. strings)
// lives in flash instead of DRAM (~2KB saved on ESP8266).
//
// Published scents store three ASCII characters followed by 0x00 in page 4.
// The codes in the published list use the original firmware's encoding
// ("0" + each byte as unpadded lowercase hex, sometimes without the leading
// "0"); the entries below are the corresponding raw bytes. 6-char entries
// match the first three bytes, 8-char entries match all four bytes.
struct ScentEntry {
    char code[9];      // Page-4 hex prefix (6 or 8 chars + null)
    char name[38];     // Scent name (longest is 35 chars + null)
};

static const ScentEntry scentTable[] PROGMEM = {
    // The Rituals
    {"6B6172", "The Ritual of Karma"},                  // "kar"
    {"44616F", "The Ritual of Dao"},                    // "Dao"
    {"4C6175", "The Ritual of Happy Buddha"},           // "Lau"
    {"53616B", "The Ritual of Sakura"},                 // "Sak"
    {"417975", "The Ritual of Ayurveda"},               // "Ayu"
    {"48616D", "The Ritual of Hammam"},                 // "Ham"
    {"4A696E", "The Ritual of Jing"},                   // "Jin"
    {"6D6568", "The Ritual of Mehr"},                   // "meh"
    {"537072", "The Ritual of Spring Garden"},          // "Spr"
    {"477375", "The Ritual of Jing Night"},             // "Gsu"
    {"02D63620", "The Ritual of Yozakura"},             // Observed on a real cartridge

    // Private Collection
    {"476F6A", "Private Collection Goji Berry"},        // "Goj"
    {"4F7269", "Private Collection Oriental Vetiver"},  // "Ori"
    {"426C61", "Private Collection Black Oudh"},        // "Bla"
    {"507265", "Private Collection Precious Amber"},    // "Pre"
    {"537765", "Private Collection Sweet Jasmine"},     // "Swe"
    {"496D70", "Private Collection Imperial Rose"},     // "Imp"
    {"536176", "Private Collection Savage Garden"},     // "Sav"
    {"56616E", "Private Collection Suede Vanilla"},     // "Van"
    {"426C6F", "Private Collection Cotton Blossom"},    // "Blo"
    {"477265", "Private Collection Green Cardamom"},    // "Gre"
    {"477275", "Private Collection Royal Tea"},         // "Gru"
};
#define SCENT_TABLE_COUNT (sizeof(scentTable) / sizeof(scentTable[0]))
#endif // ENABLE_SCENT

static inline uint32_t readBigEndian32(const byte* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Configuration page (CFG0) of NTAG213/215/216, from the capability container
// data-area size byte. 0 = not an NTAG21x we know.
static byte ntagConfigPage(byte ccSize) {
    switch (ccSize) {
        case 0x12: return 41;   // NTAG213
        case 0x3E: return 131;  // NTAG215
        case 0x6D: return 227;  // NTAG216
        default:   return 0;
    }
}

// Can pages 6 and 7 of the selected tag be written without a password?
// Requires an NTAG21x whose static lock bits leave pages 6/7 open and whose
// AUTH0 does not cover them. Passwords are never tried: NTAG21x can limit
// failed attempts and lock itself.
static bool checkTagWritable() {
    if (mfrc522->uid.sak != 0x00) return false;  // Not Ultralight/NTAG

    byte buf[18];
    byte size = sizeof(buf);
    if (mfrc522->MIFARE_Read(0, buf, &size) != MFRC522::STATUS_OK) return false;
    // Page 2, byte 2: static lock bits, bit n locks page n (4..7)
    if (buf[10] & ((1 << USAGE_PAGE) | (1 << FIRST_USE_PAGE))) return false;
    // Page 3: capability container
    if (buf[12] != 0xE1) return false;
    byte cfgPage = ntagConfigPage(buf[14]);
    if (cfgPage == 0) return false;

    size = sizeof(buf);
    if (mfrc522->MIFARE_Read(cfgPage, buf, &size) != MFRC522::STATUS_OK) return false;
    // CFG0 byte 3 = AUTH0: first page that needs password authentication
    return buf[3] > FIRST_USE_PAGE;
}

// Write a big-endian u32 to page 6 or 7 and read it back. Refuses every
// other page: the scent code and tag configuration must never be touched.
static bool writeTagPage(byte page, uint32_t value) {
    if (page != USAGE_PAGE && page != FIRST_USE_PAGE) return false;

    byte data[4] = { (byte)(value >> 24), (byte)(value >> 16), (byte)(value >> 8), (byte)value };
    if (mfrc522->MIFARE_Ultralight_Write(page, data, sizeof(data)) != MFRC522::STATUS_OK) {
        return false;
    }
    byte buf[18];
    byte size = sizeof(buf);
    if (mfrc522->MIFARE_Read(page, buf, &size) != MFRC522::STATUS_OK) return false;
    return memcmp(buf, data, sizeof(data)) == 0;
}

static void noteWriteResult(bool ok, const char* what) {
    if (ok) {
        writeFailures = 0;
        return;
    }
    writeFailures++;
    Serial.printf("[RFID] Writing %s failed (%u/%u)\n", what, writeFailures, MAX_WRITE_FAILURES);
    if (writeFailures >= MAX_WRITE_FAILURES) {
        logger.warn("Cartridge write failed, usage no longer saved on this cartridge");
    }
}

// Card is selected: bring pages 6/7 up to date if enabled and possible
static void updateTagUsage() {
    if (!usageValid || !tagWritable || writeFailures >= MAX_WRITE_FAILURES) return;
    if (!storage.isRfidWriteUsage()) return;

    // First use: stamped once, as soon as the clock is valid
    time_t now = time(nullptr);
    if (lastFirstUse == 0 && now > (time_t)CLOCK_VALID_EPOCH) {
        bool ok = writeTagPage(FIRST_USE_PAGE, (uint32_t)now);
        noteWriteResult(ok, "first use");
        if (!ok) return;
        lastFirstUse = (uint32_t)now;
        Serial.printf("[RFID] First use stamped: %u\n", (unsigned)lastFirstUse);
    }

    // Like the original, write when a blow period ends (fan off or interval
    // pause); while the fan blows continuously at most every 10 minutes.
    // Keeps tag wear low: NTAG21x endures about 100,000 writes per page.
    bool periodEnded = !fanController.isOutputOn();
    bool intervalDue = millis() - lastUsageWrite >= USAGE_WRITE_INTERVAL_MS;
    if (lastUsageMinutes > usageOnTag && (periodEnded || intervalDue)) {
        bool ok = writeTagPage(USAGE_PAGE, lastUsageMinutes);
        noteWriteResult(ok, "usage");
        if (!ok) return;
        usageOnTag = lastUsageMinutes;
        lastUsageWrite = millis();
        Serial.printf("[RFID] Usage written: %u min\n", (unsigned)usageOnTag);
    }
}

bool rfidInit() {
    Serial.println("[RFID] Initializing RC522...");
    Serial.printf("[RFID] Pins: SCK=%d, MOSI=%d, MISO=%d, CS=%d, RST=%d\n",
                  RC522_SCK_PIN, RC522_MOSI_PIN, RC522_MISO_PIN, RC522_CS_PIN, RC522_RST_PIN);

    // Setup CS and RST pins BEFORE SPI init
    pinMode(RC522_CS_PIN, OUTPUT);
    pinMode(RC522_RST_PIN, OUTPUT);
    digitalWrite(RC522_CS_PIN, HIGH);   // CS inactive
    digitalWrite(RC522_RST_PIN, HIGH);  // Not in reset
    Serial.println("[RFID] CS and RST pins configured");

    // Initialize SPI - platform specific
#ifdef PLATFORM_ESP8266
    // ESP8266 uses fixed HSPI pins (GPIO14=SCK, GPIO12=MISO, GPIO13=MOSI)
    Serial.println("[RFID] ESP8266: Using hardware HSPI");
    SPI.begin();
#else
    // ESP32 supports custom SPI pins
    Serial.println("[RFID] ESP32: Using custom SPI pins");
    SPI.begin(RC522_SCK_PIN, RC522_MISO_PIN, RC522_MOSI_PIN, RC522_CS_PIN);
#endif

    delay(50);  // Let SPI stabilize

    // Perform hardware reset
    Serial.println("[RFID] Performing hardware reset...");
    digitalWrite(RC522_RST_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(RC522_RST_PIN, HIGH);
    delay(50);  // Wait for oscillator startup

    // Create MFRC522 instance (delete existing if re-initializing to prevent memory leak)
    if (mfrc522 != nullptr) {
        delete mfrc522;
        mfrc522 = nullptr;
    }
    mfrc522 = new MFRC522(RC522_CS_PIN, RC522_RST_PIN);
    if (mfrc522 == nullptr) {
        Serial.println("[RFID] ERROR: Failed to allocate MFRC522!");
        return false;
    }

    // Initialize the MFRC522
    Serial.println("[RFID] Calling PCD_Init()...");
    mfrc522->PCD_Init();
    delay(100);

    // Read version register multiple times to check stability
    Serial.println("[RFID] Reading version register...");
    byte version1 = mfrc522->PCD_ReadRegister(mfrc522->VersionReg);
    delay(10);
    byte version2 = mfrc522->PCD_ReadRegister(mfrc522->VersionReg);
    delay(10);
    byte version3 = mfrc522->PCD_ReadRegister(mfrc522->VersionReg);

    Serial.printf("[RFID] Version reads: 0x%02X, 0x%02X, 0x%02X\n", version1, version2, version3);

    // Use the most common value (simple majority vote)
    byte version = version1;
    if (version1 == version2 || version1 == version3) {
        version = version1;
    } else if (version2 == version3) {
        version = version2;
    }

    // Store for debug access via web API
    rc522VersionReg = version;

    isPn512 = (version == PN512_VERSION);

    if (isPn512) {
        rc522Connected = true;
        Serial.println("[RFID] PN512 detected! Firmware version: 0x82");
        // The library has no PN512 self-test reference, so skip the self-test
        initReader();
        return true;
    } else if (version == 0x91 || version == 0x92 || version == 0x88) {
        rc522Connected = true;
        Serial.printf("[RFID] RC522 detected! Firmware version: 0x%02X", version);
        if (version == 0x91) Serial.println(" (v1.0)");
        else if (version == 0x92) Serial.println(" (v2.0)");
        else if (version == 0x88) Serial.println(" (clone)");
        else Serial.println();

        // Perform self-test
        Serial.println("[RFID] RC522 self-test...");
        bool selfTestOk = mfrc522->PCD_PerformSelfTest();
        Serial.printf("[RFID] Self-test result: %s\n", selfTestOk ? "PASS" : "FAIL");

        // Re-init after self-test (self-test disables crypto)
        initReader();

        return true;
    } else {
        rc522Connected = false;
        Serial.printf("[RFID] RC522 NOT detected! Got version: 0x%02X\n", version);
        if (version == 0x00) {
            Serial.println("[RFID] Version 0x00 suggests: no communication (check wiring/CS pin)");
        } else if (version == 0xFF) {
            Serial.println("[RFID] Version 0xFF suggests: no communication (check wiring/power)");
        }
        Serial.println("[RFID] Expected: 0x91 (v1.0), 0x92 (v2.0), 0x88 (clone) or 0x82 (PN512)");
        Serial.println("[RFID] Check wiring!");

        // Debug: try reading other registers
        Serial.println("[RFID] Debug - reading other registers:");
        byte commandReg = mfrc522->PCD_ReadRegister(mfrc522->CommandReg);
        byte statusReg = mfrc522->PCD_ReadRegister(mfrc522->Status1Reg);
        Serial.printf("[RFID] CommandReg: 0x%02X, Status1Reg: 0x%02X\n", commandReg, statusReg);

        return false;
    }
}

void rfidLoop() {
    if (!rc522Connected || mfrc522 == nullptr) {
        return;
    }

    unsigned long now = millis();

    // Check timeout - cartridge verwijderd?
    if (cartridgePresent && (now - lastTagTime > CARTRIDGE_TIMEOUT_MS)) {
        cartridgePresent = false;
        Serial.println("[RFID] Cartridge removed (timeout)");
        mqttHandler.requestStatePublish();  // Notify MQTT immediately
    }

    // Scan niet te vaak (elke 1000ms)
    if (now - lastScanTime < SCAN_INTERVAL_MS) {
        return;
    }
    lastScanTime = now;

    // Count fan use for the cartridge that is in the diffuser. Without one
    // (or with unreadable usage pages) the time is dropped.
    uint32_t fullSpeedSeconds = fanController.takeFullSpeedSeconds();
    if (cartridgePresent && usageValid) {
        pendingSeconds += fullSpeedSeconds;
        if (pendingSeconds >= 60) {
            lastUsageMinutes += pendingSeconds / 60;
            pendingSeconds %= 60;
        }
    }

    // Check voor kaart (nieuw of bestaand)
    if (!mfrc522->PICC_IsNewCardPresent()) {
        // Probeer bestaande kaart opnieuw te wekken
        byte bufferATQA[2];
        byte bufferSize = sizeof(bufferATQA);

        // Only do a full PCD_Init after consecutive failures to reduce SPI overhead
        // PCD_Init resets all RC522 registers - expensive to do every second
        static uint8_t wakeupFailCount = 0;
        if (wakeupFailCount >= 3) {
            initReader();
            wakeupFailCount = 0;
        }

        MFRC522::StatusCode status = mfrc522->PICC_WakeupA(bufferATQA, &bufferSize);
        if (status != MFRC522::STATUS_OK) {
            wakeupFailCount++;
            return;  // Geen kaart aanwezig
        }
        wakeupFailCount = 0;
    }

    // Selecteer de kaart
    if (!mfrc522->PICC_ReadCardSerial()) {
        return;
    }

    // Kaart gedetecteerd - update timestamp
    lastTagTime = now;
    bool wasPresent = cartridgePresent;
    cartridgePresent = true;

    // Build UID string using char array to avoid heap allocation
    char uid[24];
    uid[0] = '\0';
    for (byte i = 0; i < mfrc522->uid.size && i < 10; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", mfrc522->uid.uidByte[i]);
        strcat(uid, hex);
    }

    // Check of dit dezelfde kaart is als vorige keer
    bool isNewCard = (strcmp(uid, lastUID) != 0) || !wasPresent;

    // Update state
    hasValidTag = true;

    // Als het dezelfde kaart is, alleen timestamp updaten en stoppen
    if (!isNewCard) {
        updateTagUsage();
        // Halt PICC om stroom te besparen
        mfrc522->PICC_HaltA();
        return;
    }

    // NIEUWE KAART - volledige verwerking
    strncpy(lastUID, uid, sizeof(lastUID) - 1);
    lastUID[sizeof(lastUID) - 1] = '\0';

    // Get tag type
    MFRC522::PICC_Type piccType = mfrc522->PICC_GetType(mfrc522->uid.sak);
    Serial.println();
    Serial.println("========== NEW CARTRIDGE DETECTED ==========");
    Serial.printf("UID: %s (%d bytes)\n", uid, mfrc522->uid.size);
    // PICC_GetTypeName() returns a flash string: print it, don't pass it to printf
    Serial.print("Tag type: ");
    Serial.println(mfrc522->PICC_GetTypeName(piccType));

#ifdef PLATFORM_ESP8266
    // ESP8266: Minimal read - only page 4 (scent code) to save RAM
    // MIFARE_Read reads 16 bytes starting from page, so page 4 gives us pages 4-7
    byte buffer[18];  // 16 bytes + 2 CRC
    byte size = sizeof(buffer);

    MFRC522::StatusCode status = mfrc522->MIFARE_Read(4, buffer, &size);
    if (status == MFRC522::STATUS_OK) {
        // Extract page 4 (first 4 bytes of buffer)
        char page4Hex[9];
        snprintf(page4Hex, sizeof(page4Hex), "%02X%02X%02X%02X",
                 buffer[0], buffer[1], buffer[2], buffer[3]);
        strncpy(lastScentCode, page4Hex, sizeof(lastScentCode) - 1);
        lastScentCode[sizeof(lastScentCode) - 1] = '\0';

        // ASCII for unknown scents
        char page4Ascii[5];
        for (int i = 0; i < 4; i++) {
            page4Ascii[i] = (buffer[i] >= 32 && buffer[i] < 127) ? (char)buffer[i] : '.';
        }
        page4Ascii[4] = '\0';

        Serial.printf("[RFID] Page 4: %s (ASCII: %s)\n", page4Hex, page4Ascii);

        // MIFARE_Read returned pages 4-7: pages 6 and 7 hold usage data
        lastUsageMinutes = readBigEndian32(&buffer[8]);
        lastFirstUse = readBigEndian32(&buffer[12]);
        usageValid = true;

        // Lookup scent
        ScentInfo info = rfidLookupScent(page4Hex);
        if (info.valid) {
            strncpy(lastScent, info.name.c_str(), sizeof(lastScent) - 1);
            lastScent[sizeof(lastScent) - 1] = '\0';
            Serial.printf("[RFID] Matched scent: %s\n", lastScent);
        } else {
            // Don't leak the raw byte interpretation into the UI/MQTT - the
            // page4 ASCII is usually mostly dots (non-printable) and reads like
            // garbage. Keep the hex/ASCII in the serial log for debugging only.
            strncpy(lastScent, "Unknown cartridge", sizeof(lastScent) - 1);
            lastScent[sizeof(lastScent) - 1] = '\0';
            Serial.printf("[RFID] Unknown scent - hex: %s, ASCII: %s\n", page4Hex, page4Ascii);
        }
    } else {
        Serial.printf("[RFID] Read failed: %d\n", status);
        lastScentCode[0] = '\0';
        lastUsageMinutes = 0;
        lastFirstUse = 0;
        usageValid = false;
        strncpy(lastScent, "Read Error", sizeof(lastScent) - 1);
        lastScent[sizeof(lastScent) - 1] = '\0';
    }
#else
    // ESP32: Full debug dump (more RAM available)
    Serial.printf("SAK: 0x%02X\n", mfrc522->uid.sak);

    // Try to read memory pages (works for MIFARE Ultralight / NTAG)
    Serial.println("\n--- Memory Dump (pages 0-44) ---");
    byte buffer[18];
    byte size;

    // Collect all readable data for pattern analysis
    String allHex = "";
    String allAscii = "";

    for (byte page = 0; page < 45; page += 4) {
        size = sizeof(buffer);
        MFRC522::StatusCode status = mfrc522->MIFARE_Read(page, buffer, &size);
        if (status == MFRC522::STATUS_OK) {
            for (byte p = 0; p < 4 && (page + p) < 45; p++) {
                Serial.printf("Page %2d: ", page + p);
                // Hex dump
                for (byte i = 0; i < 4; i++) {
                    Serial.printf("%02X ", buffer[p * 4 + i]);
                    if (buffer[p * 4 + i] < 0x10) allHex += "0";
                    allHex += String(buffer[p * 4 + i], HEX);
                }
                Serial.print(" | ");
                // ASCII representation
                for (byte i = 0; i < 4; i++) {
                    byte c = buffer[p * 4 + i];
                    char ch = (c >= 32 && c < 127) ? (char)c : '.';
                    Serial.print(ch);
                    allAscii += ch;
                }
                Serial.println();
            }
        } else {
            Serial.printf("Page %2d: Read stopped (%s)\n", page, mfrc522->GetStatusCodeName(status));
            break;
        }
    }

    // Show combined data for easy pattern matching
    allHex.toUpperCase();
    strncpy(lastPagesHex, allHex.c_str(), sizeof(lastPagesHex) - 1);
    lastPagesHex[sizeof(lastPagesHex) - 1] = '\0';
    Serial.println("\n--- Combined hex (for pattern search) ---");
    Serial.println(allHex);
    Serial.println("\n--- Combined ASCII ---");
    Serial.println(allAscii);

    Serial.println("===================================\n");

    // Extract page 4 from the already collected data
    // Page 4 = bytes 16-19, which is characters 32-39 in the hex string
    String page4Hex = "";
    String page4Ascii = "";

    if (allHex.length() >= 40) {
        page4Hex = allHex.substring(32, 40);  // 8 hex chars = 4 bytes
        page4Hex.toUpperCase();
        strncpy(lastScentCode, page4Hex.c_str(), sizeof(lastScentCode) - 1);
        lastScentCode[sizeof(lastScentCode) - 1] = '\0';

        // Extract ASCII from allAscii (characters 16-19)
        if (allAscii.length() >= 20) {
            page4Ascii = allAscii.substring(16, 20);
        }
        Serial.printf("[RFID] Page 4 hex: %s (ASCII: %s)\n", page4Hex.c_str(), page4Ascii.c_str());
    } else {
        Serial.println("[RFID] ERROR: Could not extract page 4 data");
        lastScentCode[0] = '\0';
    }

    // Pages 6 and 7 hold usage data (characters 48-63 of the hex dump)
    lastUsageMinutes = 0;
    lastFirstUse = 0;
    usageValid = false;
    if (allHex.length() >= 64) {
        lastUsageMinutes = strtoul(allHex.substring(48, 56).c_str(), nullptr, 16);
        lastFirstUse = strtoul(allHex.substring(56, 64).c_str(), nullptr, 16);
        usageValid = true;
    }

    // Lookup geur based on hex code from page 4
    ScentInfo info = rfidLookupScent(page4Hex);
    if (info.valid) {
        strncpy(lastScent, info.name.c_str(), sizeof(lastScent) - 1);
        lastScent[sizeof(lastScent) - 1] = '\0';
        Serial.printf("[RFID] Matched scent: %s\n", lastScent);
    } else {
        // Don't leak the raw byte interpretation - page 4 ASCII is usually
        // dots (non-printable) and reads like garbage in the UI/MQTT.
        strncpy(lastScent, "Unknown cartridge", sizeof(lastScent) - 1);
        lastScent[sizeof(lastScent) - 1] = '\0';
        Serial.printf("[RFID] Unknown scent - hex: %s, ascii: %s\n",
                      page4Hex.c_str(), page4Ascii.c_str());
    }
#endif

    if (lastUsageMinutes != 0 || lastFirstUse != 0) {
        Serial.printf("[RFID] Tag usage: %u min, first use %u\n",
                      (unsigned)lastUsageMinutes, (unsigned)lastFirstUse);
    }

    // Usage accounting starts from the tag's own value
    usageOnTag = lastUsageMinutes;
    pendingSeconds = 0;
    writeFailures = 0;
    lastUsageWrite = millis();
    tagWritable = usageValid && checkTagWritable();
    Serial.printf("[RFID] Usage pages writable: %s\n", tagWritable ? "yes" : "no");
    updateTagUsage();

    // Notify MQTT of new cartridge
    mqttHandler.requestStatePublish();

    // Halt PICC
    mfrc522->PICC_HaltA();
    mfrc522->PCD_StopCrypto1();
}

String rfidGetLastUID() {
    return String(lastUID);
}

String rfidGetLastScent() {
    return String(lastScent);
}

const char* rfidGetLastScentCStr() {
    return lastScent;
}

const char* rfidGetLastScentCode() {
    return lastScentCode;
}

uint32_t rfidGetUsageMinutes() {
    return lastUsageMinutes;
}

uint32_t rfidGetFirstUse() {
    return lastFirstUse;
}

bool rfidIsTagWritable() {
    return tagWritable && writeFailures < MAX_WRITE_FAILURES;
}

void rfidFormatLegacyCode(const char* hex, char* out, size_t outSize) {
    // Original firmware encoding: "0" followed by each page-4 byte as
    // lowercase hex without zero padding. Lossy, only for comparison with
    // published code lists.
    if (outSize == 0) return;
    out[0] = '\0';
    if (strlen(hex) != 8) return;
    size_t pos = snprintf(out, outSize, "0");
    for (int i = 0; i < 4 && pos < outSize; i++) {
        char byteHex[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        pos += snprintf(out + pos, outSize - pos, "%lx", strtoul(byteHex, nullptr, 16));
    }
}

const char* rfidGetLastPagesHex() {
#ifndef PLATFORM_ESP8266
    return lastPagesHex;
#else
    return "";
#endif
}

bool rfidHasTag() {
    // Retourneert true als er OOIT een tag was
    return hasValidTag;
}

bool rfidIsCartridgePresent() {
    // Retourneert true als cartridge NU aanwezig is (niet timeout)
    return cartridgePresent;
}

unsigned long rfidTimeSinceLastTag() {
    if (!hasValidTag) {
        return UINT32_MAX;
    }
    return millis() - lastTagTime;
}

ScentInfo rfidLookupScent(const String& hexData) {
    ScentInfo info;
    info.valid = false;
    info.name = "";
    info.hexCode = hexData;

    // Use char array for comparison to avoid heap allocations
    // (previously created 39+ temporary String objects per call)
    char data[16];
    strncpy(data, hexData.c_str(), sizeof(data) - 1);
    data[sizeof(data) - 1] = '\0';

    // Normalize to uppercase for case-insensitive matching
    for (char* p = data; *p; p++) {
        if (*p >= 'a' && *p <= 'f') *p -= 32;
    }

#if ENABLE_SCENT
    // Match each entry against the start of the page-4 hex (no substring
    // search: a code could otherwise match at a shifted nibble position).
    // Table lives in PROGMEM - copy each entry to stack before comparing.
    for (size_t i = 0; i < SCENT_TABLE_COUNT; i++) {
        ScentEntry entry;
        memcpy_P(&entry, &scentTable[i], sizeof(entry));
        if (strncmp(data, entry.code, strlen(entry.code)) == 0) {
            info.name = String(entry.name);
            info.valid = true;
            Serial.printf("[RFID] Found scent code: %s -> %s\n", entry.code, entry.name);
            break;
        }
    }
#endif // ENABLE_SCENT

    return info;
}

bool rfidIsConnected() {
    return rc522Connected;
}

uint8_t rfidGetVersionReg() {
    return rc522VersionReg;
}

const char* rfidGetChipName() {
    if (!rc522Connected) return "none";
    return isPn512 ? "PN512" : "RC522";
}

#endif // RC522_ENABLED
