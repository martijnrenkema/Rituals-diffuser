# Rituals Diffuser ESP32 Project

## Hardware
- **Controller:** ESP32-WROOM-32 DevKit
- **Fan:** Sunon MF40100V2-1D04U-S99 (5V DC, 4-wire PWM)
- **Geen MOSFET nodig** - directe PWM control via 4-wire fan

### Pinout
| Fan draad | ESP32 Pin |
|-----------|-----------|
| Rood (+5V) | 5V (USB VIN) |
| Zwart (GND) | GND |
| Blauw (PWM) | GPIO 25 |
| Geel (Tacho) | GPIO 26 |
| LED | GPIO 2 |

## Firmware Uploaden

### Via USB (eerste keer)
```bash
pio run -t upload --upload-port /dev/cu.usbserial-0001
pio run -t uploadfs  # Voor webpagina's (SPIFFS)
```

### Via OTA (draadloos)
```bash
pio run -t upload
```
- **IP:** 192.168.1.234 (geconfigureerd in platformio.ini)
- **OTA wachtwoord:** `diffuser-ota`
- **Hostname:** `rituals-diffuser.local`

## WiFi Configuratie

### Eerste setup (AP Mode)
1. Verbind met WiFi: `Rituals-Diffuser-XXXX`
2. Wachtwoord: `diffuser123`
3. Open: `http://192.168.4.1`
4. Configureer je WiFi credentials

### Fallback gedrag
- 3 mislukte verbindingspogingen → AP mode als fallback
- In AP mode: elke 5 minuten retry naar opgeslagen WiFi
- Auto-reconnect bij WiFi verlies

## MQTT / Home Assistant

### Configuratie
Via webinterface of Home Assistant discovery.

### Auto-Discovery Entities
| Entity | Type | Beschrijving |
|--------|------|--------------|
| Diffuser | fan | Aan/uit, speed 0-100%, timer presets |
| Interval Mode | switch | Pulseer modus aan/uit |
| Interval On Time | number | Seconden aan (10-120) |
| Interval Off Time | number | Seconden uit (10-120) |
| Remaining Time | sensor | Resterende timer minuten |
| Fan RPM | sensor | Actuele toeren per minuut |
| WiFi Signal | sensor | dBm signaalsterkte |

### MQTT Topics
- `rituals_diffuser/fan/state` - ON/OFF
- `rituals_diffuser/fan/speed` - 0-100
- `rituals_diffuser/fan/preset` - Timer preset
- `rituals_diffuser/availability` - online/offline

## Webinterface
- **URL:** `http://192.168.1.234` (of `http://rituals-diffuser.local`)
- Features:
  - Fan aan/uit en snelheid
  - Timer: 30/60/90/120 min
  - Interval mode configuratie
  - WiFi en MQTT instellingen
  - Factory reset

## Belangrijke Wachtwoorden
| Functie | Wachtwoord | Locatie |
|---------|------------|---------|
| AP Mode WiFi | `diffuser123` | config.h |
| OTA Update | `diffuser-ota` | config.h |

