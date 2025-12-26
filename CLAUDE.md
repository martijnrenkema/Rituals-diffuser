# Rituals Perfume Genie 2.0 - Development Documentation

## Repository

**GitHub:** https://github.com/martijnrenkema/Rituals-diffuser

## Hardware Platform

**Target Device:** Rituals Perfume Genie 2.0
**Microcontroller:** ESP-WROOM-02 (ESP8266)
**Framework:** Arduino via PlatformIO

### GPIO Pinout
| GPIO | Function | Description |
|------|----------|-------------|
| GPIO4 | FAN_PWM_PIN | Fan on/off control |
| GPIO5 | FAN_SPEED_PIN | Fan speed PWM |
| GPIO15 | LED_DATA_PIN | WS2812B RGB LED |
| GPIO16 | BUTTON_FRONT_PIN | "Connect" button |
| GPIO3 | BUTTON_REAR_PIN | Rear button (RX pin) |

### RFID Reader (RC522)
The device contains an NXP RC522 RFID/NFC reader for cartridge detection.
SPI pins are auto-detected via the "Scan RFID" function in the web interface.

## IMPORTANT: Backup Original Firmware

**Before flashing custom firmware for the first time, backup the original Rituals firmware!**

### Backup Procedure
```bash
# Connect USB-to-serial adapter and identify port
ls /dev/cu.usbserial-*

# Read and save original firmware (2MB flash)
esptool.py --port /dev/cu.usbserial-XXXX read_flash 0x00000 0x200000 rituals_original_firmware.bin

# Save this file in a safe location!
```

### Restore Original Firmware
```bash
# Erase flash first
esptool.py --port /dev/cu.usbserial-XXXX erase_flash

# Write original firmware back
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x00000 rituals_original_firmware.bin
```

## Passwords & Security

### Default Passwords
| Function | Default Value | Configurable |
|----------|---------------|--------------|
| WiFi AP | diffuser123 | Yes, via web interface |
| OTA Updates | diffuser-ota | Yes, via web interface |

### Changing Passwords
Passwords can be changed via the web interface:
1. Connect to the device (WiFi or AP mode)
2. Open http://<device-ip>/ in browser
3. Expand "Security" section
4. Enter new password (minimum 8 characters)
5. Click "Save Passwords"
6. **Restart device** to apply changes

## OTA (Over-The-Air) Updates

### Configuration
- **Hostname:** rituals-diffuser.local
- **Port:** 3232
- **Password:** Configurable (default: diffuser-ota)

### Commands
```bash
# Flash via OTA (after initial serial flash)
pio run -e esp8266_ota -t upload

# Initial serial flash (first time only)
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Upload web files (SPIFFS)
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX
```

## WiFi Configuration

### AP Mode
- **SSID:** Rituals-Diffuser-XXXX (last 4 hex of MAC)
- **Password:** Configurable (default: diffuser123)
- **IP:** 192.168.4.1

## MQTT Integration

### Home Assistant Discovery
Device automatically registers with Home Assistant via MQTT discovery.

### Entities Created
| Entity | Type | Description |
|--------|------|-------------|
| Diffuser | Fan | On/off, speed 0-100%, presets |
| Interval Mode | Switch | Toggle pulsing mode |
| Interval On | Number | On-time in seconds (10-120) |
| Interval Off | Number | Off-time in seconds (10-120) |
| Time Left | Sensor | Remaining timer minutes |
| Fan RPM | Sensor | Current RPM (diagnostic) |
| WiFi Signal | Sensor | Signal strength in dBm |
| Cartridge | Sensor | Current scent name (via RFID) |
| Total Runtime | Sensor | Total device runtime in hours |
| Cartridge Runtime | Sensor | Runtime since cartridge change |

### Timer Presets
- 30 minutes
- 60 minutes
- 90 minutes
- 120 minutes
- Continuous

## Building

### Build Commands
```bash
# Build firmware
pio run -e esp8266

# Build and upload via serial
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Build filesystem
pio run -e esp8266 -t buildfs

# Upload filesystem via serial
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX

# Build and upload via OTA
pio run -e esp8266_ota -t upload
```

## Web Interface API

### Endpoints
| Endpoint | Method | Description |
|----------|--------|-------------|
| /api/status | GET | Get device status (includes all data) |
| /api/wifi | POST | Set WiFi credentials |
| /api/mqtt | POST | Set MQTT configuration |
| /api/fan | POST | Control fan (power, speed, timer, interval) |
| /api/passwords | GET | Get password status |
| /api/passwords | POST | Set OTA/AP passwords |
| /api/rfid | GET | Get RFID/cartridge status |
| /api/rfid | POST | RFID actions (scan, clear) |
| /api/night | GET | Get night mode settings |
| /api/night | POST | Set night mode settings |
| /api/reset | POST | Factory reset |

## Features

### Night Mode
Automatically dims LED during configured hours:
- Configurable start/end hours (default: 22:00 - 07:00)
- Adjustable LED brightness (default: 10%)
- Uses NTP for accurate time sync

### RFID Cartridge Detection
- Auto-scans SPI pins to find RC522 reader
- Detects Rituals cartridge RFID tags
- Extracts scent name from tag data
- Green status dot indicates configured state

### Usage Statistics
- Total runtime (persistent across power cycles)
- Cartridge runtime (reset when cartridge changes)
- Session runtime (current power-on session)

## Platform Notes

- Uses PLATFORM_ESP8266 conditional compilation
- analogWrite() for PWM (not ledcWrite)
- EEPROM for storage (not Preferences/NVS)
- FastLED for WS2812B LED control
- GPIO3 (RX) used for rear button
- NTP time sync for night mode
