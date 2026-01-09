#ifndef CONFIG_H
#define CONFIG_H

// ===========================================
// Platform Detection
// ===========================================
#ifdef ESP8266
    #define PLATFORM_ESP8266
#else
    #define PLATFORM_ESP32
#endif

// ===========================================
// Pin Definitions - Rituals Perfume Genie 2.0
// ===========================================
#ifdef PLATFORM_ESP8266
    // Rituals Genie ESP-WROOM-02 pinout
    #define FAN_PWM_PIN         4       // GPIO4 - Fan PWM speed control (blue wire)
    #define FAN_TACHO_PIN       5       // GPIO5 - Fan tachometer/RPM (yellow wire, TP17)
    #define LED_DATA_PIN        15      // GPIO15 - WS2812 RGB LED
    #define BUTTON_FRONT_PIN    14      // GPIO14 - Connect button (SW2)
    #define BUTTON_REAR_PIN     13      // GPIO13 - Cold reset button (SW1)
    #define NUM_LEDS            1       // Single WS2812 LED
#else
    // ESP32 DevKit pinout voor Rituals Genie
    // Sluit de Genie board draden aan op deze ESP32 pinnen:
    #define FAN_PWM_PIN         25      // GPIO25 → Genie IO4 (fan PWM, blue wire)
    #define FAN_TACHO_PIN       26      // GPIO26 → Genie IO5/TP17 (tachometer, yellow wire)
    #define LED_DATA_PIN        27      // GPIO27 → Genie IO15 (WS2812 LED data)
    #define BUTTON_FRONT_PIN    13      // GPIO13 → Genie IO16 (Connect button SW2)
    #define BUTTON_REAR_PIN     14      // GPIO14 → Genie IO14 (Cold reset SW1)
    #define NUM_LEDS            1       // Single WS2812 LED
#endif

// ===========================================
// PWM Configuration
// ===========================================
#ifdef PLATFORM_ESP8266
    #define PWM_FREQUENCY       1000    // ESP8266 default PWM freq
    #define PWM_RANGE           255     // 8-bit PWM
#else
    #define PWM_FREQUENCY       25000   // 25kHz - optimal for 4-wire fans
    #define PWM_CHANNEL         0       // LEDC channel for fan PWM
    #define PWM_RESOLUTION      8       // 8-bit (0-255)
#endif

// ===========================================
// Fan Settings
// ===========================================
#define FAN_MIN_SPEED       0       // Minimum speed (0%)
#define FAN_MAX_SPEED       100     // Maximum speed (100%)
#define FAN_MIN_PWM         0       // Some fans need minimum ~20% to start
#define FAN_SOFT_START_MS   500     // Soft start duration

// ===========================================
// Button Configuration
// ===========================================
#define BUTTON_DEBOUNCE_MS      50      // Debounce time
#define BUTTON_LONG_PRESS_MS    3000    // Long press for WiFi reset

// ===========================================
// Timer Presets (minutes)
// ===========================================
#define TIMER_PRESET_1      30
#define TIMER_PRESET_2      60
#define TIMER_PRESET_3      90
#define TIMER_PRESET_4      120

// ===========================================
// Interval Mode Defaults
// ===========================================
#define INTERVAL_ON_DEFAULT     30  // seconds
#define INTERVAL_OFF_DEFAULT    30  // seconds
#define INTERVAL_MIN            10  // minimum seconds
#define INTERVAL_MAX            120 // maximum seconds

// ===========================================
// WiFi Settings
// ===========================================
#define WIFI_AP_SSID_PREFIX     "Rituals-Diffuser-"
#define WIFI_AP_PASSWORD        "diffuser123"
#define WIFI_CONNECT_TIMEOUT    30000           // 30 seconds
#define WIFI_RECONNECT_INTERVAL 60000           // 1 minute

// ===========================================
// MQTT Settings
// ===========================================
#define MQTT_TOPIC_PREFIX       "rituals_diffuser"
#define MQTT_DISCOVERY_PREFIX   "homeassistant"
#define MQTT_RECONNECT_INTERVAL 5000            // 5 seconds
#define MQTT_KEEPALIVE          60              // seconds

// ===========================================
// Webserver Settings
// ===========================================
#define WEBSERVER_PORT          80

// ===========================================
// OTA Settings
// ===========================================
#define OTA_HOSTNAME            "rituals-diffuser"
#define OTA_PASSWORD            "diffuser-ota"

// ===========================================
// NVS Storage Keys (EEPROM on ESP8266)
// ===========================================
#define NVS_NAMESPACE           "diffuser"
#define NVS_WIFI_SSID           "wifi_ssid"
#define NVS_WIFI_PASS           "wifi_pass"
#define NVS_MQTT_HOST           "mqtt_host"
#define NVS_MQTT_PORT           "mqtt_port"
#define NVS_MQTT_USER           "mqtt_user"
#define NVS_MQTT_PASS           "mqtt_pass"
#define NVS_DEVICE_NAME         "device_name"
#define NVS_FAN_SPEED           "fan_speed"
#define NVS_FAN_MIN_PWM         "fan_min_pwm"
#define NVS_INTERVAL_ON         "interval_on"
#define NVS_INTERVAL_OFF        "interval_off"
#define NVS_INTERVAL_ENABLED    "interval_en"
#define NVS_TOTAL_RUNTIME       "total_run"
#define NVS_OTA_PASSWORD        "ota_pass"
#define NVS_AP_PASSWORD         "ap_pass"
#define NVS_NIGHT_ENABLED       "night_en"
#define NVS_NIGHT_START         "night_start"
#define NVS_NIGHT_END           "night_end"
#define NVS_NIGHT_BRIGHT        "night_bri"

// ===========================================
// LED Colors (RGB)
// ===========================================
#define LED_COLOR_OFF           0x000000
#define LED_COLOR_RED           0xFF0000    // Disconnected / Error
#define LED_COLOR_GREEN         0x00FF00    // Fan running
#define LED_COLOR_BLUE          0x0000FF    // Connected, idle
#define LED_COLOR_PURPLE        0xFF00FF    // OTA update
#define LED_COLOR_ORANGE        0xFF8000    // AP mode
#define LED_COLOR_CYAN          0x00FFFF    // WiFi connecting
#define LED_COLOR_WHITE         0xFFFFFF    // Full brightness

// ===========================================
// LED Blink Patterns (ms)
// ===========================================
#define LED_BLINK_FAST          100     // WiFi connecting
#define LED_BLINK_SLOW          500     // AP mode
#define LED_PULSE_INTERVAL      2000    // Timer active

// ===========================================
// Misc
// ===========================================
#define SERIAL_BAUD             115200
#define TACHO_PULSES_PER_REV    2       // Most fans have 2 pulses per revolution

// ===========================================
// Firmware Version (centralized)
// ===========================================
#define FIRMWARE_VERSION        "1.6.4"

// ===========================================
// Update Checker Settings
// ===========================================
#define UPDATE_CHECK_INTERVAL   86400000UL  // 24 hours in milliseconds
#define UPDATE_GITHUB_REPO      "martijnrenkema/Rituals-diffuser"
#define UPDATE_CHECK_TIMEOUT    15000       // 15 seconds HTTP timeout

#endif // CONFIG_H
