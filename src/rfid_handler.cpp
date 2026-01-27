#include "rfid_handler.h"

#if defined(RC522_ENABLED)

#include <SPI.h>
#include <MFRC522.h>
#include "mqtt_handler.h"  // For state publish on cartridge change

// RC522 instance
static MFRC522* mfrc522 = nullptr;
static bool rc522Connected = false;

// Laatste gedetecteerde tag
static String lastUID = "";
static String lastScent = "";
static String lastScentCode = "";  // 3-letter code from page 4
static unsigned long lastTagTime = 0;
static unsigned long lastScanTime = 0;
static bool hasValidTag = false;
static bool cartridgePresent = false;

// Timeout: als geen tag gedetecteerd in 5 seconden, is cartridge verwijderd
#define CARTRIDGE_TIMEOUT_MS 5000
// Scan interval: check elke 1000ms of cartridge nog aanwezig is (was 500ms, verhoogd voor WiFi stabiliteit)
#define SCAN_INTERVAL_MS 1000

// Geurtabel - officieel gedeeld
struct ScentEntry {
    const char* uid;      // UID prefix (eerste 4 bytes als hex)
    const char* name;
};

// Geurtabel met hex codes - zowel 3-letter ASCII als officiële codes
// Format: {"hex-code", "Full scent name"}
static const ScentEntry scentTable[] = {
    // ============ KARMA ============
    {"6B6172", "The Ritual of Karma"},           // "kar" ASCII - VERIFIED
    {"06B617", "The Ritual of Karma"},           // Officieel (alternatief)

    // ============ DAO ============
    {"64616F", "The Ritual of Dao"},             // "dao" ASCII (lowercase)
    {"44616F", "The Ritual of Dao"},             // "Dao" ASCII (capitalized)
    {"044616", "The Ritual of Dao"},             // Officieel

    // ============ HAPPY BUDDHA ============
    {"686170", "The Ritual of Happy Buddha"},    // "hap" ASCII
    {"04C617", "The Ritual of Happy Buddha"},    // Officieel

    // ============ SAKURA ============
    {"73616B", "The Ritual of Sakura"},          // "sak" ASCII
    {"053616", "The Ritual of Sakura"},          // Officieel

    // ============ AYURVEDA ============
    {"617975", "The Ritual of Ayurveda"},        // "ayu" ASCII
    {"047975", "The Ritual of Ayurveda"},        // Officieel

    // ============ HAMMAM ============
    {"68616D", "The Ritual of Hammam"},          // "ham" ASCII
    {"048616", "The Ritual of Hammam"},          // Officieel

    // ============ JING ============
    {"6A696E", "The Ritual of Jing"},            // "jin" ASCII
    {"04A696", "The Ritual of Jing"},            // Officieel

    // ============ MEHR ============
    {"6D6568", "The Ritual of Mehr"},            // "meh" ASCII
    {"06D656", "The Ritual of Mehr"},            // Officieel

    // ============ SPRING GARDEN ============
    {"737072", "The Ritual of Spring Garden"},   // "spr" ASCII
    {"057072", "The Ritual of Spring Garden"},   // Officieel

    // ============ PRIVATE COLLECTION ============
    {"676F6A", "Private Collection Goji Berry"},          // "goj" ASCII
    {"0476F6", "Private Collection Goji Berry"},          // Officieel

    {"766574", "Private Collection Oriental Vetiver"},    // "vet" ASCII
    {"04F726", "Private Collection Oriental Vetiver"},    // Officieel

    {"6F7564", "Private Collection Black Oudh"},          // "oud" ASCII
    {"0426C6", "Private Collection Black Oudh"},          // Officieel

    {"616D62", "Private Collection Precious Amber"},      // "amb" ASCII
    {"057265", "Private Collection Precious Amber"},      // Officieel

    {"6A6173", "Private Collection Sweet Jasmine"},       // "jas" ASCII
    {"057765", "Private Collection Sweet Jasmine"},       // Officieel

    {"726F73", "Private Collection Imperial Rose"},       // "ros" ASCII
    {"0496D7", "Private Collection Imperial Rose"},       // Officieel

    {"736176", "Private Collection Savage Garden"},       // "sav" ASCII
    {"056176", "Private Collection Savage Garden"},       // Officieel

    {"76616E", "Private Collection Suede Vanilla"},       // "van" ASCII
    {"056616", "Private Collection Suede Vanilla"},       // Officieel

    {"636F74", "Private Collection Cotton Blossom"},      // "cot" ASCII
    {"0426C6", "Private Collection Cotton Blossom"},      // Officieel

    {"636172", "Private Collection Green Cardamom"},      // "car" ASCII
    {"047265", "Private Collection Green Cardamom"},      // Officieel

    {"746561", "Private Collection Royal Tea"},           // "tea" ASCII
    {"047275", "Private Collection Royal Tea"},           // Officieel

    // ============ JING NIGHT ============
    {"6E6967", "The Ritual of Jing Night"},      // "nig" ASCII (night)
    {"047375", "The Ritual of Jing Night"},      // Officieel

    // ============ INVALID ============
    {"013A0C", "Cartridge tag invalid"},         // Officieel

    {nullptr, nullptr}  // End marker
};

