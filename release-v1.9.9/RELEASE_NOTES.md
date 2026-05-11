# v1.9.9 - Stability & Cleanup

Bug fixes and cleanup pass over v1.9.8. No new features.

## Changes

- **MQTT discovery cleanup:** `removeDiscovery()` now wipes all 13 entities. Home Assistant no longer keeps orphaned entries (`total_runtime`, `update_available`, `latest_version`, `current_version` were missing before).
- **Anonymous MQTT brokers:** passes `nullptr` for empty user/password so brokers that enforce anonymous-only access can connect.
- **Update checker retries on ESP8266:** if the first check fails, retries once per hour instead of staying stuck until reboot. Low-heap floor raised to 18 KB.
- **ESP8266 OTA upload size fix:** uses `maxSketchSpace` instead of multipart `contentLength`, so `Update.begin` no longer fails on slightly oversized requests.
- **Logger streams to /api/logs** instead of building the full JSON in heap first. Saves ~3 KB on ESP8266.
- **Logger save retry throttled** to one save-interval after a failed write. A stuck filesystem no longer hammers `saveToFile()` on every loop.
- **Fan calibration race fix:** speed/on/off commands during auto-calibration are now ignored.
- **Interval times clamped before storage:** out-of-range MQTT input (e.g. `interval_on/set = 5000`) no longer wraps to a garbage value in NVS.
- **Night mode brightness applies immediately:** changing brightness during an active night-mode period no longer waits for the next day/night transition.
- **RFID:** unknown cartridges show "Unknown cartridge" instead of leaking raw page-4 bytes. Ambiguous hex matches logged with a warning.
- **Cleanup:** removed dead fields, unused OTA upload-tracking variables, and the unused `getDeviceJson()` helper.

## Resource Usage

| Platform | RAM | Flash |
|----------|-----|-------|
| ESP8266 | ~77% | ~71% |
| ESP32 | ~22% | ~71% |
| ESP32-C3 | ~19% | ~68% |

## Binaries

| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
| `firmware_esp32c3.bin` | ESP32-C3 SuperMini | `0x10000` |
| `spiffs_esp32c3.bin` | ESP32-C3 SuperMini | `0x3D0000` |

> **You must flash TWO files:** firmware + filesystem. See [Installation guide](https://github.com/martijnrenkema/Rituals-diffuser#installation) for details.
