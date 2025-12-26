# Rituals Perfume Genie 2.0 - Custom Firmware

## Repository

**GitHub:** https://github.com/martijnrenkema/Rituals-diffuser

## Version

Current version: **v1.1.0**

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
1. Connect to the device (WiFi or AP mode)
2. Open http://<device-ip>/ in browser
3. Expand "Security" section
4. Enter new password (minimum 8 characters)
5. Click "Save Passwords"
6. **Restart device** to apply changes

## OTA (Over-The-Air) Updates

- **Hostname:** rituals-diffuser.local
- **Port:** 3232
- **Password:** Configurable (default: diffuser-ota)

```bash
# Flash via OTA (after initial serial flash)
pio run -e esp8266_ota -t upload
```

## WiFi Configuration

### AP Mode
- **SSID:** Rituals-Diffuser-XXXX (last 4 hex of MAC)
- **Password:** Configurable (default: diffuser123)
- **IP:** 192.168.4.1

## Building

```bash
# Build firmware
pio run -e esp8266

# Build and upload via serial (first time)
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Build and upload filesystem
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX

# OTA update (after initial flash)
pio run -e esp8266_ota -t upload
```

## MQTT / Home Assistant

Device auto-registers via MQTT discovery. Entities:
- Fan (on/off, speed, timer presets)
- Interval Mode switch
- Sensors: Time Left, RPM, WiFi, Cartridge, Runtime

## Features

- **Night Mode:** Auto-dim LED during configured hours
- **RFID Detection:** Scent name from cartridge tag
- **Usage Statistics:** Total/cartridge/session runtime
- **Interval Mode:** Pulsing to save fragrance

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| /api/status | GET | Device status |
| /api/fan | POST | Fan control |
| /api/wifi | POST | WiFi config |
| /api/mqtt | POST | MQTT config |
| /api/rfid | GET/POST | RFID status/actions |
| /api/night | GET/POST | Night mode settings |
| /api/passwords | GET/POST | Password management |
| /api/reset | POST | Factory reset |
