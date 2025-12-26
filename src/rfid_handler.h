#ifndef RFID_HANDLER_H
#define RFID_HANDLER_H

#include <Arduino.h>
#include "config.h"

#ifdef PLATFORM_ESP8266
#include <SPI.h>
#include <MFRC522.h>
#endif

enum class RFIDStatus {
    NOT_CONFIGURED,
    SCANNING,
    CONFIGURED,
    TAG_PRESENT,
    ERROR
};

class RFIDHandler {
public:
    void begin();
    void loop();

    // Pin scanning
    void startScan();
    bool isScanning();
    bool isScanComplete();
    bool isScanSuccess();

    // Status
    RFIDStatus getStatus();
    bool isConfigured();
    bool isTagPresent();
    String getCartridgeName();
    String getTagUID();

    // Manual pin configuration
    void configurePins(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst);
    void clearConfig();

    // Callbacks
    typedef void (*CartridgeCallback)(const char* name);
    void onCartridgeChange(CartridgeCallback callback);

private:
    RFIDStatus _status = RFIDStatus::NOT_CONFIGURED;
    bool _scanning = false;
    bool _scanComplete = false;
    bool _scanSuccess = false;
    bool _tagPresent = false;
    String _cartridgeName = "";
    String _tagUID = "";
    String _lastTagUID = "";
    unsigned long _lastCheck = 0;
    unsigned long _scanStartTime = 0;
    CartridgeCallback _callback = nullptr;

#ifdef PLATFORM_ESP8266
    MFRC522* _mfrc522 = nullptr;
#endif

    // Pin combinations to try during scan
    static const uint8_t SCAN_PINS[];
    static const uint8_t SCAN_PINS_COUNT;
    uint8_t _scanIndex = 0;

    bool tryPinCombination(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst);
    void initReader(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t rst);
    void continueScan();
    bool detectTag();
    String readTagData();
    String extractScentName(const String& data);
};

extern RFIDHandler rfidHandler;

#endif // RFID_HANDLER_H
