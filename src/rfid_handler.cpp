#include "rfid_handler.h"
#include "storage.h"

RFIDHandler rfidHandler;

#ifdef PLATFORM_ESP8266

// GPIO pins available for SPI on ESP8266 (avoiding used pins)
// GPIO4, GPIO5 = Fan, GPIO15 = LED, GPIO16 = Button, GPIO3 = Button
// Available: GPIO0, GPIO2, GPIO12, GPIO13, GPIO14
const uint8_t RFIDHandler::SCAN_PINS[] = {0, 2, 12, 13, 14};
const uint8_t RFIDHandler::SCAN_PINS_COUNT = 5;

void RFIDHandler::begin() {
    DiffuserSettings settings = storage.load();

    if (settings.rfidConfigured) {
        Serial.println("[RFID] Using saved pin configuration");
        initReader(settings.rfidPinSCK, settings.rfidPinMISO,
                   settings.rfidPinMOSI, settings.rfidPinSS, settings.rfidPinRST);

        // Verify it still works
        if (_mfrc522 && _mfrc522->PCD_PerformSelfTest()) {
            _status = RFIDStatus::CONFIGURED;
            Serial.println("[RFID] Reader initialized successfully");
            _mfrc522->PCD_Init();  // Re-init after self-test
        } else {
            Serial.println("[RFID] Saved config invalid, needs rescan");
            _status = RFIDStatus::NOT_CONFIGURED;
            if (_mfrc522) {
                delete _mfrc522;
                _mfrc522 = nullptr;
            }
        }
    } else {
        Serial.println("[RFID] Not configured, scan required");
        _status = RFIDStatus::NOT_CONFIGURED;
    }
}

void RFIDHandler::loop() {
    if (_scanning) {
        // Continue pin scanning
        continueScan();
        return;
    }

    if (_status != RFIDStatus::CONFIGURED && _status != RFIDStatus::TAG_PRESENT) {
        return;
    }

    // Check for tags every 500ms
    unsigned long now = millis();
    if (now - _lastCheck < 500) return;
    _lastCheck = now;

    if (detectTag()) {
        String data = readTagData();
        String scent = extractScentName(data);

        if (scent.length() > 0 && scent != _cartridgeName) {
            _cartridgeName = scent;
            storage.setCurrentCartridge(scent.c_str());
            Serial.printf("[RFID] New cartridge detected: %s\n", scent.c_str());

            if (_callback) {
                _callback(scent.c_str());
            }
        }

        _tagPresent = true;
        _status = RFIDStatus::TAG_PRESENT;
    } else {
        if (_tagPresent) {
            Serial.println("[RFID] Cartridge removed");
            _tagPresent = false;
            _status = RFIDStatus::CONFIGURED;
        }
    }

    // Halt and stop crypto to prepare for next read
    if (_mfrc522) {
        _mfrc522->PICC_HaltA();
        _mfrc522->PCD_StopCrypto1();
    }
}

void RFIDHandler::startScan() {
    Serial.println("[RFID] Starting pin scan...");
    _scanning = true;
    _scanComplete = false;
    _scanSuccess = false;
    _scanIndex = 0;
    _scanStartTime = millis();
}

