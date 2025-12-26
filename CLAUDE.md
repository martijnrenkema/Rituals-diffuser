# Rituals Perfume Genie 2.0 - Development Documentation

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

## OTA (Over-The-Air) Updates

### Configuration
- **Hostname:** `rituals-diffuser.local`
- **Port:** 3232
- **Password:** `diffuser-ota`

### Commands
```bash
# Flash via OTA (after initial serial flash)
pio run -e esp8266_ota -t upload

# Initial serial flash (first time only)
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Upload web files (SPIFFS)
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX
```

### Troubleshooting OTA
1. Verify device is online: `ping rituals-diffuser.local`
2. Try IP address if mDNS fails
3. Check port 3232 is not blocked
4. Fallback: use serial flash

## WiFi Configuration

### AP Mode
- **SSID:** `Rituals-Diffuser-XXXX` (last 4 hex of MAC)
- **Password:** `diffuser123`
- **IP:** `192.168.4.1`

### Captive Portal
Device automatically redirects to configuration page when connected to AP.

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

### Timer Presets
- 30 minutes
- 60 minutes
- 90 minutes
- 120 minutes
- Continuous

### MQTT Topics
```
rituals_diffuser/fan/state        → ON/OFF
rituals_diffuser/fan/speed        → 0-100
rituals_diffuser/fan/preset       → Timer preset
rituals_diffuser/interval/state   → ON/OFF
rituals_diffuser/availability     → online/offline
```

## Building

### Requirements
- PlatformIO CLI or VS Code extension
- USB-to-Serial adapter for initial flash

### Build Commands
```bash
# Build firmware
pio run -e esp8266

# Build and upload via serial
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Build and upload via OTA
pio run -e esp8266_ota -t upload

# Upload SPIFFS filesystem
pio run -e esp8266 -t uploadfs
```

## Default Credentials

| Function | Value |
|----------|-------|
| WiFi AP Password | `diffuser123` |
| OTA Password | `diffuser-ota` |

## Platform-Specific Notes

### ESP8266 vs ESP32
This firmware uses conditional compilation for ESP8266:
- `#ifdef PLATFORM_ESP8266` for platform-specific code
- Uses `analogWrite()` instead of `ledcWrite()` for PWM
- Uses EEPROM instead of Preferences (NVS) for storage
- FastLED for WS2812B LED control

### Known Limitations
- GPIO3 (RX) used for rear button - serial debug may interfere
- Limited to ~600KB firmware (flash usage ~58%)
- Single WS2812B LED on GPIO15