## Geheugengebruik
- **Flash:** ~90% (1.18 MB van 1.31 MB)
- **RAM:** ~17% (57 KB van 320 KB)
- **SPIFFS:** ~13 KB (webpagina's)

## Projectstructuur
```
src/
├── main.cpp           # Entry point
├── config.h           # Pin definities & constanten
├── fan_controller.*   # PWM control, timer, interval mode
├── led_controller.*   # LED status indicator
├── storage.*          # NVS persistente opslag
├── wifi_manager.*     # WiFi + AP mode + fallback
├── web_server.*       # Async webserver + API
├── mqtt_handler.*     # MQTT + HA discovery
└── ota_handler.*      # ArduinoOTA
data/
├── index.html         # Webinterface
├── style.css          # Styling (Rituals-thema)
└── script.js          # Frontend logic
```

## Dependencies
- PubSubClient (MQTT)
- ArduinoJson
- ESPAsyncWebServer
- AsyncTCP

## Troubleshooting

### Fan entity verschijnt niet in Home Assistant
1. Verwijder device in HA: Instellingen → Apparaten → MQTT → Rituals Diffuser → Verwijderen
2. Herstart ESP32 (reset knop)
3. Wacht 30 sec voor discovery

### OTA upload faalt
- Check of ESP32 op WiFi zit: `ping rituals-diffuser.local`
- Controleer wachtwoord in platformio.ini
- USB uploaden als backup: `--upload-port /dev/cu.usbserial-0001`

### WiFi verbindt niet
- Factory reset via webinterface of serieel: `http://192.168.4.1`
- AP mode start automatisch na 3 mislukte pogingen

## Development Workflow

### PlatformIO Setup
PlatformIO is vereist voor het builden van de firmware:
```bash
# Installeer via virtual environment (aanbevolen)
python3 -m venv ~/.platformio-venv
~/.platformio-venv/bin/pip install platformio

# Of voeg alias toe aan .bashrc/.zshrc
alias pio="~/.platformio-venv/bin/pio"
```

### Code Wijzigingen Aanbrengen

**Stap 1: Maak wijzigingen aan de code**
- Pas de relevante source files aan in `src/`
- Test lokaal via serial monitor

**Stap 2: Build de firmware**
```bash
# Build voor ESP32
~/.platformio-venv/bin/pio run -e esp32dev

# Build voor ESP8266
~/.platformio-venv/bin/pio run -e esp8266

# Of beide platforms
~/.platformio-venv/bin/pio run
```

**Stap 3: Versie bumpen**

De versie staat centraal in `src/config.h`:
```c
#define FIRMWARE_VERSION "1.6.7"
```

Alle andere bestanden (main.cpp, mqtt_handler.cpp, web_server.cpp) gebruiken deze constante automatisch.

Gebruik semantic versioning:
- **MAJOR.MINOR.PATCH** (bijv. 1.6.7)
- PATCH: bugfixes
- MINOR: nieuwe features (backwards compatible)
- MAJOR: breaking changes

**Stap 4: Build release binaries**
```bash
# Build firmware voor beide platforms
~/.platformio-venv/bin/pio run -e esp8266 -e esp32dev

# Build SPIFFS voor beide platforms
~/.platformio-venv/bin/pio run -t buildfs -e esp8266 -e esp32dev

# Binaries staan in:
# .pio/build/esp8266/firmware.bin
# .pio/build/esp8266/spiffs.bin
# .pio/build/esp32dev/firmware.bin
# .pio/build/esp32dev/spiffs.bin
```

**Stap 5: Commit en push naar GitHub**
```bash
git add -A
git commit -m "v1.6.7: Korte beschrijving van changes"
git push origin main
```

**Stap 6: GitHub Release maken (VERPLICHT)**

> **BELANGRIJK:** De update checker in de firmware kijkt naar GitHub Releases via de API.
> Zonder release zien gebruikers de update NIET in de webinterface!

```bash
# Kopieer binaries met juiste namen
mkdir -p /tmp/release
cp .pio/build/esp8266/firmware.bin /tmp/release/firmware_esp8266.bin
cp .pio/build/esp8266/spiffs.bin /tmp/release/spiffs_esp8266.bin
cp .pio/build/esp32dev/firmware.bin /tmp/release/firmware_esp32.bin
cp .pio/build/esp32dev/spiffs.bin /tmp/release/spiffs_esp32.bin

# Maak release aan met gh CLI
gh release create v1.6.7 \
  --title "v1.6.7 - Korte titel" \
  --notes "Beschrijving van de wijzigingen" \
  /tmp/release/firmware_esp8266.bin \
  /tmp/release/spiffs_esp8266.bin \
  /tmp/release/firmware_esp32.bin \
  /tmp/release/spiffs_esp32.bin
```

### Belangrijke Bestanden voor Versioning
| Bestand | Beschrijving |
|---------|--------------|
| `src/config.h` | **Enige plek waar versie moet worden aangepast** (FIRMWARE_VERSION) |
| GitHub Release | **Verplicht** voor update checker - zonder release geen update notificatie |

### Build Environments
| Environment | Platform | Beschrijving |
|-------------|----------|--------------|
| `esp32dev` | ESP32 | Voor nieuwe ESP32 installaties |
| `esp8266` | ESP8266 | Voor originele Rituals chip |
