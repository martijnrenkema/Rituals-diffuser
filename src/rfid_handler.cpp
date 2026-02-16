#include "rfid_handler.h"

#if defined(RC522_ENABLED)

#include <SPI.h>
#include <MFRC522.h>
#include "mqtt_handler.h"  // For state publish on cartridge change

// RC522 instance
static MFRC522* mfrc522 = nullptr;
static bool rc522Connected = false;
static uint8_t rc522VersionReg = 0;  // Store version for debug

// Laatste gedetecteerde tag
#ifdef PLATFORM_ESP8266
// Use fixed char arrays on ESP8266 to avoid heap fragmentation
static char lastUID[24] = "";           // UID max ~14 chars for 7-byte UID + null
static char lastScent[48] = "";         // Scent name
static char lastScentCode[12] = "";     // 8 hex chars + null
#else
static String lastUID = "";
static String lastScent = "";
static String lastScentCode = "";  // 3-letter code from page 4
#endif
static unsigned long lastTagTime = 0;
static unsigned long lastScanTime = 0;
static bool hasValidTag = false;
static bool cartridgePresent = false;

// Timeout: als geen tag gedetecteerd in 5 seconden, is cartridge verwijderd
#define CARTRIDGE_TIMEOUT_MS 5000
// Scan interval: check elke 1000ms of cartridge nog aanwezig is
#define SCAN_INTERVAL_MS 1000

// Geurtabel - officieel gedeeld
// Use PROGMEM on ESP8266 to store table in Flash instead of RAM
struct ScentEntry {
    const char* uid;      // UID prefix (eerste 4 bytes als hex)
    const char* name;
};

