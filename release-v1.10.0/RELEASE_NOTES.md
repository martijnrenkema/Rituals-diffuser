# v1.10.0 - Reliability & Safety

Bug fixes from a full code review, a safer factory reset on the rear button, and CSRF protection for the web API.

## Bug Fixes

- **AP mode via front button stayed open for only a few seconds:** the background WiFi retry fired immediately after a long press and closed the AP again. The retry now waits 5 minutes.
- **Total runtime double-counted in Home Assistant:** runtime saved every 30 minutes was added a second time. Web UI and MQTT now report the same value.
- **Wrong handler for `/api/status/lite` and `/api/diagnostic/buttons`:** the web server's prefix matching sent them to `/api/status` and `/api/diagnostic`. Status polling now really uses the light endpoint, and the diagnostics button test works again.
- **Stuck OTA state:** a failed or interrupted web upload left the LED blinking purple until reboot. Failed uploads are now aborted and cleaned up, a dropped connection is detected, and stalled uploads time out after 30 seconds.
- **ESP32: web upload during another update:** a web upload started while ArduinoOTA or the GitHub updater was running could abort that update from another task. Such uploads are now rejected (409).
- **ESP8266: crash on direct upload:** `/api/update/firmware` and `/api/update/filesystem` crashed the device when called directly (the updater can't run in the async network context). They now return 400. The web UI already used Safe Update mode, so nothing changes there.
- **ESP8266 Safe Update mode after an interrupted upload:** every retry failed until the device was power-cycled. The updater is now reset when an upload is aborted.
- **Filesystem corruption risk during upload:** the filesystem is unmounted and log writes are paused while a filesystem image is uploaded through the web UI.
- **Night mode brightness 0%:** the LED turned back on at 50% on the next status change. 0% now keeps the LED off at night.
- **Inconsistent daytime brightness:** the LED ran at 50% without night mode and 100% with night mode enabled. It is now 100% in both cases.
- **Default OTA password:** back to `diffuser-ota` as documented (was derived from the MAC address).
- **WiFi reconnect status:** when WiFi comes back on its own, the LED and MQTT recover immediately instead of after up to 60 seconds.
- **MQTT settings validation:** host/user/password length limits now match the storage size (no silent truncation of the last character).

## ESP8266 RAM

Static RAM usage drops from 75.7% to 61.6%: about 12 KB more free heap, which makes out-of-memory crashes much less likely and leaves room for the HTTPS update check.

- **Unused `ASYNCWEBSERVER_REGEX` build flag removed:** no web route uses regular expressions, but the flag pulled in `std::regex` and the C++ locale tables (~11 KB RAM, ~190 KB flash on ESP8266; ~5 KB RAM, ~240 KB flash on ESP32). Web behaviour is unchanged.
- **EEPROM buffer freed after use:** the settings copy is only held in RAM while reading or writing (~440 bytes heap).
- **Leaner update check:** ESP8266 no longer parses the release asset list it never uses (~1 KB less heap during the TLS check).
- **ESP32-only update URLs** no longer reserved on ESP8266 (392 bytes).

## Improvements

- **Reproducible builds:** platform and library versions are pinned exactly in `platformio.ini`.

- **Rear button factory reset:** needs a 5 second hold. After 1 second the LED blinks red slowly as a warning; releasing the button cancels. At 5 seconds the LED blinks red fast to confirm, then the settings are cleared. A short press still restarts the device.
- **CSRF protection:** state-changing requests sent by a browser from another website (Origin/Referer header not matching the device address) are rejected with 403. A malicious web page can no longer reset or reflash the diffuser through your browser. The web UI, curl and scripts are unaffected.
- **ESP32 thread safety:** web handlers and the main loop are serialized with a mutex, so web requests no longer race with fan, LED, MQTT and storage updates.
- **ESP8266 Safe Update mode:** turns the fan off when entered and restarts automatically after 10 minutes without activity.
- **Less flash wear:** fan speed is saved 5 seconds after the last change instead of on every slider step.
- **Web OTA upload** turns the fan off. **Fan calibration** saves pending runtime and updates LED/MQTT.
- **Fewer filesystem lookups:** API routes are matched before static files.
- **MQTT reconnect backoff** capped at 60 seconds as intended.

## Resource Usage

| Platform | RAM | Flash |
|----------|-----|-------|
| ESP8266 | ~62% | ~54% |
| ESP32 | ~20% | ~60% |
| ESP32-C3 | ~18% | ~57% |

## Binaries

| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
| `firmware_esp32c3.bin` | ESP32-C3 SuperMini | `0x10000` |
| `spiffs_esp32c3.bin` | ESP32-C3 SuperMini | `0x3D0000` |

## Notes

- **ESP32 / ESP32-C3 with ArduinoOTA:** if you never set your own OTA password and use `espota.py` or `pio run -t upload` over the network, the password is now `diffuser-ota` (was `ota-` + last 6 hex digits of the MAC address).
- **Reverse proxy:** if you access the web UI through a reverse proxy that rewrites the `Host` header, settings changes are rejected (403) by the new cross-site check. Direct access by IP or `rituals-diffuser.local` is not affected.
- Updating the filesystem is optional for this release (only the speed slider debounce changed), but recommended.
