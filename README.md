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
- **Usage Statistics** - Track total runtime
- **OTA Updates** - Wireless firmware updates (via web interface or ArduinoOTA)
- **Web Interface** - Configure WiFi, MQTT, passwords, and control the diffuser
- **RGB LED Status** - Visual feedback for device state
- **Physical Buttons** - Front (SW2) and rear (SW1) button support

## Hardware

This firmware supports both the original **ESP-WROOM-02** (ESP8266) and **ESP32** as a drop-in replacement.

### ESP32 GPIO Pinout (Recommended)

| ESP32 GPIO | Rituals Pin | Function |
|------------|-------------|----------|
| GPIO25 | IO4 | Fan PWM speed control |
| GPIO26 | IO5/TP17 | Fan tachometer (RPM) |
| GPIO27 | IO15 | WS2812 RGB LED |
| GPIO13 | IO16 | Front button (SW2 - Connect) |
| GPIO14 | IO14 | Rear button (SW1 - Cold reset) |

### ESP8266 GPIO Pinout (Original)

| GPIO | Function | Description |
|------|----------|-------------|
| GPIO4 | Fan PWM | Speed control |
| GPIO5 | Fan Tacho | RPM feedback |
| GPIO15 | LED | WS2812 RGB LED |
| GPIO14 | SW2 | Connect button |
| GPIO13 | SW1 | Cold reset button |

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
| Long press (3s) | Start AP mode (WiFi config) |

### Rear Button (SW1 - Cold Reset)
| Action | Function |
|--------|----------|
| Short press | Restart ESP |
| Long press (3s) | Factory reset (clear all settings) |

## LED Status

| Color | Pattern | Status |
|-------|---------|--------|
| Red | Solid | Startup / Error |
| Cyan | Fast blink | Connecting to WiFi |
| Green | Solid | WiFi connected (fan off or on) |
| Purple | Solid | Interval mode active |
| Orange | Pulsing | AP mode active |
| Purple | Fast blink | OTA update in progress |

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
│   ├── config.h              # Configuration & pin definitions
│   ├── fan_controller.*      # Fan control, timer, interval, RPM
│   ├── led_controller.*      # WS2812 RGB LED
│   ├── button_handler.*      # Button handling
│   ├── storage.*             # NVS/EEPROM storage
│   ├── wifi_manager.*        # WiFi management
│   ├── web_server.*          # Web interface + OTA upload
│   ├── mqtt_handler.*        # MQTT + HA discovery
│   └── ota_handler.*         # ArduinoOTA updates
├── data/                     # Web files (SPIFFS)
│   ├── index.html            # Main control page
│   ├── update.html           # OTA firmware upload
│   ├── style.css             # Styling
│   └── script.js             # UI logic
├── platformio.ini            # PlatformIO config
└── README.md
```

## Dependencies

- [PubSubClient](https://github.com/knolleary/pubsubclient) - MQTT
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - JSON
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Web server
- [FastLED](https://github.com/FastLED/FastLED) - LED control

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