void RFIDHandler::continueScan() {
    // Try different pin combinations
    // SPI needs: SCK, MISO, MOSI, SS (CS), RST
    // We'll try common ESP8266 SPI pins first

    // Standard ESP8266 SPI: SCK=14, MISO=12, MOSI=13
    // Then try: SS and RST on remaining pins

    static const uint8_t sckPins[] = {14, 12, 13};
    static const uint8_t misoPins[] = {12, 14, 13};
    static const uint8_t mosiPins[] = {13, 12, 14};
    static const uint8_t ssPins[] = {2, 0, 15, 4, 5};
    static const uint8_t rstPins[] = {0, 2, 16, 4, 5};

    static uint8_t sckIdx = 0, misoIdx = 0, mosiIdx = 0, ssIdx = 0, rstIdx = 0;

    // Timeout after 30 seconds
    if (millis() - _scanStartTime > 30000) {
        Serial.println("[RFID] Scan timeout");
        _scanning = false;
        _scanComplete = true;
        _scanSuccess = false;
        return;
    }

    // Try current combination
    uint8_t sck = sckPins[sckIdx];
    uint8_t miso = misoPins[misoIdx];
    uint8_t mosi = mosiPins[mosiIdx];
    uint8_t ss = ssPins[ssIdx];
    uint8_t rst = rstPins[rstIdx];

    // Skip invalid combinations (same pin used twice)
    bool valid = (sck != miso && sck != mosi && sck != ss && sck != rst &&
                  miso != mosi && miso != ss && miso != rst &&
                  mosi != ss && mosi != rst &&
                  ss != rst);

    // Skip pins we're using for other purposes
    bool conflict = (sck == 4 || sck == 5 || sck == 15 || sck == 16 || sck == 3 ||
                     miso == 4 || miso == 5 || miso == 15 || miso == 16 || miso == 3 ||
                     mosi == 4 || mosi == 5 || mosi == 15 || mosi == 16 || mosi == 3);

    if (valid && !conflict) {
        Serial.printf("[RFID] Trying: SCK=%d MISO=%d MOSI=%d SS=%d RST=%d\n", sck, miso, mosi, ss, rst);

        if (tryPinCombination(sck, miso, mosi, ss, rst)) {
            Serial.println("[RFID] SUCCESS! Found working pin configuration");
            storage.setRFIDPins(sck, miso, mosi, ss, rst);
            _status = RFIDStatus::CONFIGURED;
            _scanning = false;
            _scanComplete = true;
            _scanSuccess = true;
            return;
        }
    }

    // Move to next combination
    rstIdx++;
    if (rstIdx >= sizeof(rstPins)) {
        rstIdx = 0;
        ssIdx++;
        if (ssIdx >= sizeof(ssPins)) {
            ssIdx = 0;
            mosiIdx++;
            if (mosiIdx >= sizeof(mosiPins)) {
                mosiIdx = 0;
                misoIdx++;
                if (misoIdx >= sizeof(misoPins)) {
                    misoIdx = 0;
                    sckIdx++;
                    if (sckIdx >= sizeof(sckPins)) {
                        // All combinations tried
                        Serial.println("[RFID] Scan complete - no working configuration found");
                        _scanning = false;
                        _scanComplete = true;
                        _scanSuccess = false;
                        return;
                    }
                }
            }
        }
    }

    yield();  // Allow other tasks to run
}

bool RFIDHandler::tryPinCombination(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst) {
    // Clean up previous instance
    if (_mfrc522) {
        delete _mfrc522;
        _mfrc522 = nullptr;
    }

    // Initialize SPI with these pins
    SPI.pins(sck, miso, mosi, ss);
    SPI.begin();

    // Create MFRC522 instance
    _mfrc522 = new MFRC522(ss, rst);
    _mfrc522->PCD_Init();

    delay(50);  // Give it time to initialize

    // Check if reader responds
    byte v = _mfrc522->PCD_ReadRegister(MFRC522::VersionReg);

    // Valid version bytes: 0x91 (v1.0), 0x92 (v2.0), 0x88 (clone)
    if (v == 0x91 || v == 0x92 || v == 0x88 || v == 0x12) {
        Serial.printf("[RFID] Found reader! Version: 0x%02X\n", v);
        return true;
    }

    // Clean up failed attempt
    SPI.end();
    delete _mfrc522;
    _mfrc522 = nullptr;

    return false;
}

void RFIDHandler::initReader(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst) {
    if (_mfrc522) {
        delete _mfrc522;
        _mfrc522 = nullptr;
    }

    SPI.pins(sck, miso, mosi, ss);
    SPI.begin();

    _mfrc522 = new MFRC522(ss, rst);
    _mfrc522->PCD_Init();

    delay(50);
}

