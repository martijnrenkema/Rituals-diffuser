# v1.9.8 - Audit Fixes

Bug fixes identified during firmware audit. Improves MQTT persistence, fan state consistency, update error visibility, and build reliability.

## Changes

- **MQTT persistence:** Speed and interval mode changes via Home Assistant now survive reboot
- **Fan speed=0:** Setting speed to 0 on a running fan now correctly turns it off instead of leaving it "on" at 0%
- **Update checker:** Error messages stay visible in status until next check (were instantly cleared)
- **OTA auto-prepare:** Visiting /update.html no longer triggers safe mode automatically on ESP8266
- **Build fixes:** Removed broken `esp32c3_rfid_scan` environment, removed unused FastLED from ESP8266, fixed ESP32 WiFi TX power constant

## Resource Usage

| Platform | RAM | Flash |
|----------|-----|-------|
| ESP8266 | 77.8% | 71.8% |
| ESP32 | 21.6% | 71.6% |
| ESP32-C3 | 19.2% | 68.1% |

## Binaries

| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
| `firmware_esp32c3.bin` | ESP32-C3 SuperMini | `0x10000` |
| `spiffs_esp32c3.bin` | ESP32-C3 SuperMini | `0x3D0000` |
