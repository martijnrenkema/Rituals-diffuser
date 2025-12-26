# Rituals Perfume Genie 2.0 - Custom Firmware

Custom firmware for the Rituals Perfume Genie 2.0 diffuser. Replaces the cloud-dependent Rituals firmware with fully local control via Home Assistant.

![Version](https://img.shields.io/badge/Version-1.1.0-brightgreen)
![ESP-WROOM-02](https://img.shields.io/badge/ESP-WROOM--02-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP8266-orange)
![Home Assistant](https://img.shields.io/badge/Home%20Assistant-MQTT-41BDF5)
![License](https://img.shields.io/badge/License-MIT-green)

## Features

- **Local Control** - No cloud dependency, works offline
- **Home Assistant Integration** - MQTT auto-discovery
- **Timer Presets** - 30, 60, 90, 120 minutes + continuous
- **Interval Mode** - Pulsing mode to save fragrance
- **Night Mode** - Auto-dim LED during configured hours
- **RFID Cartridge Detection** - Reads scent name from Rituals cartridge
- **Usage Statistics** - Track total runtime and cartridge usage
- **OTA Updates** - Wireless firmware updates after initial flash
- **Web Interface** - Configure WiFi, MQTT, passwords, and control the diffuser
- **RGB LED Status** - Visual feedback for device state
- **Physical Buttons** - Front and rear button support

## Hardware

This firmware is designed for the **Rituals Perfume Genie 2.0** which contains an **ESP-WROOM-02** (ESP8266) module.

### GPIO Pinout

| GPIO | Function | Description |
|------|----------|-------------|
| GPIO4 | Fan Control | On/Off control |
| GPIO5 | Fan Speed | PWM speed control |
| GPIO15 | LED | WS2812 RGB LED |
| GPIO16 | Front Button | "Connect" button |
| GPIO3 | Rear Button | Back button (RX pin) |

## ⚠️ Backup Original Firmware First!

**Before flashing custom firmware, backup the original Rituals firmware so you can restore it if needed.**

```bash
# Connect USB-to-serial adapter and identify port
ls /dev/cu.usbserial-*

# Backup original firmware (2MB flash)
esptool.py --port /dev/cu.usbserial-XXXX read_flash 0x00000 0x200000 rituals_original_firmware.bin
```

To restore original firmware:
```bash
esptool.py --port /dev/cu.usbserial-XXXX erase_flash
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x00000 rituals_original_firmware.bin
```

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-Serial adapter (FTDI, CP2102, or NodeMCU as programmer)
- Dupont wires

### First Flash (Serial)

1. **Connect the programmer:**
   ```
   USB Adapter    →  Genie Board (pin header)
   TX             →  RX
   RX             →  TX
   GND            →  GND
   3.3V           →  VCC
   ```

2. **Enter flash mode:**
   - Hold GPIO0 low during power-on, OR
   - Hold the "Connect" button while powering on

3. **Flash the firmware:**
   ```bash
   # Clone the repository
   git clone https://github.com/martijnrenkema/Rituals-diffuser.git
   cd Rituals-diffuser

   # Flash firmware
   pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

   # Flash web files (SPIFFS)
   pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX
   ```

### OTA Updates (After First Flash)

Once the firmware is installed and connected to WiFi:

```bash
pio run -e esp8266_ota -t upload
```

| Parameter | Value |
|-----------|-------|
| Hostname | `rituals-diffuser.local` |
| Port | 3232 |
| Password | `diffuser-ota` |

## WiFi Setup

1. **Connect to AP:** `Rituals-Diffuser-XXXX` (password: `diffuser123`)
2. **Open browser:** `http://192.168.4.1`
3. **Configure WiFi** credentials
4. Device restarts and connects to your network

## Home Assistant

### MQTT Auto-Discovery

The device automatically appears in Home Assistant when MQTT auto-discovery is enabled.

### Entities

| Entity | Type | Description |
|--------|------|-------------|
| Diffuser | Fan | On/off, speed 0-100%, presets |
| Interval Mode | Switch | Pulsing mode toggle |
| Interval On | Number | On-time (10-120 sec) |
| Interval Off | Number | Off-time (10-120 sec) |
| Time Left | Sensor | Remaining timer minutes |
| Fan RPM | Sensor | Current fan speed |
| WiFi Signal | Sensor | Signal strength (dBm) |
| Cartridge | Sensor | Current scent name (via RFID) |
| Total Runtime | Sensor | Total device runtime (hours) |
| Cartridge Runtime | Sensor | Time since cartridge change |

### Timer Presets

- 30 minutes
- 60 minutes
- 90 minutes
- 120 minutes
- Continuous

## Button Controls

### Front Button (Connect)
| Action | Function |
|--------|----------|
| Short press | Toggle fan on/off |
| Long press (3s) | Factory reset |

### Rear Button
| Action | Function |
|--------|----------|
| Short press | Cycle speed: 25% → 50% → 75% → 100% |
| Long press (3s) | Toggle interval mode |

## LED Status

| Color | Status |
|-------|--------|
| Blue (solid) | WiFi connected, fan off |
| Green (solid) | Fan running |
| Cyan (blinking) | Connecting to WiFi |
| Orange (blinking) | AP mode active |
| Purple (fast) | OTA update in progress |
| Red (blinking) | Error / disconnected |

## Configuration

### Default Passwords

| Function | Default | Configurable |
|----------|---------|--------------|
| WiFi AP | `diffuser123` | Yes, via web interface |
| OTA Updates | `diffuser-ota` | Yes, via web interface |

Passwords can be changed in the web interface under "Security". Minimum 8 characters required. Device restart needed after changing.

### MQTT Topics

```
rituals_diffuser/fan/state        → ON/OFF
rituals_diffuser/fan/speed        → 0-100
rituals_diffuser/fan/preset       → Timer preset
rituals_diffuser/availability     → online/offline
```

## Project Structure

```
├── src/
│   ├── main.cpp              # Entry point
│   ├── config.h              # Configuration
│   ├── fan_controller.*      # Fan control, timer, interval
│   ├── led_controller.*      # WS2812 RGB LED
│   ├── button_handler.*      # Button handling
│   ├── rfid_handler.*        # RFID cartridge detection
│   ├── storage.*             # EEPROM storage
│   ├── wifi_manager.*        # WiFi management
│   ├── web_server.*          # Web interface
│   ├── mqtt_handler.*        # MQTT + HA discovery
│   └── ota_handler.*         # OTA updates
├── data/                     # Web files (SPIFFS)
├── platformio.ini            # PlatformIO config
└── README.md
```

## Dependencies

- [PubSubClient](https://github.com/knolleary/pubsubclient) - MQTT
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - JSON
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Web server
- [FastLED](https://github.com/FastLED/FastLED) - LED control
- [MFRC522](https://github.com/miguelbalboa/rfid) - RFID reader

## Troubleshooting

### OTA upload fails
- Check WiFi: `ping rituals-diffuser.local`
- Try using IP address instead of hostname
- Ensure port 3232 is not blocked
- Fallback: flash via serial

### Device not found in Home Assistant
- Check MQTT broker connection
- Remove old device from HA
- Power cycle the ESP
- Wait 30 seconds for discovery

### WiFi won't connect
- Factory reset: hold front button for 3 seconds
- Connect to AP mode and reconfigure

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Credits

- Based on research from the [Home Assistant Community](https://community.home-assistant.io/t/replace-the-software-of-the-rituals-genie-esp-with-esphome/762356)
- Inspired by [Echnics/Perfume-Genie-ESPhome](https://github.com/Echnics/Perfume-Genie-ESPhome)

## License

MIT License - feel free to use and modify.

## Disclaimer

This project is not affiliated with Rituals Cosmetics. Use at your own risk. Modifying your device may void warranty.