bool rfidInit() {
    Serial.println("[RFID] Initializing RC522...");
    Serial.printf("[RFID] Pins: SCK=%d, MOSI=%d, MISO=%d, CS=%d, RST=%d\n",
                  RC522_SCK_PIN, RC522_MOSI_PIN, RC522_MISO_PIN, RC522_CS_PIN, RC522_RST_PIN);

    // Initialize SPI - platform specific
#ifdef PLATFORM_ESP8266
    // ESP8266 uses fixed HSPI pins (GPIO14=SCK, GPIO12=MISO, GPIO13=MOSI)
    // No custom pin configuration possible via SPI.begin()
    SPI.begin();
#else
    // ESP32 supports custom SPI pins
    SPI.begin(RC522_SCK_PIN, RC522_MISO_PIN, RC522_MOSI_PIN, RC522_CS_PIN);
#endif

    // Maak MFRC522 instance
    mfrc522 = new MFRC522(RC522_CS_PIN, RC522_RST_PIN);
    mfrc522->PCD_Init();
    delay(100);

    // Check of RC522 gedetecteerd is
    byte version = mfrc522->PCD_ReadRegister(mfrc522->VersionReg);

    if (version == 0x91 || version == 0x92 || version == 0x88) {
        rc522Connected = true;
        Serial.printf("[RFID] RC522 detected! Firmware version: 0x%02X", version);
        if (version == 0x91) Serial.println(" (v1.0)");
        else if (version == 0x92) Serial.println(" (v2.0)");
        else if (version == 0x88) Serial.println(" (clone)");
        return true;
    } else {
        rc522Connected = false;
        Serial.printf("[RFID] RC522 NOT detected! Got version: 0x%02X\n", version);
        Serial.println("[RFID] Check wiring!");
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

    // Scan niet te vaak (elke 500ms)
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

    // Update state
    hasValidTag = true;

    // Als het dezelfde kaart is, alleen timestamp updaten en stoppen
    if (!isNewCard) {
        // Halt PICC om stroom te besparen
        mfrc522->PICC_HaltA();
        return;
    }

    // NIEUWE KAART - volledige verwerking
    lastUID = uid;

    // Get tag type
    MFRC522::PICC_Type piccType = mfrc522->PICC_GetType(mfrc522->uid.sak);
    Serial.println();
    Serial.println("========== NEW CARTRIDGE DETECTED ==========");
    Serial.printf("UID: %s (%d bytes)\n", uid.c_str(), mfrc522->uid.size);
    Serial.printf("Tag type: %s\n", mfrc522->PICC_GetTypeName(piccType));
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

    // Search for hex codes in the tag data
    for (int i = 0; scentTable[i].uid != nullptr; i++) {
        String tableCode = String(scentTable[i].uid);
        tableCode.toUpperCase();

        // Check if the hex code appears in the tag data
        if (data.indexOf(tableCode) >= 0) {
            info.name = String(scentTable[i].name);
            info.valid = true;
            Serial.printf("[RFID] Found hex pattern: %s\n", tableCode.c_str());
            break;
        }
    }

    return info;
}

bool rfidIsConnected() {
    return rc522Connected;
}

#endif // RC522_ENABLED