bool RFIDHandler::detectTag() {
    if (!_mfrc522) return false;

    // Check for new card
    if (!_mfrc522->PICC_IsNewCardPresent()) {
        return false;
    }

    // Select one of the cards
    if (!_mfrc522->PICC_ReadCardSerial()) {
        return false;
    }

    // Get UID
    String uid = "";
    for (byte i = 0; i < _mfrc522->uid.size; i++) {
        if (_mfrc522->uid.uidByte[i] < 0x10) uid += "0";
        uid += String(_mfrc522->uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    _tagUID = uid;

    return true;
}

String RFIDHandler::readTagData() {
    if (!_mfrc522) return "";

    String data = "";

    // Try to read NDEF data from the tag
    // Rituals tags are likely NTAG213/215 (MIFARE Ultralight compatible)

    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    // Read pages 4-15 (user data area for NTAG)
    byte buffer[18];
    byte size = sizeof(buffer);

    for (byte page = 4; page < 16; page++) {
        MFRC522::StatusCode status = _mfrc522->MIFARE_Read(page, buffer, &size);
        if (status == MFRC522::STATUS_OK) {
            for (byte i = 0; i < 4; i++) {
                if (buffer[i] >= 32 && buffer[i] < 127) {
                    data += (char)buffer[i];
                }
            }
        }
    }

    return data;
}

String RFIDHandler::extractScentName(const String& data) {
    // Rituals cartridge data typically contains the scent name
    // Try to extract readable text

    String scent = "";

    // Look for common Rituals scent names
    String lowerData = data;
    lowerData.toLowerCase();

    // Common Rituals scents
    const char* scents[] = {
        "Sakura", "Karma", "Dao", "Hammam", "Mehr",
        "Samurai", "Jing", "Amsterdam", "Private Collection",
        "The Ritual of", "Oriental", "Ayurveda"
    };

    for (const char* s : scents) {
        String lowerScent = s;
        lowerScent.toLowerCase();
        if (lowerData.indexOf(lowerScent) >= 0) {
            scent = s;
            break;
        }
    }

    // If no known scent found, try to extract any text
    if (scent.length() == 0 && data.length() > 3) {
        // Clean up the data - remove non-printable characters
        for (unsigned int i = 0; i < data.length() && scent.length() < 30; i++) {
            char c = data[i];
            if (c >= 'A' && c <= 'z') {
                scent += c;
            } else if (c == ' ' && scent.length() > 0) {
                scent += c;
            }
        }
        scent.trim();
    }

    return scent;
}

bool RFIDHandler::isScanning() {
    return _scanning;
}

bool RFIDHandler::isScanComplete() {
    return _scanComplete;
}

bool RFIDHandler::isScanSuccess() {
    return _scanSuccess;
}

RFIDStatus RFIDHandler::getStatus() {
    return _status;
}

bool RFIDHandler::isConfigured() {
    return _status == RFIDStatus::CONFIGURED || _status == RFIDStatus::TAG_PRESENT;
}

bool RFIDHandler::isTagPresent() {
    return _tagPresent;
}

String RFIDHandler::getCartridgeName() {
    return _cartridgeName;
}

String RFIDHandler::getTagUID() {
    return _tagUID;
}

void RFIDHandler::configurePins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst) {
    if (tryPinCombination(sck, miso, mosi, ss, rst)) {
        storage.setRFIDPins(sck, miso, mosi, ss, rst);
        _status = RFIDStatus::CONFIGURED;
    } else {
        _status = RFIDStatus::ERROR;
    }
}

void RFIDHandler::clearConfig() {
    storage.clearRFIDConfig();
    if (_mfrc522) {
        delete _mfrc522;
        _mfrc522 = nullptr;
    }
    _status = RFIDStatus::NOT_CONFIGURED;
    _cartridgeName = "";
    _tagPresent = false;
}

void RFIDHandler::onCartridgeChange(CartridgeCallback callback) {
    _callback = callback;
}

#else
// ESP32 stubs
void RFIDHandler::begin() {}
void RFIDHandler::loop() {}
void RFIDHandler::startScan() {}
bool RFIDHandler::isScanning() { return false; }
bool RFIDHandler::isScanComplete() { return false; }
bool RFIDHandler::isScanSuccess() { return false; }
RFIDStatus RFIDHandler::getStatus() { return RFIDStatus::NOT_CONFIGURED; }
bool RFIDHandler::isConfigured() { return false; }
bool RFIDHandler::isTagPresent() { return false; }
String RFIDHandler::getCartridgeName() { return ""; }
String RFIDHandler::getTagUID() { return ""; }
void RFIDHandler::configurePins(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
void RFIDHandler::clearConfig() {}
void RFIDHandler::onCartridgeChange(CartridgeCallback) {}
#endif
