# v1.9.6 - ESP8266 Update Checker & OTA Improvements

Firmware version now reliably appears in Home Assistant on ESP8266. Update UI enabled on all platforms. OTA upload page shows real-time progress.

## Stream-based Update Checker
- Replaced `getString()` with direct JSON stream parsing (`deserializeJson` from HTTP stream)
- Avoids allocating ~10KB GitHub API response as String - critical for ESP8266 low-heap situations
- Increased BearSSL rx buffer from 512 to 1024 bytes for more reliable TLS
- Added `HTTP/1.0` mode to force Content-Length headers (no chunked transfer)

## Update UI on ESP8266
- Update section now visible on ESP8266 web interface (was hidden since v1.8.0)
- Added extra polling timeouts (10s, 15s) for slower BearSSL HTTPS checks
- Dynamic release URL from API response
- Added `release_url` and `error` fields to main `/api/status` endpoint

## OTA Upload Progress
- Sync OTA page now uses XHR-based uploads with real-time progress bars
- Button disables during upload with "Do not interrupt!" warning
- Success/failure shown inline (no page reload)
- Removed PROGMEM success/fail HTML pages (saves flash)

## Build Info
| Platform | RAM | Flash |
|----------|-----|-------|
| ESP8266 | 78.5% | 74.3% |
| ESP32 | 21.6% | 71.5% |

## Binaries
| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
