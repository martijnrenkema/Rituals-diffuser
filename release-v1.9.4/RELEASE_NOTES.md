# Rituals Diffuser Firmware v1.9.4

**ESP8266 Memory Optimization** - Significant stability improvements for ESP8266 through reduced memory usage and heap fragmentation prevention.

> **ESP8266 Users:** Versions v1.9.0 and above include NFC scent detection which is still **experimental** on ESP8266 due to memory constraints. If you experience instability or crashes, please use **[v1.8.5](https://github.com/martijnrenkema/Rituals-diffuser/releases/tag/v1.8.5)** as a stable fallback (without NFC detection).
>
> **ESP32/ESP32-C3 Users:** All features work reliably on ESP32 platforms.

## What's New

### Lite Status Endpoint
- New `/api/status/lite` endpoint for UI polling
- Uses `StaticJsonDocument` on **stack** instead of heap allocation
- Reduces response size from 870 bytes to 313 bytes (64% smaller)
- Eliminates heap fragmentation during normal operation

### Gzip Compression
- Web interface files are now gzip compressed
- Total size reduced from 62KB to 15KB (76% reduction)
- Faster page loads, less flash usage

### Additional Memory Optimizations
- **Logger::toJson()**: Pre-allocates string buffer with `reserve()` to prevent fragmentation from repeated concatenation
- **handleDiagnostic()**: Converted from `DynamicJsonDocument` (heap) to `StaticJsonDocument` (stack)
- **PROGMEM strings**: Captive portal responses moved to flash memory (~500 bytes RAM saved)
- **Fixed-size buffers**: Replaced `String` members with `char[]` arrays for pending WiFi/MQTT credentials

### Bug Fixes
- **Input validation**: WiFi/MQTT credential handlers now validate length limits (SSID: 1-32 chars, password: 8-63 chars, MQTT host: 1-64 chars) and return proper error messages instead of silently truncating
- **Fan control validation**: Numeric parameters (speed 0-100, timer 1-1440 min) are now validated to prevent unexpected behavior from malformed input
- **RFID memory leak**: Fixed potential memory leak if RFID is re-initialized (previous MFRC522 instance is now properly deleted)

### Technical Details
- `data/` folder now contains `.gz` files only
- Original source files preserved in `data_src/`
- Web UI polls `/api/status/lite` every 5 seconds
- Full `/api/status` only fetched at page load
- All frequently-called handlers now use stack-based JSON documents

## Download Files

| Platform | Firmware | Filesystem | Flash Addresses |
|----------|----------|------------|-----------------|
| ESP8266 (Genie V1/V2) | `firmware_esp8266.bin` | `littlefs_esp8266.bin` | `0x0` / `0x1E0000` |
| ESP32 DevKit | `firmware_esp32.bin` | `spiffs_esp32.bin` | `0x10000` / `0x3D0000` |
| ESP32-C3 SuperMini | `firmware_esp32c3.bin` | `spiffs_esp32c3.bin` | `0x10000` / `0x3D0000` |

## Installation

### Using esptool.py

```bash
# ESP8266 - Flash BOTH files:
esptool.py --port /dev/ttyUSB0 --chip esp8266 --baud 460800 \
  write_flash 0x0 firmware_esp8266.bin 0x1E0000 littlefs_esp8266.bin

# ESP32 - Flash BOTH files:
esptool.py --port /dev/ttyUSB0 --chip esp32 --baud 460800 \
  write_flash 0x10000 firmware_esp32.bin 0x3D0000 spiffs_esp32.bin

# ESP32-C3 - Flash BOTH files:
esptool.py --port /dev/ttyUSB0 --chip esp32c3 --baud 460800 \
  write_flash 0x10000 firmware_esp32c3.bin 0x3D0000 spiffs_esp32c3.bin
```

### OTA Update (Web Interface)

1. Open `http://rituals-diffuser.local` or device IP
2. Go to "Firmware Update" section
3. Upload the firmware `.bin` file
4. Upload the filesystem `.bin` file
5. Wait for restart

## Memory Comparison

| Metric | v1.9.3 | v1.9.4 | Improvement |
|--------|--------|--------|-------------|
| Poll response size | 870 bytes | 313 bytes | -64% |
| Poll heap allocation | 1280 bytes | 0 (stack) | -100% |
| Diagnostic heap alloc | 384 bytes | 0 (stack) | -100% |
| Web files (flash) | 62 KB | 15 KB | -76% |
| RAM (PROGMEM strings) | ~500 bytes | 0 | -100% |
| Pending credentials | ~200 bytes heap | ~260 bytes stack | No fragmentation |

## Checksums

```
SHA256:
firmware_esp8266.bin   ed6e296cd2a85d3028528f0a2424522d17c50ebc9e97892834b409cab949918f
littlefs_esp8266.bin   9c59e40e0dc1930b138233ebb65cc2594e4e183b720b76c52b60c5c5878fc480
firmware_esp32.bin     fd04caa5a5bfaaa447181c36127882f3d82d636869de8b924718fc033059dcb9
spiffs_esp32.bin       25432da2747b2481871f464a3bbdc81fad45d7109e5c464b9f20869ef75e7845
firmware_esp32c3.bin   3efe7a2656b3e6593d8fa3c6643f4bd8aa93939eac196db31fd3eaff39053049
spiffs_esp32c3.bin     25432da2747b2481871f464a3bbdc81fad45d7109e5c464b9f20869ef75e7845
```

## Feedback Requested

**ESP8266 users with NFC enabled:** Please report your experience with this version! The memory optimizations in v1.9.4 should significantly improve stability compared to v1.9.2/v1.9.3, but real-world testing is valuable.

Report issues at: https://github.com/martijnrenkema/Rituals-diffuser/issues
