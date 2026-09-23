# v1.11.0 - New Web Interface

A redesigned, professional web interface for all platforms (ESP8266, ESP32, ESP32-C3).

## New web interface

- **Three tabs:** Control, Settings and Firmware. A tab bar at the bottom on phones, a sidebar on desktop.
- **Light and dark mode:** follows your phone or computer setting automatically.
- **Cleaner look:** Inter (or your system font), neutral greys with one blue accent, compact cards.
- **Control:** large fan speed dial with RPM, power button, scent cartridge, timer presets and interval mode. Interval times now save automatically.
- **Settings:** a grouped list that shows the current state at a glance (WiFi network and signal, MQTT broker, night mode hours, firmware version, total runtime). Tap a row to edit it.
- **Firmware:** installed and latest version, update check, one-click install on ESP32 / ESP32-C3, and manual upload of firmware and web interface files. `update.html` now redirects here, so old links keep working.
- **Notifications** replace pop-up alerts for saved settings and errors.
- **Smaller:** all web files together are 14.4 KB gzipped (was 15.5 KB).
- **ESP8266 Safe Update page** restyled to match, including dark mode.

## Firmware

- New `POST /api/restart` endpoint, used by "Restart device" in Settings.
- ESP32-C3 reports its platform as `ESP32-C3` (was `ESP32`).

## Updating

Update **both** files: the new web interface needs this firmware (for "Restart device"), and the firmware's web pages live in the filesystem image.

1. Open the web interface and upload the firmware file, then the filesystem file:
   - v1.10.0: via "Firmware Update" at the bottom of the page.
   - v1.11.0: via the Firmware tab.
2. ESP32 / ESP32-C3: "Install update" downloads and installs both files from GitHub automatically.

## Binaries

| File | Platform | Flash Address |
|------|----------|---------------|
| `firmware_esp8266.bin` | ESP8266 | `0x0` |
| `littlefs_esp8266.bin` | ESP8266 | `0x1E0000` |
| `firmware_esp32.bin` | ESP32 | `0x10000` |
| `spiffs_esp32.bin` | ESP32 | `0x3D0000` |
| `firmware_esp32c3.bin` | ESP32-C3 SuperMini | `0x10000` |
| `spiffs_esp32c3.bin` | ESP32-C3 SuperMini | `0x3D0000` |
