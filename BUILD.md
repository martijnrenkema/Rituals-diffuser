# Building Firmware

## Quick Download (Pre-built)

**Option 1: GitHub Actions Artifacts**
1. Go to [Actions](https://github.com/martijnrenkema/Rituals-diffuser/actions)
2. Click on the latest successful build
3. Download `firmware-esp8266` artifact
4. Extract the .bin file

**Option 2: GitHub Releases (for tagged versions)**
1. Go to [Releases](https://github.com/martijnrenkema/Rituals-diffuser/releases)
2. Download `rituals-diffuser-v1.3.0-esp8266.bin`

## Build Locally

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- Git

### Steps

```bash
# 1. Clone repository
git clone https://github.com/martijnrenkema/Rituals-diffuser.git
cd Rituals-diffuser

# 2. Checkout the desired branch/tag
git checkout claude/check-project-bugs-nhzB1  # or main, or v1.3.0

# 3. Build firmware
pio run -e esp8266

# 4. Find the .bin file
# Location: .pio/build/esp8266/firmware.bin
```

## Build Output

After successful build, you'll find:

```
.pio/build/esp8266/
├── firmware.bin       # Main firmware (flash to 0x00000)
├── firmware.elf       # Debug symbols
└── spiffs.bin         # Web files (optional)
```

## Flash the Firmware

### Method 1: Web OTA (if already running custom firmware)

```bash
# 1. Build the firmware
pio run -e esp8266

# 2. Upload via web interface
# Go to: http://rituals-diffuser.local/
# Upload: .pio/build/esp8266/firmware.bin
```

### Method 2: PlatformIO Serial

```bash
# First time (USB connection required)
pio run -e esp8266 -t upload --upload-port /dev/cu.usbserial-XXXX

# Flash SPIFFS (web files)
pio run -e esp8266 -t uploadfs --upload-port /dev/cu.usbserial-XXXX
```

### Method 3: PlatformIO OTA

```bash
# After initial setup (WiFi connected)
pio run -e esp8266_ota -t upload
```

### Method 4: esptool.py

```bash
# Install esptool
pip install esptool

# Flash firmware only
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x00000 firmware.bin

# Flash firmware + SPIFFS
esptool.py --port /dev/cu.usbserial-XXXX write_flash 0x00000 firmware.bin 0x300000 spiffs.bin
```

## Verification

After flashing:

1. Connect to serial port (115200 baud)
2. You should see:
   ```
   =================================
     Rituals Perfume Genie 2.0
     Custom Firmware v1.3.0
   =================================
   ```

3. Check WiFi AP: `Rituals-Diffuser-XXXX`
4. Connect and configure via `http://192.168.4.1`

## Troubleshooting

**Build fails with "Platform not found"**
```bash
pio platform install espressif8266
```

**Missing libraries**
```bash
pio lib install
```

**Out of memory during build**
- Close other applications
- Use `pio run -e esp8266 -j 1` (single thread)

**Upload fails**
- Check USB cable connection
- Verify correct serial port
- Press reset button during upload
- Try lower baud rate: `--upload-speed 115200`

## Advanced: Custom Build Flags

Edit `platformio.ini` to customize:

```ini
[env:esp8266]
build_flags =
    -DSERIAL_BAUD=115200
    -DWEBSERVER_PORT=80
    -DMQTT_PORT=1883
```

## Size Optimization

Current build size (v1.3.0):
- Firmware: ~450KB (of 1MB available)
- SPIFFS: ~100KB (of 1MB available)
- Free space: ~450KB

To reduce size:
- Remove unused features from `config.h`
- Disable debug logging
- Optimize compiler flags in `platformio.ini`
