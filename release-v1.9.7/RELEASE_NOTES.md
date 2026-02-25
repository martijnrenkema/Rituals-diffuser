# v1.9.7 - ESP8266 RAM Optimization

> **Experimental release** - For a stable ESP8266 experience, use [v1.8.5](https://github.com/martijnrenkema/Rituals-diffuser/releases/tag/v1.8.5).

## Changes

- **Disabled ArduinoOTA on ESP8266** to free RAM for more critical functions
  - ArduinoOTA ran as a background UDP service consuming memory unnecessarily
  - ESP8266 already has web-based Safe Update mode for OTA updates
  - ESP32 and ESP32-C3 retain ArduinoOTA (plenty of RAM available)

## Flash Addresses

| Chip | Firmware | Filesystem |
|------|----------|------------|
| ESP8266 | `0x0` | `0x1E0000` (LittleFS) |
| ESP32 | `0x10000` | `0x3D0000` (SPIFFS) |
| ESP32-C3 | `0x10000` | `0x3D0000` (SPIFFS) |

## Build Info

| Platform | RAM Usage | Flash Usage |
|----------|-----------|-------------|
| ESP8266 | 77.7% (63,676 / 81,920) | 71.8% |
| ESP32 | 21.6% (70,880 / 327,680) | 71.5% |
| ESP32-C3 | 19.2% (62,956 / 327,680) | 68.1% |
