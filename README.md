# Rituals Perfume Genie 2.0 - Custom Firmware

Custom firmware for the Rituals Perfume Genie 2.0 diffuser. Replaces the cloud-dependent Rituals firmware with fully local control via Home Assistant.

<p align="center">
  <img src="docs/images/web-interface.png" alt="Web Interface" width="250"/>
</p>

![Version](https://img.shields.io/badge/Version-1.7.4-brightgreen)
![ESP32](https://img.shields.io/badge/ESP32-Tested-blue)
![ESP8266](https://img.shields.io/badge/ESP8266-Untested-yellow)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Build-orange)
![Home Assistant](https://img.shields.io/badge/Home%20Assistant-MQTT-41BDF5)
![License](https://img.shields.io/badge/License-MIT-green)

> **Looking for ESP8266 testers!** The ESP8266 version builds successfully but has not been tested on actual hardware. If you have a Rituals Perfume Genie 2.0 with the original ESP-WROOM-02, please help test and [report any issues](https://github.com/martijnrenkema/Rituals-diffuser/issues).

## Features

- **Local Control** - No cloud dependency, works offline
- **Home Assistant Integration** - MQTT auto-discovery
- **Timer Presets** - 30, 60, 90, 120 minutes + continuous
- **Interval Mode** - Pulsing mode to save fragrance
- **Night Mode** - Auto-dim LED during configured hours
- **Usage Statistics** - Track total runtime
- **OTA Updates** - Wireless firmware updates via web interface
- **Auto-Update** - Checks GitHub for new releases, one-click install (ESP32)
- **Web Interface** - Configure WiFi, MQTT, passwords, and control the diffuser
- **RGB LED Status** - Visual feedback for device state
- **Physical Buttons** - Front and rear button support

## Quick Start

### Option 1: Pre-built Binaries (Easiest)

1. Download the latest release from [Releases](https://github.com/martijnrenkema/Rituals-diffuser/releases)
2. Flash using esptool or web flasher (see Installation section)

### Option 2: Build from Source

```bash
# Clone repository
git clone https://github.com/martijnrenkema/Rituals-diffuser.git
cd Rituals-diffuser

# Build for ESP32 (recommended)
pio run -e esp32dev

# Or build for ESP8266 (original chip)
pio run -e esp8266
```

## Hardware

> **ℹ️ Developed for Rituals Perfume Genie 2.0**
>
> This firmware is developed for the **Rituals Perfume Genie 2.0** which contains an ESP-WROOM-02 (ESP8266) chip. Have a different hardware version? Feel free to try and [share your feedback](https://github.com/martijnrenkema/Rituals-diffuser/issues)!

This firmware supports both **ESP32** (recommended) and the original **ESP-WROOM-02** (ESP8266).

### ESP32 Wiring (Recommended for new builds)

Connect your ESP32 DevKit to the Rituals Genie board:

| ESP32 GPIO | Genie Board | Wire Color | Function |
|------------|-------------|------------|----------|
| GPIO25 | IO4 | Blue | Fan PWM speed control |
| GPIO26 | IO5/TP17 | Yellow | Fan tachometer (RPM) |
| GPIO27 | IO15 | Green | WS2812 RGB LED data |
| GPIO13 | SW2 | - | Front button (Connect) |
| GPIO14 | SW1 | - | Rear button (Reset) |
| GND | GND | Black | Ground |
| 3.3V | 3.3V | Red | Power |

> **⚠️ Important: Antenna Placement**
>
> When installing an ESP32 dev board inside the metal housing, position the board so the WiFi antenna points toward the nozzle opening. The metal enclosure acts as a Faraday cage, blocking WiFi signals. The nozzle opening is the only path for the signal to escape.

### ESP8266 Pinout (Original chip)

| GPIO | Function | Description |
|------|----------|-------------|
| GPIO4 | Fan PWM | Speed control (blue wire) |
| GPIO5 | Fan Tacho | RPM feedback (yellow wire) |
| GPIO15 | LED | WS2812 RGB LED |
| GPIO14 | SW2 | Connect button |
| GPIO13 | SW1 | Cold reset button |

## Installation

### Step 1: Backup Original Firmware (Important!)

Before flashing, backup the original Rituals firmware:

```bash
# Find your serial port
ls /dev/cu.usbserial-*

# Backup (2MB flash for ESP8266)
esptool.py --port /dev/cu.usbserial-XXXX read_flash 0x00000 0x200000 rituals_backup.bin
```

### Step 2: Flash Firmware

#### Method A: Using PlatformIO (Recommended)

```bash
# For ESP32
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX
pio run -e esp32dev -t uploadfs --upload-port /dev/cu.usbserial-XXXX

# For ESP8266
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX
```

#### Method B: Using esptool (Pre-built binaries)

> **⚠️ You must flash TWO files: firmware + filesystem (web interface)**
>
> | Chip | File | Address |
> |------|------|---------|
> | ESP8266 | `firmware_esp8266.bin` | `0x0` |
> | ESP8266 | `littlefs_esp8266.bin` | `0x1E0000` |
> | ESP32 | `firmware_esp32.bin` | `0x10000` |
> | ESP32 | `spiffs_esp32.bin` | `0x3D0000` |
>
> **Web flashers don't work!** Tools like ESPHome Flasher only flash to one address. Use `esptool.py` instead.
>
> Install esptool: `pip install esptool` ([Windows/Mac/Linux guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html))

```bash
# For ESP8266 - flash BOTH files:
esptool.py --port /dev/cu.usbserial-XXXX --chip esp8266 --baud 460800 \
  write_flash 0x0 firmware_esp8266.bin 0x1E0000 littlefs_esp8266.bin

# For ESP32 - flash BOTH files:
esptool.py --port /dev/cu.usbserial-XXXX --chip esp32 --baud 460800 \
  write_flash 0x10000 firmware_esp32.bin 0x3D0000 spiffs_esp32.bin
```

You can also flash them separately:
```bash
# ESP8266
esptool.py --port /dev/cu.usbserial-XXXX --chip esp8266 write_flash 0x0 firmware_esp8266.bin
esptool.py --port /dev/cu.usbserial-XXXX --chip esp8266 write_flash 0x1E0000 littlefs_esp8266.bin

# ESP32
esptool.py --port /dev/cu.usbserial-XXXX --chip esp32 write_flash 0x10000 firmware_esp32.bin
esptool.py --port /dev/cu.usbserial-XXXX --chip esp32 write_flash 0x3D0000 spiffs_esp32.bin
```

### Step 3: Initial Setup

1. Power on the device - LED will pulse orange (AP mode)
2. Connect to WiFi network: `Rituals-Diffuser-XXXX`
3. Password: `diffuser123`
4. Open browser: `http://192.168.4.1`
5. Configure your WiFi credentials
6. Device restarts and connects to your network

### Step 4: Configure MQTT (Optional)

1. Find device IP in your router or use `rituals-diffuser.local`
2. Open web interface
3. Enter MQTT broker settings
4. Device appears automatically in Home Assistant

## Updating Firmware (OTA)

Once installed, you can update wirelessly using one of these methods:

### Method 1: Web Interface (Easiest)

1. Open web interface: `http://rituals-diffuser.local` or device IP
2. Click "Firmware Update" at bottom
3. Upload the firmware `.bin` file
4. Upload the filesystem `.bin` file (optional, for web interface updates)
5. Wait for restart

### Method 2: PlatformIO OTA (Developers)

After the first serial flash, use OTA for subsequent updates:

```bash
# ESP8266 - Firmware
pio run -e esp8266_ota -t upload

# ESP8266 - Filesystem (LittleFS)
pio run -e esp8266_ota -t uploadfs

# ESP32 - Firmware
pio run -e esp32_ota -t upload

# ESP32 - Filesystem (SPIFFS)
pio run -e esp32_ota -t uploadfs
```

**Requirements:**
- Device must be on same network as your computer
- Default hostname: `rituals-diffuser.local`
- Default OTA password: `diffuser-ota` (configurable in web UI → Security)
- OTA port: 3232 (ESP32) / 8266 (ESP8266)

> **Note:** OTA updates don't require flash addresses - the ESP framework handles this automatically.

### Method 3: Manual OTA with espota.py

For firmware updates without PlatformIO:

```bash
# Download espota.py from Arduino ESP32/ESP8266 repository
python espota.py -i <device-ip> -p 3232 -a diffuser-ota -f firmware.bin
```

> **Tip:** For filesystem updates, use the web interface instead - it's easier than espota.py for SPIFFS/LittleFS.

### ESP32 Dual Partition Safety

The ESP32 uses a dual OTA partition scheme for safe updates:

| Partition | Address | Size | Purpose |
|-----------|---------|------|---------|
| app0 (ota_0) | 0x10000 | 1.9MB | OTA slot 0 |
| app1 (ota_1) | 0x1F0000 | 1.9MB | OTA slot 1 |

**How it protects your device:**
1. New firmware is written to the **inactive** partition
2. If write completes successfully, bootloader switches to new partition
3. If update fails mid-write, old partition remains active → **device keeps working**

> **Note:** ESP8266 has a single app partition (no rollback) due to its limited 2MB flash.

## Home Assistant Integration

### MQTT Auto-Discovery

The device automatically appears in Home Assistant when MQTT auto-discovery is enabled. No manual configuration needed!

<p align="center">
  <img src="docs/images/home-assistant.png" alt="Home Assistant MQTT Integration" width="700"/>
</p>

### Entities Created

| Entity | Type | Description |
|--------|------|-------------|
| Diffuser | Fan | On/off, speed 0-100%, timer presets |
| Interval Mode | Switch | Pulsing mode toggle |
| Interval On | Number | On-time (10-120 sec) |
| Interval Off | Number | Off-time (10-120 sec) |
| Time Left | Sensor | Remaining timer minutes |
| Fan RPM | Sensor | Current fan speed |
| WiFi Signal | Sensor | Signal strength (dBm) |
| Total Runtime | Sensor | Total device runtime (hours) |

### Timer Presets

- 30 minutes
- 60 minutes
- 90 minutes
- 120 minutes
- Continuous

## Button Controls

### Front Button (SW2 - Connect)
| Action | Function |
|--------|----------|
| Short press | Toggle fan on/off |
| Long press (3s) | Start AP mode for WiFi config |

### Rear Button (SW1 - Cold Reset)
| Action | Function |
|--------|----------|
| Short press | Restart device |
| Long press (3s) | Factory reset (clears all settings) |

## LED Status Indicators

| Color | Pattern | Status |
|-------|---------|--------|
| Red | Blinking | Disconnected / Error |
| Cyan | Fast blink | Connecting to WiFi |
| Green | Solid | Fan running |
| Blue | Solid | Timer active |
| Blue | Slow breathing | Timer + Interval combined |
| Purple | Solid | Interval mode active |
| Orange | Pulsing | AP mode (WiFi config) |
| Purple | Fast blink | OTA update in progress |

## Configuration

### Default Passwords

| Function | Default Password | Changeable |
|----------|------------------|------------|
| WiFi AP | `diffuser123` | Yes |
| OTA Updates | `diffuser-ota` | Yes |

Change passwords in web interface under "Security". Minimum 8 characters. Restart required after change.

### Night Mode

Automatically dims the LED during specified hours:
- Configure start/end hour (0-23)
- Set dimmed brightness (0-100%)
- Enable/disable via web interface

## Troubleshooting

### Device won't connect to WiFi
1. Long press front button (3s) to enter AP mode
2. Connect to `Rituals-Diffuser-XXXX`
3. Reconfigure WiFi settings

### Device not appearing in Home Assistant
1. Verify MQTT broker settings
2. Check MQTT broker is reachable
3. Power cycle the device
4. Wait 30 seconds for discovery

### OTA upload fails
1. Ensure device is on same network
2. Try using IP address instead of hostname
3. Check port 3232 is not blocked
4. Fallback: flash via serial connection

### Fan not spinning
1. Check wiring connections
2. Go to Hardware Diagnostics in web interface
3. Try "Test Cycle" to verify fan works
4. Adjust Min PWM if fan needs higher starting voltage

<p align="center">
  <img src="docs/images/hardware-diagnostics.png" alt="Hardware Diagnostics" width="250"/>
</p>

### Can't access 192.168.4.1 (AP mode)

**First, verify you're in AP mode:**
- LED should be **orange pulsing**
- WiFi network `Rituals-Diffuser-XXXX` should be visible
- Password: `diffuser123`

**If AP mode won't start:**
1. AP mode only activates when:
   - No WiFi credentials saved, OR
   - WiFi connection fails 3x (takes ~90 seconds), OR
   - Long press front button (3 seconds)
2. Check serial log for `[WIFI] AP started` and `[WIFI] AP IP: 192.168.4.1`
3. If `[WIFI] ERROR: Failed to start AP!` appears, try factory reset (long press rear button)

**If connected but page won't load:**
1. Use `http://192.168.4.1/` (not https!)
2. Check your phone's WiFi details - Gateway should show `192.168.4.1`
3. Disable mobile data temporarily
4. Try a different browser or device

**If you see "Web interface files missing":**
- You need to flash the filesystem (the web interface files)
- Download `littlefs_esp8266.bin` (ESP8266) or `spiffs_esp32.bin` (ESP32) from the [latest release](https://github.com/martijnrenkema/Rituals-diffuser/releases)
- Flash via web interface (Firmware Update) or esptool:
  ```bash
  # ESP8266: filesystem offset is 0x1E0000
  esptool.py write_flash 0x1E0000 littlefs_esp8266.bin
  ```

**Serial debug commands:**
```bash
# Monitor serial output (115200 baud)
pio device monitor -b 115200

# Or with screen
screen /dev/ttyUSB0 115200
```

Look for these log messages:
- `[WIFI] AP started: Rituals-Diffuser-XXXX` - AP is running
- `[WIFI] AP Password: diffuser123` - Password being used
- `[WIFI] AP IP: 192.168.4.1` - IP address assigned
- `[WEB] Server started on port 80` - Web server ready
- `[WIFI] DNS server started for captive portal` - Captive portal active

## Project Structure

```
├── src/
│   ├── main.cpp              # Main entry point
│   ├── config.h              # Pin definitions & settings
│   ├── fan_controller.*      # Fan control, timer, interval
│   ├── led_controller.*      # WS2812 RGB LED
│   ├── button_handler.*      # Button input handling
│   ├── storage.*             # Settings persistence
│   ├── wifi_manager.*        # WiFi connection
│   ├── web_server.*          # Web interface + OTA
│   ├── mqtt_handler.*        # MQTT + HA discovery
│   └── ota_handler.*         # ArduinoOTA
├── data/                     # Web files (LittleFS on ESP8266, SPIFFS on ESP32)
│   ├── index.html
│   ├── update.html
│   ├── style.css
│   └── script.js
├── platformio.ini
└── README.md
```

## Building from Source

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-Serial adapter

### Build Commands

```bash
# Build firmware
pio run -e esp32dev        # ESP32
pio run -e esp8266         # ESP8266

# Build filesystem
pio run -e esp32dev -t buildfs
pio run -e esp8266 -t buildfs

# Upload firmware
pio run -e esp32dev -t upload
pio run -e esp8266 -t upload

# Upload filesystem
pio run -e esp32dev -t uploadfs
pio run -e esp8266 -t uploadfs
```

## Dependencies

- [PubSubClient](https://github.com/knolleary/pubsubclient) - MQTT client
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - JSON parsing
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Async web server
- [FastLED](https://github.com/FastLED/FastLED) - WS2812 LED control

## Credits

- Based on research from the [Home Assistant Community](https://community.home-assistant.io/t/replace-the-software-of-the-rituals-genie-esp-with-esphome/762356)
- Inspired by [Echnics/Perfume-Genie-ESPhome](https://github.com/Echnics/Perfume-Genie-ESPhome)

## License

MIT License - feel free to use and modify.

## Disclaimer

This project is not affiliated with Rituals Cosmetics. Use at your own risk. Modifying your device may void warranty.

## Changelog

### v1.8.0
**Rituals Genie V1 (ESP8266) Now Fully Supported!**

This release adds complete support for the original Rituals Perfume Genie V1 with its ESP8266 chip. All features now work reliably on this memory-constrained platform.

**What's New:**
- **Full V1 Support**: Rituals Genie V1 (ESP8266) now works alongside V2 and custom ESP32 boards
- **Stable LED Status**: Fixed LED flickering issues on ESP8266 - status lights now work correctly
- **Working Web OTA**: Web-based firmware updates now work reliably on ESP8266 via "Safe Update Mode"
- **Improved Safe Update Mode UI**: Modern styling matching the main interface, with GitHub releases link
- **Exit Safe Mode**: Added restart button to exit safe update mode without uploading
- **Reduced Memory Usage**: Removed automatic update checking on ESP8266 (not enough RAM for HTTPS)

**Supported Hardware:**
| Device | Chip | Auto-Update | OTA Rollback |
|--------|------|-------------|--------------|
| Rituals Genie V1 | ESP8266 | Manual (GitHub link) | No |
| Rituals Genie V2 | ESP8266 | Manual (GitHub link) | No |
| Custom ESP32 board | ESP32 | Yes (automatic) | Yes (dual partition) |

**ESP32 Advantages:**
- Automatic GitHub update checking
- One-click firmware installation
- Automatic rollback on failed updates (dual partition)

### v1.7.6
- **Fix web OTA crash on ESP8266**: Initial attempt at fixing AsyncWebServer OTA crashes

### v1.7.0
**Major Feature Release:**
- Web-based firmware updates
- Night mode with scheduled LED dimming
- MQTT Home Assistant auto-discovery
- Captive portal for easy WiFi setup

For older versions, see [GitHub Releases](https://github.com/martijnrenkema/Rituals-diffuser/releases).
