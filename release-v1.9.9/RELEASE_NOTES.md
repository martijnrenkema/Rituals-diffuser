# v1.9.9 - Stability & Cleanup

Multi-pass code review fixes. Focus on MQTT correctness, heap stability on ESP8266, fan safety during calibration, and clearer status reporting. No new features.

## Changes

### MQTT
- **`removeDiscovery()` complete**: now publishes empty payloads for all 13 entities (was missing `total_runtime`, `update_available`, `latest_version`, `current_version`). Home Assistant no longer keeps orphan entities after factory reset.
- **Anonymous broker support**: passes `nullptr` for empty user/password instead of empty strings. Brokers that enforce anonymous access (e.g. `allow_anonymous true` with ACLs) now connect.
- **Scent publish heap-allocation removed**: added `rfidGetLastScentCStr()` so the periodic STATE_SCENT publish no longer creates a temporary String every 30 s.
- **Interval times clamped before storage**: out-of-range MQTT input like `interval_on/set = 5000` no longer wraps to a garbage `uint8_t` value in NVS/EEPROM.

### Logger
- **`/api/logs` streams JSON** instead of building a ~3 KB String in heap first. Stream buffer pre-allocates 4 KB on ESP8266 to avoid mid-response reallocs.
- **Failed save retries are throttled** to one attempt per save interval. Previously a stuck filesystem with an urgent flag would call `saveToFile()` every loop iteration.
- **Urgent flag survives a failed save**: a transient FS error no longer silently downgrades an ERROR/WARN entry.

### Update checker
- **ESP8266 retries once per hour** after a failed first check. Previously `_lastAutoCheck` was set before the check, so a failed first attempt blocked all subsequent auto-checks until reboot.
- **Wraparound-proof success flag**: `_hasSucceededOnce` boolean replaces the `_info.lastCheckTime == 0` check, which could false-positive after ~49.7 days of uptime when `millis()` wraps.
- **Low-heap floor raised to 18 KB** to account for BearSSL (~12-15 KB) plus the 1.5 KB JSON doc and HTTP buffers (~2 KB). Was 15 KB - the bare minimum for BearSSL alone.

### Web server
- **ESP8266 firmware OTA uses `maxSketchSpace`** instead of `request->contentLength()`, matching the sync OTA path. Multipart-aware contentLength could exceed the actual firmware size and cause Update.begin to fail.
- **`/api/status` document bumped to 1536 bytes on ESP32**. Long `releaseUrl` + `errorMessage` + scent name combined could push past 1408 and yield an empty 500 response.

### Fan controller
- **Calibration fencing**: `setSpeed()`, `turnOn()`, and `turnOff()` are now ignored while auto-calibration is in progress. A concurrent UI/MQTT command no longer races the calibration ramp.

### RFID
- **Unknown cartridges report "Unknown cartridge"** instead of leaking the raw page-4 ASCII interpretation (which was usually mostly dots, e.g. `Unknown: ..6`). Hex/ASCII dump kept in serial log for debugging.
- **Ambiguous hex matches logged**: the scent lookup now warns when a tag matches multiple table entries with different scent names, helping surface accidental collisions.

### Main / LED
- **Night mode brightness change applies immediately**: UI tweaks during an active night-mode period no longer have to wait for the next day/night transition. `checkNightMode()` accepts a force flag.
- **Fan state-change callback uses `requestStatePublish()`** instead of `publishState()` directly, going through the same MQTT synchronisation entry point as the rest of the code.
- **LedController guards against re-init leak**: deletes the existing NeoPixelBus instance before allocating a new one.

### Cleanup
- Dead fields removed: `_targetSpeed`, `_lastShownColor`, `updateContentLength`, `updateIsFS`.
- Unused `getDeviceJson()` and its ArduinoJson include removed from `mqtt_handler.cpp`.
- `Logger::saveToFile()` now returns bool.

## Resource Usage

| Platform | RAM | Flash |
|----------|-----|-------|
| ESP8266 | ~77% | ~71% |
| ESP32 | ~22% | ~71% |
| ESP32-C3 | ~19% | ~68% |

(Approximate - confirm after building locally.)

## Binaries

| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
| `firmware_esp32c3.bin` | ESP32-C3 SuperMini | `0x10000` |
| `spiffs_esp32c3.bin` | ESP32-C3 SuperMini | `0x3D0000` |
