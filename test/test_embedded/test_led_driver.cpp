/**
 * test_led_driver.cpp - On-device integration tests for LED driver
 * 
 * Tests the WS2812 driver and LedController on actual ESP8266 hardware.
 * Verifies GPIO states, timing, color storage, and mode transitions.
 * 
 * Run with: pio test -e esp8266_test
 */
#include <Arduino.h>
#include <unity.h>
#include "config.h"
#include "ws2812_minimal.h"
#include "led_controller.h"

// =============================================================================
// Helper: Read GPIO output register bit for LED_DATA_PIN
// =============================================================================
static bool readGpioOutput(uint8_t pin) {
    uint32_t gpio_out = *(volatile uint32_t*)0x60000300; // GPIO_OUT register
    return (gpio_out >> pin) & 1;
}

// =============================================================================
// TEST SUITE 1: WS2812Minimal driver basics
// =============================================================================

void test_ws2812_begin_sets_pin_low() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    // After begin(), GPIO should be LOW (WS2812 idle state)
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ws2812_setcolor_rgb_stores_values() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    led.setColor(0xFF, 0x80, 0x40);
    TEST_ASSERT_EQUAL_UINT8(0xFF, led.getRed());
    TEST_ASSERT_EQUAL_UINT8(0x80, led.getGreen());
    TEST_ASSERT_EQUAL_UINT8(0x40, led.getBlue());
}

void test_ws2812_setcolor_uint32_stores_values() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    led.setColor((uint32_t)0xFF8040);
    TEST_ASSERT_EQUAL_UINT8(0xFF, led.getRed());
    TEST_ASSERT_EQUAL_UINT8(0x80, led.getGreen());
    TEST_ASSERT_EQUAL_UINT8(0x40, led.getBlue());
}

void test_ws2812_setcolor_black() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    led.setColor(0, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, led.getRed());
    TEST_ASSERT_EQUAL_UINT8(0, led.getGreen());
    TEST_ASSERT_EQUAL_UINT8(0, led.getBlue());
}

void test_ws2812_setcolor_white() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    led.setColor(255, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(255, led.getRed());
    TEST_ASSERT_EQUAL_UINT8(255, led.getGreen());
    TEST_ASSERT_EQUAL_UINT8(255, led.getBlue());
}

void test_ws2812_pin_config() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    TEST_ASSERT_EQUAL_UINT8(LED_DATA_PIN, led.getPin());
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(1 << LED_DATA_PIN), led.getPinMask());
}

// =============================================================================
// TEST SUITE 2: WS2812 show() execution and GPIO state
// =============================================================================

void test_ws2812_show_black_gpio_idle_low() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(0, 0, 0);
    led.show();
    
    // After show() completes (including 60µs reset), pin should be LOW
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ws2812_show_red_gpio_idle_low() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(255, 0, 0);
    led.show();
    
    // Pin should idle LOW after data transmission
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ws2812_show_white_gpio_idle_low() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(255, 255, 255);
    led.show();
    
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ws2812_show_no_crash() {
    // Verify show() with various colors doesn't crash
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    
    uint32_t colors[] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 
                         0xFF8000, 0x00FFFF, 0xFF00FF, 0xFFFFFF};
    for (size_t i = 0; i < sizeof(colors)/sizeof(colors[0]); i++) {
        led.setColor(colors[i]);
        led.show();
        delay(1); // Small delay between updates
    }
    TEST_ASSERT_TRUE(true); // Reached here = no crash
}

// =============================================================================
// TEST SUITE 3: WS2812 show() timing verification
// =============================================================================

void test_ws2812_show_timing() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(128, 128, 128);
    
    unsigned long start = micros();
    led.show();
    unsigned long elapsed = micros() - start;
    
    // Expected: 24 bits * 1.25µs = 30µs data + 60µs reset = ~90µs
    // Allow generous tolerance: 70-300µs
    Serial.printf("[TEST] show() took %lu µs\n", elapsed);
    TEST_ASSERT_GREATER_OR_EQUAL(70, elapsed);   // at least 70µs
    TEST_ASSERT_LESS_OR_EQUAL(300, elapsed);      // no more than 300µs
}

void test_ws2812_show_timing_black() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(0, 0, 0);
    
    unsigned long start = micros();
    led.show();
    unsigned long elapsed = micros() - start;
    
    Serial.printf("[TEST] show(black) took %lu µs\n", elapsed);
    TEST_ASSERT_GREATER_OR_EQUAL(70, elapsed);
    TEST_ASSERT_LESS_OR_EQUAL(300, elapsed);
}

