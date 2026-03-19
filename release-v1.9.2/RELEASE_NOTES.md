# v1.9.2 - Scent Recognition Fix & NFC Debugging

This release fixes scent pattern recognition for cartridges that use capitalized codes and adds debugging tools for NFC issues.

## What's New

### Scent Recognition Fix
- **Fix capitalized scent codes**: Rituals cartridges can use uppercase first letters (e.g., "Jin" instead of "jin"). Added uppercase hex variants for all scents.
- **Fixes issue #7**: Jing (`4A696E` = "Jin") and Sakura (`53616B` = "Sak") cartridges now correctly identified instead of showing "Unknown".

### NFC Debugging Improvements
- **Improved RC522 detection**: More robust initialization with hardware reset and multiple version register reads
- **API debug info**: Added `rfid.version_reg` to `/api/status` endpoint for remote debugging
- **Better serial logging**: Detailed output during RFID initialization to help diagnose connection issues

## Troubleshooting NFC

If scent detection doesn't work, check `http://[device-ip]/api/status` and look at `rfid.version_reg`:
- `0x91` (v1.0), `0x92` (v2.0), or `0x88` (clone) = RC522 detected correctly
- `0x00` or `0xFF` = No communication (check wiring)

## Download

| Platform | Firmware | Filesystem |
|----------|----------|------------|
| ESP8266 | `firmware_esp8266.bin` | `littlefs_esp8266.bin` |
| ESP32 | `firmware_esp32.bin` | `spiffs_esp32.bin` |

## Flash Addresses

| Chip | Firmware | Filesystem |
|------|----------|------------|
| ESP8266 | `0x0` | `0x1E0000` |
| ESP32 | `0x10000` | `0x3D0000` |

## Installation

**Via Web Interface (recommended):**
1. Go to `http://<device-ip>/update`
2. Upload firmware first, then filesystem

**Via Serial (ESP8266):**
```bash
esptool.py --port /dev/cu.usbserial-XXXX --baud 57600 --chip esp8266 --no-stub write_flash --no-compress 0x0 firmware_esp8266.bin
esptool.py --port /dev/cu.usbserial-XXXX --baud 57600 --chip esp8266 --no-stub write_flash --no-compress 0x1E0000 littlefs_esp8266.bin
```