// Geurtabel met hex codes - zowel 3-letter ASCII als officiële codes
// Elke geur heeft lowercase, uppercase (capitalized) en officiële hex varianten
#ifdef PLATFORM_ESP8266
#include <pgmspace.h>
static const ScentEntry scentTable[] PROGMEM = {
#else
static const ScentEntry scentTable[] = {
#endif
    // ============ KARMA ============
    {"6B6172", "The Ritual of Karma"},           // "kar" ASCII lowercase
    {"4B6172", "The Ritual of Karma"},           // "Kar" ASCII uppercase
    {"06B617", "The Ritual of Karma"},           // Officieel (alternatief)

    // ============ DAO ============
    {"64616F", "The Ritual of Dao"},             // "dao" ASCII lowercase
    {"44616F", "The Ritual of Dao"},             // "Dao" ASCII uppercase
    {"044616", "The Ritual of Dao"},             // Officieel

    // ============ HAPPY BUDDHA ============
    {"686170", "The Ritual of Happy Buddha"},    // "hap" ASCII lowercase
    {"486170", "The Ritual of Happy Buddha"},    // "Hap" ASCII uppercase
    {"04C617", "The Ritual of Happy Buddha"},    // Officieel

    // ============ SAKURA ============
    {"73616B", "The Ritual of Sakura"},          // "sak" ASCII lowercase
    {"53616B", "The Ritual of Sakura"},          // "Sak" ASCII uppercase
    {"053616", "The Ritual of Sakura"},          // Officieel

    // ============ AYURVEDA ============
    {"617975", "The Ritual of Ayurveda"},        // "ayu" ASCII lowercase
    {"417975", "The Ritual of Ayurveda"},        // "Ayu" ASCII uppercase
    {"047975", "The Ritual of Ayurveda"},        // Officieel

    // ============ HAMMAM ============
    {"68616D", "The Ritual of Hammam"},          // "ham" ASCII lowercase
    {"48616D", "The Ritual of Hammam"},          // "Ham" ASCII uppercase
    {"048616", "The Ritual of Hammam"},          // Officieel

    // ============ JING ============
    {"6A696E", "The Ritual of Jing"},            // "jin" ASCII lowercase
    {"4A696E", "The Ritual of Jing"},            // "Jin" ASCII uppercase
    {"04A696", "The Ritual of Jing"},            // Officieel

    // ============ MEHR ============
    {"6D6568", "The Ritual of Mehr"},            // "meh" ASCII lowercase
    {"4D6568", "The Ritual of Mehr"},            // "Meh" ASCII uppercase
    {"06D656", "The Ritual of Mehr"},            // Officieel

    // ============ SPRING GARDEN ============
    {"737072", "The Ritual of Spring Garden"},   // "spr" ASCII lowercase
    {"537072", "The Ritual of Spring Garden"},   // "Spr" ASCII uppercase
    {"057072", "The Ritual of Spring Garden"},   // Officieel

    // ============ PRIVATE COLLECTION ============
    {"676F6A", "Private Collection Goji Berry"},          // "goj" ASCII lowercase
    {"476F6A", "Private Collection Goji Berry"},          // "Goj" ASCII uppercase
    {"0476F6", "Private Collection Goji Berry"},          // Officieel

    {"766574", "Private Collection Oriental Vetiver"},    // "vet" ASCII lowercase
    {"566574", "Private Collection Oriental Vetiver"},    // "Vet" ASCII uppercase
    {"04F726", "Private Collection Oriental Vetiver"},    // Officieel

    {"6F7564", "Private Collection Black Oudh"},          // "oud" ASCII lowercase
    {"4F7564", "Private Collection Black Oudh"},          // "Oud" ASCII uppercase
    {"0426C6", "Private Collection Black Oudh"},          // Officieel

    {"616D62", "Private Collection Precious Amber"},      // "amb" ASCII lowercase
    {"416D62", "Private Collection Precious Amber"},      // "Amb" ASCII uppercase
    {"057265", "Private Collection Precious Amber"},      // Officieel

    {"6A6173", "Private Collection Sweet Jasmine"},       // "jas" ASCII lowercase
    {"4A6173", "Private Collection Sweet Jasmine"},       // "Jas" ASCII uppercase
    {"057765", "Private Collection Sweet Jasmine"},       // Officieel

    {"726F73", "Private Collection Imperial Rose"},       // "ros" ASCII lowercase
    {"526F73", "Private Collection Imperial Rose"},       // "Ros" ASCII uppercase
    {"0496D7", "Private Collection Imperial Rose"},       // Officieel

    {"736176", "Private Collection Savage Garden"},       // "sav" ASCII lowercase
    {"536176", "Private Collection Savage Garden"},       // "Sav" ASCII uppercase
    {"056176", "Private Collection Savage Garden"},       // Officieel

    {"76616E", "Private Collection Suede Vanilla"},       // "van" ASCII lowercase
    {"56616E", "Private Collection Suede Vanilla"},       // "Van" ASCII uppercase
    {"056616", "Private Collection Suede Vanilla"},       // Officieel

    {"636F74", "Private Collection Cotton Blossom"},      // "cot" ASCII lowercase
    {"436F74", "Private Collection Cotton Blossom"},      // "Cot" ASCII uppercase
    {"0426C6", "Private Collection Cotton Blossom"},      // Officieel

    {"636172", "Private Collection Green Cardamom"},      // "car" ASCII lowercase
    {"436172", "Private Collection Green Cardamom"},      // "Car" ASCII uppercase
    {"047265", "Private Collection Green Cardamom"},      // Officieel

    {"746561", "Private Collection Royal Tea"},           // "tea" ASCII lowercase
    {"546561", "Private Collection Royal Tea"},           // "Tea" ASCII uppercase
    {"047275", "Private Collection Royal Tea"},           // Officieel

    // ============ JING NIGHT ============
    {"6E6967", "The Ritual of Jing Night"},      // "nig" ASCII lowercase
    {"4E6967", "The Ritual of Jing Night"},      // "Nig" ASCII uppercase
    {"047375", "The Ritual of Jing Night"},      // Officieel

    // ============ INVALID ============
    {"013A0C", "Cartridge tag invalid"},         // Officieel

    {nullptr, nullptr}  // End marker
};

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

    if (version == 0x91 || version == 0x92 || version == 0x88) {
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
        mfrc522->PCD_Init();

        return true;
    } else {
        rc522Connected = false;
        Serial.printf("[RFID] RC522 NOT detected! Got version: 0x%02X\n", version);
        if (version == 0x00) {
            Serial.println("[RFID] Version 0x00 suggests: no communication (check wiring/CS pin)");
        } else if (version == 0xFF) {
            Serial.println("[RFID] Version 0xFF suggests: no communication (check wiring/power)");
        }
        Serial.println("[RFID] Expected: 0x91 (v1.0), 0x92 (v2.0), or 0x88 (clone)");
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

    // Check voor kaart (nieuw of bestaand)
    if (!mfrc522->PICC_IsNewCardPresent()) {
        // Probeer bestaande kaart opnieuw te wekken
        byte bufferATQA[2];
        byte bufferSize = sizeof(bufferATQA);
        mfrc522->PCD_Init();  // Reset reader state
        MFRC522::StatusCode status = mfrc522->PICC_WakeupA(bufferATQA, &bufferSize);
        if (status != MFRC522::STATUS_OK) {
            return;  // Geen kaart aanwezig
        }
    }

    // Selecteer de kaart
    if (!mfrc522->PICC_ReadCardSerial()) {
        return;
    }

    // Kaart gedetecteerd - update timestamp
    lastTagTime = now;
    bool wasPresent = cartridgePresent;
    cartridgePresent = true;

#ifdef PLATFORM_ESP8266
    // ESP8266: Use char array to avoid String heap allocation
    char uid[24];
    uid[0] = '\0';
    for (byte i = 0; i < mfrc522->uid.size && i < 10; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", mfrc522->uid.uidByte[i]);
        strcat(uid, hex);
    }
    
    // Check of dit dezelfde kaart is als vorige keer
    bool isNewCard = (strcmp(uid, lastUID) != 0) || !wasPresent;
#else
    // Bouw UID string om te checken of het dezelfde kaart is
    String uid = "";
    for (byte i = 0; i < mfrc522->uid.size; i++) {
        if (mfrc522->uid.uidByte[i] < 0x10) {
            uid += "0";
        }
        uid += String(mfrc522->uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    // Check of dit dezelfde kaart is als vorige keer
    bool isNewCard = (uid != lastUID) || !wasPresent;
#endif

    // Update state
    hasValidTag = true;

    // Als het dezelfde kaart is, alleen timestamp updaten en stoppen
    if (!isNewCard) {
        // Halt PICC om stroom te besparen
        mfrc522->PICC_HaltA();
        return;
    }

    // NIEUWE KAART - volledige verwerking
#ifdef PLATFORM_ESP8266
    strncpy(lastUID, uid, sizeof(lastUID) - 1);
    lastUID[sizeof(lastUID) - 1] = '\0';
#else
    lastUID = uid;
#endif

    // Get tag type
    MFRC522::PICC_Type piccType = mfrc522->PICC_GetType(mfrc522->uid.sak);
    Serial.println();
    Serial.println("========== NEW CARTRIDGE DETECTED ==========");
#ifdef PLATFORM_ESP8266
    Serial.printf("UID: %s (%d bytes)\n", uid, mfrc522->uid.size);
#else
    Serial.printf("UID: %s (%d bytes)\n", uid.c_str(), mfrc522->uid.size);
#endif
    Serial.printf("Tag type: %s\n", mfrc522->PICC_GetTypeName(piccType));

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
        
        // Lookup scent
        ScentInfo info = rfidLookupScent(page4Hex);
        if (info.valid) {
            strncpy(lastScent, info.name.c_str(), sizeof(lastScent) - 1);
            lastScent[sizeof(lastScent) - 1] = '\0';
            Serial.printf("[RFID] Matched scent: %s\n", lastScent);
        } else {
            snprintf(lastScent, sizeof(lastScent), "Unknown: %s", page4Ascii);
            Serial.printf("[RFID] Unknown scent\n");
        }
    } else {
        Serial.printf("[RFID] Read failed: %d\n", status);
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
        lastScentCode = page4Hex;

        // Extract ASCII from allAscii (characters 16-19)
        if (allAscii.length() >= 20) {
            page4Ascii = allAscii.substring(16, 20);
        }
        Serial.printf("[RFID] Page 4 hex: %s (ASCII: %s)\n", page4Hex.c_str(), page4Ascii.c_str());
    } else {
        Serial.println("[RFID] ERROR: Could not extract page 4 data");
    }

    // Lookup geur based on hex code from page 4
    ScentInfo info = rfidLookupScent(page4Hex);
    if (info.valid) {
        lastScent = info.name;
        Serial.printf("[RFID] Matched scent: %s\n", info.name.c_str());
    } else {
        // Show ASCII interpretation if no match
        lastScent = "Unknown: " + page4Ascii;
        Serial.printf("[RFID] Unknown scent - hex: %s, ascii: %s\n", page4Hex.c_str(), page4Ascii.c_str());
    }
#endif

    Serial.println("============================================\n");

    // Notify MQTT of new cartridge
    mqttHandler.requestStatePublish();

    // Halt PICC
    mfrc522->PICC_HaltA();
    mfrc522->PCD_StopCrypto1();
}

String rfidGetLastUID() {
    return lastUID;
}

String rfidGetLastScent() {
    return lastScent;
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

    // Normalize to uppercase for matching
    String data = hexData;
    data.toUpperCase();
    const char* dataPtr = data.c_str();

    // Search for hex codes in the tag data using direct C-string comparison
    // This avoids creating String objects in the loop, reducing heap fragmentation
#ifdef PLATFORM_ESP8266
    // Read from PROGMEM on ESP8266
    ScentEntry entry;
    for (int i = 0; ; i++) {
        memcpy_P(&entry, &scentTable[i], sizeof(ScentEntry));
        if (entry.uid == nullptr) break;
        if (strstr(dataPtr, entry.uid) != nullptr) {
            info.name = String(entry.name);
            info.valid = true;
            Serial.printf("[RFID] Found hex pattern: %s\n", entry.uid);
            break;
        }
    }
#else
    for (int i = 0; scentTable[i].uid != nullptr; i++) {
        // Table UIDs are already uppercase, strstr is case-sensitive, but we've already uppercased data
        if (strstr(dataPtr, scentTable[i].uid) != nullptr) {
            info.name = String(scentTable[i].name);
            info.valid = true;
            Serial.printf("[RFID] Found hex pattern: %s\n", scentTable[i].uid);
            break;
        }
    }
#endif

    return info;
}

bool rfidIsConnected() {
    return rc522Connected;
}

uint8_t rfidGetVersionReg() {
    return rc522VersionReg;
}

#endif // RC522_ENABLED