void test_ws2812_show_timing_white() {
    WS2812Minimal led;
    led.begin(LED_DATA_PIN);
    led.setColor(255, 255, 255);
    
    unsigned long start = micros();
    led.show();
    unsigned long elapsed = micros() - start;
    
    Serial.printf("[TEST] show(white) took %lu µs\n", elapsed);
    TEST_ASSERT_GREATER_OR_EQUAL(70, elapsed);
    TEST_ASSERT_LESS_OR_EQUAL(300, elapsed);
}

// =============================================================================
// TEST SUITE 4: LedController integration tests
// =============================================================================

void test_ledcontroller_begin() {
    ledController.begin();
    
    // After begin(), LED should be off (black) and pin should be LOW
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ledcontroller_default_brightness() {
    ledController.begin();
    
    // Default brightness should be 50%
    TEST_ASSERT_EQUAL(50, ledController.getBrightness());
}

void test_ledcontroller_mode_off() {
    ledController.begin();
    ledController.off();
    ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::OFF, (int)ledController.getMode());
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ledcontroller_mode_on() {
    ledController.begin();
    ledController.setColor((uint32_t)LED_COLOR_GREEN);
    ledController.on();
    ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::ON, (int)ledController.getMode());
    // After show(), pin should be idle LOW
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_ledcontroller_show_connected() {
    ledController.begin();
    ledController.showConnected();
    ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::ON, (int)ledController.getMode());
}

void test_ledcontroller_show_connecting() {
    ledController.begin();
    ledController.showConnecting();
    
    // Run multiple loop iterations to see blinking
    for (int i = 0; i < 20; i++) {
        ledController.loop();
        delay(10);
    }
    
    TEST_ASSERT_EQUAL((int)LedMode::BLINK_FAST, (int)ledController.getMode());
}

void test_ledcontroller_show_ap_mode() {
    ledController.begin();
    ledController.showAPMode();
    
    for (int i = 0; i < 10; i++) {
        ledController.loop();
        delay(10);
    }
    
    TEST_ASSERT_EQUAL((int)LedMode::PULSE, (int)ledController.getMode());
}

void test_ledcontroller_show_ota() {
    ledController.begin();
    ledController.showOTA();
    
    for (int i = 0; i < 10; i++) {
        ledController.loop();
        delay(10);
    }
    
    TEST_ASSERT_EQUAL((int)LedMode::OTA, (int)ledController.getMode());
}

void test_ledcontroller_show_error() {
    ledController.begin();
    ledController.showError();
    
    for (int i = 0; i < 10; i++) {
        ledController.loop();
        delay(10);
    }
    
    TEST_ASSERT_EQUAL((int)LedMode::BLINK_FAST, (int)ledController.getMode());
}

void test_ledcontroller_show_fan_running() {
    ledController.begin();
    ledController.showFanRunning();
    ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::ON, (int)ledController.getMode());
}

void test_ledcontroller_show_interval_mode() {
    ledController.begin();
    ledController.showIntervalMode();
    ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::ON, (int)ledController.getMode());
}

// =============================================================================
// TEST SUITE 5: Brightness control integration
// =============================================================================

void test_ledcontroller_set_brightness_100() {
    ledController.begin();
    ledController.setBrightness(100);
    TEST_ASSERT_EQUAL(100, ledController.getBrightness());
}

void test_ledcontroller_set_brightness_0() {
    ledController.begin();
    ledController.setBrightness(0);
    TEST_ASSERT_EQUAL(0, ledController.getBrightness());
}

void test_ledcontroller_set_brightness_50() {
    ledController.begin();
    ledController.setBrightness(50);
    // Due to integer mapping: 50→127→49
    TEST_ASSERT_INT_WITHIN(1, 50, ledController.getBrightness());
}

void test_ledcontroller_brightness_auto_restore() {
    ledController.begin();
    ledController.setBrightness(0);
    // When turning on with brightness 0, it should auto-restore to 128
    ledController.on();
    // brightness should have been restored
    TEST_ASSERT_TRUE(ledController.getBrightness() > 0);
}

// =============================================================================
// TEST SUITE 6: Mode transition stress test
// =============================================================================

void test_mode_rapid_transitions() {
    ledController.begin();
    
    // Rapidly switch between all modes
    ledController.showConnected(); ledController.loop();
    ledController.showConnecting(); ledController.loop();
    ledController.showAPMode(); ledController.loop();
    ledController.showOTA(); ledController.loop();
    ledController.showError(); ledController.loop();
    ledController.showFanRunning(); ledController.loop();
    ledController.showIntervalMode(); ledController.loop();
    ledController.off(); ledController.loop();
    ledController.on(); ledController.loop();
    ledController.off(); ledController.loop();
    
    TEST_ASSERT_EQUAL((int)LedMode::OFF, (int)ledController.getMode());
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_mode_off_after_all_modes() {
    ledController.begin();
    
    // Cycle through every mode, end with OFF
    LedMode modes[] = {LedMode::ON, LedMode::BLINK_FAST, LedMode::BLINK_SLOW,
                       LedMode::PULSE, LedMode::BREATHE_SLOW, LedMode::OTA, LedMode::OFF};
    
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        ledController.setMode(modes[i]);
        for (int j = 0; j < 5; j++) {
            ledController.loop();
            delay(5);
        }
    }
    
    TEST_ASSERT_EQUAL((int)LedMode::OFF, (int)ledController.getMode());
}

// =============================================================================
// TEST SUITE 7: Pulse/Breathe animation over time
// =============================================================================

void test_pulse_animation_runs() {
    ledController.begin();
    ledController.setColor((uint32_t)LED_COLOR_ORANGE);
    ledController.setMode(LedMode::PULSE);
    
    // Run 100 loop iterations with 20ms delays (2 seconds of animation)
    for (int i = 0; i < 100; i++) {
        ledController.loop();
        delay(20);
    }
    
    // Should still be in PULSE mode
    TEST_ASSERT_EQUAL((int)LedMode::PULSE, (int)ledController.getMode());
    // Pin should be idle LOW between show() calls
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

void test_breathe_animation_runs() {
    ledController.begin();
    ledController.setColor((uint32_t)LED_COLOR_BLUE);
    ledController.setMode(LedMode::BREATHE_SLOW);
    
    // Run 100 loop iterations with 30ms delays (3 seconds of animation)
    for (int i = 0; i < 100; i++) {
        ledController.loop();
        delay(30);
    }
    
    // Should still be in BREATHE_SLOW mode
    TEST_ASSERT_EQUAL((int)LedMode::BREATHE_SLOW, (int)ledController.getMode());
    TEST_ASSERT_FALSE(readGpioOutput(LED_DATA_PIN));
}

// =============================================================================
// SETUP & LOOP (PlatformIO test runner for embedded targets)
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial monitor
    
    Serial.println("\n========================================");
    Serial.println("LED Driver Integration Tests - ESP8266");
    Serial.println("========================================\n");
    
    UNITY_BEGIN();
    
    // Suite 1: WS2812 driver basics
    RUN_TEST(test_ws2812_begin_sets_pin_low);
    RUN_TEST(test_ws2812_setcolor_rgb_stores_values);
    RUN_TEST(test_ws2812_setcolor_uint32_stores_values);
    RUN_TEST(test_ws2812_setcolor_black);
    RUN_TEST(test_ws2812_setcolor_white);
    RUN_TEST(test_ws2812_pin_config);
    
    // Suite 2: show() execution and GPIO state
    RUN_TEST(test_ws2812_show_black_gpio_idle_low);
    RUN_TEST(test_ws2812_show_red_gpio_idle_low);
    RUN_TEST(test_ws2812_show_white_gpio_idle_low);
    RUN_TEST(test_ws2812_show_no_crash);
    
    // Suite 3: Timing verification
    RUN_TEST(test_ws2812_show_timing);
    RUN_TEST(test_ws2812_show_timing_black);
    RUN_TEST(test_ws2812_show_timing_white);
    
    // Suite 4: LedController integration
    RUN_TEST(test_ledcontroller_begin);
    RUN_TEST(test_ledcontroller_default_brightness);
    RUN_TEST(test_ledcontroller_mode_off);
    RUN_TEST(test_ledcontroller_mode_on);
    RUN_TEST(test_ledcontroller_show_connected);
    RUN_TEST(test_ledcontroller_show_connecting);
    RUN_TEST(test_ledcontroller_show_ap_mode);
    RUN_TEST(test_ledcontroller_show_ota);
    RUN_TEST(test_ledcontroller_show_error);
    RUN_TEST(test_ledcontroller_show_fan_running);
    RUN_TEST(test_ledcontroller_show_interval_mode);
    
    // Suite 5: Brightness control
    RUN_TEST(test_ledcontroller_set_brightness_100);
    RUN_TEST(test_ledcontroller_set_brightness_0);
    RUN_TEST(test_ledcontroller_set_brightness_50);
    RUN_TEST(test_ledcontroller_brightness_auto_restore);
    
    // Suite 6: Mode transitions stress test
    RUN_TEST(test_mode_rapid_transitions);
    RUN_TEST(test_mode_off_after_all_modes);
    
    // Suite 7: Animation tests
    RUN_TEST(test_pulse_animation_runs);
    RUN_TEST(test_breathe_animation_runs);
    
    UNITY_END();
}

void loop() {
    // Nothing - tests run once in setup()
}
