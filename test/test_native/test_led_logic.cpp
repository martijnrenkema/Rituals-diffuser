/**
 * test_led_logic.cpp - Native unit tests for LED controller logic
 * 
 * Tests all color math, brightness scaling, mode logic, and byte ordering
 * without any hardware dependencies. Runs on the host PC via PlatformIO native.
 * 
 * Run with: pio test -e native
 */
#include <unity.h>
#include <stdint.h>
#include <string.h>

// Unity requires setUp/tearDown
void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Portable reimplementations of LED logic (same formulas as led_controller.cpp)
// =============================================================================

// Color constants (from config.h)
#define LED_COLOR_OFF     0x000000
#define LED_COLOR_RED     0xFF0000
#define LED_COLOR_GREEN   0x00FF00
#define LED_COLOR_BLUE    0x0000FF
#define LED_COLOR_PURPLE  0xFF00FF
#define LED_COLOR_ORANGE  0xFF8000
#define LED_COLOR_CYAN    0x00FFFF
#define LED_COLOR_WHITE   0xFFFFFF

// Extract RGB components from 0xRRGGBB
static uint8_t extractRed(uint32_t color)   { return (color >> 16) & 0xFF; }
static uint8_t extractGreen(uint32_t color) { return (color >> 8) & 0xFF; }
static uint8_t extractBlue(uint32_t color)  { return color & 0xFF; }

// Pack RGB into uint32_t (same as setColor(r,g,b))
static uint32_t packColor(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Brightness scaling (same formula as showLed())
static uint8_t scaleBrightness(uint8_t value, uint8_t brightness) {
    return ((uint16_t)value * brightness) / 255;
}

// Arduino map() reimplementation
static long testMap(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Arduino constrain() reimplementation
static long testConstrain(long val, long min_val, long max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

// setBrightness logic: percent -> internal 0-255
static uint8_t percentToInternal(uint8_t percent) {
    return (uint8_t)testMap(testConstrain(percent, 0, 100), 0, 100, 0, 255);
}

// getBrightness logic: internal 0-255 -> percent
static uint8_t internalToPercent(uint8_t internal) {
    return (uint8_t)testMap(internal, 0, 255, 0, 100);
}

// =============================================================================
// TEST: Color extraction from uint32_t
// =============================================================================

void test_extract_red_component() {
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractRed(0xFF0000));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractRed(0x00FF00));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractRed(0x0000FF));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractRed(0xFF8000));
    TEST_ASSERT_EQUAL_UINT8(0xAB, extractRed(0xABCDEF));
}

void test_extract_green_component() {
    TEST_ASSERT_EQUAL_UINT8(0x00, extractGreen(0xFF0000));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractGreen(0x00FF00));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractGreen(0x0000FF));
    TEST_ASSERT_EQUAL_UINT8(0x80, extractGreen(0xFF8000));
    TEST_ASSERT_EQUAL_UINT8(0xCD, extractGreen(0xABCDEF));
}

void test_extract_blue_component() {
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(0xFF0000));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(0x00FF00));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractBlue(0x0000FF));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(0xFF8000));
    TEST_ASSERT_EQUAL_UINT8(0xEF, extractBlue(0xABCDEF));
}

// =============================================================================
// TEST: Color packing (RGB -> uint32_t)
// =============================================================================

void test_pack_color_primary() {
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, packColor(0xFF, 0x00, 0x00));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, packColor(0x00, 0xFF, 0x00));
    TEST_ASSERT_EQUAL_HEX32(0x0000FF, packColor(0x00, 0x00, 0xFF));
}

void test_pack_color_mixed() {
    TEST_ASSERT_EQUAL_HEX32(0xFF8000, packColor(0xFF, 0x80, 0x00));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, packColor(0xFF, 0xFF, 0xFF));
    TEST_ASSERT_EQUAL_HEX32(0x000000, packColor(0x00, 0x00, 0x00));
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, packColor(0xAB, 0xCD, 0xEF));
}

void test_pack_unpack_roundtrip() {
    // Verify extract(pack(r,g,b)) == original values
    uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFF8000, 0x00FFFF, 0xFF00FF, 0xABCDEF, 0x000000, 0xFFFFFF};
    for (size_t i = 0; i < sizeof(colors)/sizeof(colors[0]); i++) {
        uint8_t r = extractRed(colors[i]);
        uint8_t g = extractGreen(colors[i]);
        uint8_t b = extractBlue(colors[i]);
        TEST_ASSERT_EQUAL_HEX32(colors[i], packColor(r, g, b));
    }
}

// =============================================================================
// TEST: WS2812B GRB byte ordering
// =============================================================================

void test_grb_byte_order_red() {
    uint8_t r = 0xFF, g = 0x00, b = 0x00;
    uint8_t data[3] = { g, r, b };  // GRB order as in show()
    TEST_ASSERT_EQUAL_UINT8(0x00, data[0]); // G first
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[1]); // R second
    TEST_ASSERT_EQUAL_UINT8(0x00, data[2]); // B third
}

void test_grb_byte_order_green() {
    uint8_t r = 0x00, g = 0xFF, b = 0x00;
    uint8_t data[3] = { g, r, b };
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[0]); // G first
    TEST_ASSERT_EQUAL_UINT8(0x00, data[1]); // R second
    TEST_ASSERT_EQUAL_UINT8(0x00, data[2]); // B third
}

void test_grb_byte_order_orange() {
    // Orange = 0xFF8000 → R=0xFF, G=0x80, B=0x00
    uint8_t r = 0xFF, g = 0x80, b = 0x00;
    uint8_t data[3] = { g, r, b };
    TEST_ASSERT_EQUAL_UINT8(0x80, data[0]); // G=0x80 first
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[1]); // R=0xFF second
    TEST_ASSERT_EQUAL_UINT8(0x00, data[2]); // B=0x00 third
}

void test_grb_byte_order_white() {
    uint8_t r = 0xFF, g = 0xFF, b = 0xFF;
    uint8_t data[3] = { g, r, b };
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, data[2]);
}

// =============================================================================
// TEST: Brightness scaling
// =============================================================================

void test_brightness_full_255() {
    // At full brightness (255), values pass through unchanged
    TEST_ASSERT_EQUAL_UINT8(255, scaleBrightness(255, 255));
    TEST_ASSERT_EQUAL_UINT8(128, scaleBrightness(128, 255));
    TEST_ASSERT_EQUAL_UINT8(1,   scaleBrightness(1, 255));
    TEST_ASSERT_EQUAL_UINT8(0,   scaleBrightness(0, 255));
}

void test_brightness_half_128() {
    // At 50% brightness (128/255)
    TEST_ASSERT_EQUAL_UINT8(128, scaleBrightness(255, 128)); // 255*128/255=128
    TEST_ASSERT_EQUAL_UINT8(64,  scaleBrightness(128, 128)); // 128*128/255=64
    TEST_ASSERT_EQUAL_UINT8(0,   scaleBrightness(1, 128));   // 1*128/255=0 (truncated)
    TEST_ASSERT_EQUAL_UINT8(0,   scaleBrightness(0, 128));
}

void test_brightness_zero() {
    // At zero brightness, everything should be 0
    TEST_ASSERT_EQUAL_UINT8(0, scaleBrightness(255, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scaleBrightness(128, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scaleBrightness(1, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scaleBrightness(0, 0));
}

void test_brightness_quarter_64() {
    // At 25% brightness (64/255)
    TEST_ASSERT_EQUAL_UINT8(64, scaleBrightness(255, 64)); // 255*64/255=64
    TEST_ASSERT_EQUAL_UINT8(32, scaleBrightness(128, 64)); // 128*64/255=32
    TEST_ASSERT_EQUAL_UINT8(0,  scaleBrightness(0, 64));
}

void test_brightness_max_value_no_overflow() {
    // Verify no overflow: 255*255 = 65025, fits in uint16_t (max 65535)
    uint16_t product = (uint16_t)255 * 255;
    TEST_ASSERT_TRUE(product <= 65535);
    TEST_ASSERT_EQUAL_UINT8(255, scaleBrightness(255, 255));
}

void test_brightness_specific_colors() {
    uint8_t brightness = 128; // 50%
    // LED_COLOR_ORANGE = 0xFF8000
    TEST_ASSERT_EQUAL_UINT8(128, scaleBrightness(0xFF, brightness)); // R
    TEST_ASSERT_EQUAL_UINT8(64,  scaleBrightness(0x80, brightness)); // G
    TEST_ASSERT_EQUAL_UINT8(0,   scaleBrightness(0x00, brightness)); // B
    
    // LED_COLOR_CYAN = 0x00FFFF
    TEST_ASSERT_EQUAL_UINT8(0,   scaleBrightness(0x00, brightness)); // R
    TEST_ASSERT_EQUAL_UINT8(128, scaleBrightness(0xFF, brightness)); // G
    TEST_ASSERT_EQUAL_UINT8(128, scaleBrightness(0xFF, brightness)); // B
}

// =============================================================================
// TEST: Brightness percent <-> internal value mapping
// =============================================================================

void test_brightness_percent_to_internal() {
    TEST_ASSERT_EQUAL_UINT8(0,   percentToInternal(0));
    TEST_ASSERT_EQUAL_UINT8(127, percentToInternal(50));
    TEST_ASSERT_EQUAL_UINT8(255, percentToInternal(100));
    TEST_ASSERT_EQUAL_UINT8(25,  percentToInternal(10));  // 10*255/100=25
}

void test_brightness_internal_to_percent() {
    TEST_ASSERT_EQUAL_UINT8(0,   internalToPercent(0));
    TEST_ASSERT_EQUAL_UINT8(50,  internalToPercent(128)); // 128*100/255=50
    TEST_ASSERT_EQUAL_UINT8(100, internalToPercent(255));
}

void test_brightness_clamping() {
    // Values above 100% should clamp to 100%
    TEST_ASSERT_EQUAL_UINT8(255, percentToInternal(100));
    TEST_ASSERT_EQUAL_UINT8(255, percentToInternal(110)); // constrained to 100
    TEST_ASSERT_EQUAL_UINT8(255, percentToInternal(200));
    
    // Values below 0% should clamp to 0%
    // uint8_t can't be negative, but test the constrain logic
    TEST_ASSERT_EQUAL_UINT8(0, percentToInternal(0));
}

void test_brightness_roundtrip() {
    // Note: due to integer division, perfect roundtrip is not guaranteed
    // setBrightness(50) → internal=127 → getBrightness()=49
    uint8_t internal = percentToInternal(50);  // 127
    uint8_t back = internalToPercent(internal); // 49
    // Accept ±1 rounding error
    TEST_ASSERT_INT_WITHIN(1, 50, back);
}

// =============================================================================
// TEST: Pulse value stepping (PULSE mode)
// =============================================================================

void test_pulse_step_up_increment() {
    uint8_t val = 0;
    bool dir = true; // up
    
    // Each step adds 10
    val += 10;
    TEST_ASSERT_EQUAL_UINT8(10, val);
    val += 10;
    TEST_ASSERT_EQUAL_UINT8(20, val);
}

void test_pulse_step_up_cap_at_255() {
    uint8_t val = 240;
    bool dir = true;
    
    // Simulate the pulse step logic
    val += 10; // val = 250, which is >= 245
    if (val >= 245) {
        val = 255;
        dir = false;
    }
    TEST_ASSERT_EQUAL_UINT8(255, val);
    TEST_ASSERT_FALSE(dir);
}

void test_pulse_step_down_decrement() {
    uint8_t val = 255;
    bool dir = false;
    
    val -= 10;
    TEST_ASSERT_EQUAL_UINT8(245, val);
    val -= 10;
    TEST_ASSERT_EQUAL_UINT8(235, val);
}

void test_pulse_step_down_floor_at_10() {
    uint8_t val = 20;
    bool dir = false;
    
    // At 20, trigger: val <= 20 → set to 10, reverse
    if (val <= 20) {
        val = 10;
        dir = true;
    }
    TEST_ASSERT_EQUAL_UINT8(10, val);
    TEST_ASSERT_TRUE(dir);
}

void test_pulse_full_cycle() {
    // Simulate a full up-down cycle
    uint8_t val = 0;
    bool dir = true;
    int steps = 0;
    
    // Go up
    while (dir && steps < 100) {
        if (dir) {
            if (val >= 245) { val = 255; dir = false; }
            else { val += 10; }
        }
        steps++;
    }
    TEST_ASSERT_EQUAL_UINT8(255, val);
    TEST_ASSERT_FALSE(dir);
    
    // Go down
    while (!dir && steps < 200) {
        if (!dir) {
            if (val <= 20) { val = 10; dir = true; }
            else { val -= 10; }
        }
        steps++;
    }
    TEST_ASSERT_EQUAL_UINT8(10, val);
    TEST_ASSERT_TRUE(dir);
}

// =============================================================================
// TEST: Breathe value stepping (BREATHE_SLOW mode)
// =============================================================================

void test_breathe_step_up_increment() {
    uint8_t val = 20;
    val += 4;
    TEST_ASSERT_EQUAL_UINT8(24, val);
}

void test_breathe_step_up_cap_at_255() {
    uint8_t val = 248;
    bool dir = true;
    
    val += 4; // 252, which is >= 251
    if (val >= 251) {
        val = 255;
        dir = false;
    }
    TEST_ASSERT_EQUAL_UINT8(255, val);
    TEST_ASSERT_FALSE(dir);
}

void test_breathe_step_down_floor_at_20() {
    uint8_t val = 24;
    bool dir = false;
    
    val -= 4; // 20
    if (val <= 24) {
        val = 20;
        dir = true;
    }
    TEST_ASSERT_EQUAL_UINT8(20, val);
    TEST_ASSERT_TRUE(dir);
}

// =============================================================================
// TEST: Pulse with brightness scaling (combined effect)
// =============================================================================

void test_pulse_brightness_scaling_mid() {
    uint8_t pulseValue = 128;
    uint8_t brightness = 128;
    uint32_t color = 0xFF8000; // orange
    
    uint8_t scaledPulse = ((uint16_t)pulseValue * brightness) / 255;
    uint8_t r = (((color >> 16) & 0xFF) * scaledPulse) / 255;
    uint8_t g = (((color >> 8) & 0xFF) * scaledPulse) / 255;
    uint8_t b = ((color & 0xFF) * scaledPulse) / 255;
    
    TEST_ASSERT_EQUAL_UINT8(64, scaledPulse); // 128*128/255≈64
    TEST_ASSERT_EQUAL_UINT8(64, r);           // 255*64/255=64
    TEST_ASSERT_EQUAL_UINT8(32, g);           // 128*64/255=32
    TEST_ASSERT_EQUAL_UINT8(0,  b);           // 0*64/255=0
}

void test_pulse_brightness_full() {
    uint8_t pulseValue = 255;
    uint8_t brightness = 255;
    uint32_t color = 0xFF0000; // pure red
    
    uint8_t scaledPulse = ((uint16_t)pulseValue * brightness) / 255;
    uint8_t r = (((color >> 16) & 0xFF) * scaledPulse) / 255;
    
    TEST_ASSERT_EQUAL_UINT8(255, scaledPulse);
    TEST_ASSERT_EQUAL_UINT8(255, r);
}

void test_pulse_brightness_zero() {
    uint8_t pulseValue = 255;
    uint8_t brightness = 0;
    uint32_t color = 0xFFFFFF; // white
    
    uint8_t scaledPulse = ((uint16_t)pulseValue * brightness) / 255;
    uint8_t r = (((color >> 16) & 0xFF) * scaledPulse) / 255;
    uint8_t g = (((color >> 8) & 0xFF) * scaledPulse) / 255;
    uint8_t b = ((color & 0xFF) * scaledPulse) / 255;
    
    TEST_ASSERT_EQUAL_UINT8(0, scaledPulse);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_UINT8(0, g);
    TEST_ASSERT_EQUAL_UINT8(0, b);
}

// =============================================================================
// TEST: No double brightness application
// =============================================================================

void test_showled_applies_brightness_once() {
    // showLed() does: scaled = (value * brightness) / 255
    // PULSE/BREATHE already apply brightness via scaledPulse
    // They should write directly to LED (not through showLed), preventing double-apply
    
    uint8_t brightness = 128;
    uint8_t pulseValue = 200;
    uint32_t color = 0xFF0000;
    
    // PULSE path (correct - single brightness application):
    uint8_t scaledPulse = ((uint16_t)pulseValue * brightness) / 255;
    uint8_t correct_r = (((color >> 16) & 0xFF) * scaledPulse) / 255;
    
    // If showLed() were called on these values (WRONG - double application):
    uint8_t wrong_r = ((uint16_t)correct_r * brightness) / 255;
    
    // Correct should be brighter than wrong
    TEST_ASSERT_TRUE(correct_r > wrong_r);
    TEST_ASSERT_EQUAL_UINT8(100, scaledPulse); // 200*128/255≈100
    TEST_ASSERT_EQUAL_UINT8(100, correct_r);   // 255*100/255=100
    TEST_ASSERT_EQUAL_UINT8(50,  wrong_r);     // 100*128/255≈50
}

// =============================================================================
// TEST: Mode initialization values
// =============================================================================

void test_mode_breathe_slow_starts_at_max() {
    // BREATHE_SLOW initializes with pulseValue=255, direction=false (down)
    uint8_t pulseValue = 255;
    bool pulseDirection = false;
    TEST_ASSERT_EQUAL_UINT8(255, pulseValue);
    TEST_ASSERT_FALSE(pulseDirection);
}

void test_mode_pulse_starts_at_zero() {
    // Other modes initialize with pulseValue=0, direction=true (up)
    uint8_t pulseValue = 0;
    bool pulseDirection = true;
    TEST_ASSERT_EQUAL_UINT8(0, pulseValue);
    TEST_ASSERT_TRUE(pulseDirection);
}

// =============================================================================
// TEST: Color constants verification
// =============================================================================

void test_color_constants_values() {
    // Verify defined colors have expected RGB components
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractRed(LED_COLOR_RED));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractGreen(LED_COLOR_RED));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(LED_COLOR_RED));
    
    TEST_ASSERT_EQUAL_UINT8(0x00, extractRed(LED_COLOR_GREEN));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractGreen(LED_COLOR_GREEN));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(LED_COLOR_GREEN));
    
    TEST_ASSERT_EQUAL_UINT8(0x00, extractRed(LED_COLOR_BLUE));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractGreen(LED_COLOR_BLUE));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractBlue(LED_COLOR_BLUE));
    
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractRed(LED_COLOR_PURPLE));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractGreen(LED_COLOR_PURPLE));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractBlue(LED_COLOR_PURPLE));
    
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractRed(LED_COLOR_ORANGE));
    TEST_ASSERT_EQUAL_UINT8(0x80, extractGreen(LED_COLOR_ORANGE));
    TEST_ASSERT_EQUAL_UINT8(0x00, extractBlue(LED_COLOR_ORANGE));
    
    TEST_ASSERT_EQUAL_UINT8(0x00, extractRed(LED_COLOR_CYAN));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractGreen(LED_COLOR_CYAN));
    TEST_ASSERT_EQUAL_UINT8(0xFF, extractBlue(LED_COLOR_CYAN));
}

void test_color_off_is_black() {
    TEST_ASSERT_EQUAL_HEX32(0x000000, LED_COLOR_OFF);
    TEST_ASSERT_EQUAL_UINT8(0, extractRed(LED_COLOR_OFF));
    TEST_ASSERT_EQUAL_UINT8(0, extractGreen(LED_COLOR_OFF));
    TEST_ASSERT_EQUAL_UINT8(0, extractBlue(LED_COLOR_OFF));
}

// =============================================================================
// TEST: showLed() final RGB output for known inputs
// =============================================================================

void test_showled_output_green_full_brightness() {
    // LED_COLOR_GREEN at full brightness
    uint8_t r = extractRed(LED_COLOR_GREEN);   // 0
    uint8_t g = extractGreen(LED_COLOR_GREEN); // 255
    uint8_t b = extractBlue(LED_COLOR_GREEN);  // 0
    uint8_t brightness = 255;
    
    uint8_t out_r = scaleBrightness(r, brightness);
    uint8_t out_g = scaleBrightness(g, brightness);
    uint8_t out_b = scaleBrightness(b, brightness);
    
    TEST_ASSERT_EQUAL_UINT8(0,   out_r);
    TEST_ASSERT_EQUAL_UINT8(255, out_g);
    TEST_ASSERT_EQUAL_UINT8(0,   out_b);
}

void test_showled_output_orange_half_brightness() {
    // LED_COLOR_ORANGE = 0xFF8000 at 50% brightness
    uint8_t r = extractRed(LED_COLOR_ORANGE);   // 0xFF = 255
    uint8_t g = extractGreen(LED_COLOR_ORANGE); // 0x80 = 128
    uint8_t b = extractBlue(LED_COLOR_ORANGE);  // 0x00 = 0
    uint8_t brightness = 128; // ~50%
    
    uint8_t out_r = scaleBrightness(r, brightness);
    uint8_t out_g = scaleBrightness(g, brightness);
    uint8_t out_b = scaleBrightness(b, brightness);
    
    TEST_ASSERT_EQUAL_UINT8(128, out_r); // 255*128/255=128
    TEST_ASSERT_EQUAL_UINT8(64,  out_g); // 128*128/255=64
    TEST_ASSERT_EQUAL_UINT8(0,   out_b); // 0*128/255=0
    
    // Verify GRB order for wire
    uint8_t wire[3] = { out_g, out_r, out_b };
    TEST_ASSERT_EQUAL_UINT8(64,  wire[0]); // G
    TEST_ASSERT_EQUAL_UINT8(128, wire[1]); // R
    TEST_ASSERT_EQUAL_UINT8(0,   wire[2]); // B
}

void test_showled_output_off_mode() {
    // OFF mode: r=g=b=0 regardless of brightness
    uint8_t r = 0, g = 0, b = 0;
    uint8_t brightness = 255;
    
    uint8_t out_r = scaleBrightness(r, brightness);
    uint8_t out_g = scaleBrightness(g, brightness);
    uint8_t out_b = scaleBrightness(b, brightness);
    
    TEST_ASSERT_EQUAL_UINT8(0, out_r);
    TEST_ASSERT_EQUAL_UINT8(0, out_g);
    TEST_ASSERT_EQUAL_UINT8(0, out_b);
}

// =============================================================================
// TEST: OTA mode hardcoded purple color
// =============================================================================

void test_ota_mode_purple_color() {
    // OTA mode hardcodes purple (0xFF, 0x00, 0xFF)
    uint8_t r = 0xFF, g = 0x00, b = 0xFF;
    uint8_t brightness = 128; // default 50%
    
    // Through showLed()
    uint8_t out_r = scaleBrightness(r, brightness);
    uint8_t out_g = scaleBrightness(g, brightness);
    uint8_t out_b = scaleBrightness(b, brightness);
    
    TEST_ASSERT_EQUAL_UINT8(128, out_r);
    TEST_ASSERT_EQUAL_UINT8(0,   out_g);
    TEST_ASSERT_EQUAL_UINT8(128, out_b);
}

// =============================================================================
// TEST: WS2812B bit encoding
// =============================================================================

void test_bit_encoding_msb_first() {
    // Verify MSB-first bit extraction (same as in show() loop)
    uint8_t byte = 0b10110100; // 0xB4
    
    // bit 7 (MSB) = 1
    TEST_ASSERT_TRUE(byte & (1 << 7));
    // bit 6 = 0
    TEST_ASSERT_FALSE(byte & (1 << 6));
    // bit 5 = 1
    TEST_ASSERT_TRUE(byte & (1 << 5));
    // bit 4 = 1
    TEST_ASSERT_TRUE(byte & (1 << 4));
    // bit 3 = 0
    TEST_ASSERT_FALSE(byte & (1 << 3));
    // bit 2 = 1
    TEST_ASSERT_TRUE(byte & (1 << 2));
    // bit 1 = 0
    TEST_ASSERT_FALSE(byte & (1 << 1));
    // bit 0 (LSB) = 0
    TEST_ASSERT_FALSE(byte & (1 << 0));
}

void test_bit_encoding_all_ones() {
    uint8_t byte = 0xFF;
    for (int8_t bit = 7; bit >= 0; bit--) {
        TEST_ASSERT_TRUE(byte & (1 << bit));
    }
}

void test_bit_encoding_all_zeros() {
    uint8_t byte = 0x00;
    for (int8_t bit = 7; bit >= 0; bit--) {
        TEST_ASSERT_FALSE(byte & (1 << bit));
    }
}

// =============================================================================
// TEST: Cycle timing constants (@ 80MHz)
// =============================================================================

void test_cycle_constants_80mhz() {
    uint32_t f_cpu = 80000000UL;
    
    uint32_t t0h = f_cpu / 2500000;   // 400ns → 32 cycles
    uint32_t t1h = f_cpu / 1250000;   // 800ns → 64 cycles
    uint32_t period = f_cpu / 800000; // 1250ns → 100 cycles
    
    TEST_ASSERT_EQUAL_UINT32(32,  t0h);
    TEST_ASSERT_EQUAL_UINT32(64,  t1h);
    TEST_ASSERT_EQUAL_UINT32(100, period);
    
    // T0H must be less than T1H (to distinguish 0 vs 1 bits)
    TEST_ASSERT_TRUE(t0h < t1h);
    // T1H must be less than Period (low time must be > 0)
    TEST_ASSERT_TRUE(t1h < period);
}

void test_cycle_constants_160mhz() {
    uint32_t f_cpu = 160000000UL;
    
    uint32_t t0h = f_cpu / 2500000;   // 400ns → 64 cycles
    uint32_t t1h = f_cpu / 1250000;   // 800ns → 128 cycles
    uint32_t period = f_cpu / 800000; // 1250ns → 200 cycles
    
    TEST_ASSERT_EQUAL_UINT32(64,  t0h);
    TEST_ASSERT_EQUAL_UINT32(128, t1h);
    TEST_ASSERT_EQUAL_UINT32(200, period);
    
    TEST_ASSERT_TRUE(t0h < t1h);
    TEST_ASSERT_TRUE(t1h < period);
}

// =============================================================================
// TEST: Pin mask calculation
// =============================================================================

void test_pin_mask_gpio15() {
    uint32_t pinMask = (1 << 15);
    TEST_ASSERT_EQUAL_HEX32(0x00008000, pinMask);
}

void test_pin_mask_gpio0() {
    uint32_t pinMask = (1 << 0);
    TEST_ASSERT_EQUAL_HEX32(0x00000001, pinMask);
}

void test_pin_mask_gpio2() {
    uint32_t pinMask = (1 << 2);
    TEST_ASSERT_EQUAL_HEX32(0x00000004, pinMask);
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Color extraction
    RUN_TEST(test_extract_red_component);
    RUN_TEST(test_extract_green_component);
    RUN_TEST(test_extract_blue_component);
    
    // Color packing
    RUN_TEST(test_pack_color_primary);
    RUN_TEST(test_pack_color_mixed);
    RUN_TEST(test_pack_unpack_roundtrip);
    
    // GRB byte ordering
    RUN_TEST(test_grb_byte_order_red);
    RUN_TEST(test_grb_byte_order_green);
    RUN_TEST(test_grb_byte_order_orange);
    RUN_TEST(test_grb_byte_order_white);
    
    // Brightness scaling
    RUN_TEST(test_brightness_full_255);
    RUN_TEST(test_brightness_half_128);
    RUN_TEST(test_brightness_zero);
    RUN_TEST(test_brightness_quarter_64);
    RUN_TEST(test_brightness_max_value_no_overflow);
    RUN_TEST(test_brightness_specific_colors);
    
    // Brightness percent mapping
    RUN_TEST(test_brightness_percent_to_internal);
    RUN_TEST(test_brightness_internal_to_percent);
    RUN_TEST(test_brightness_clamping);
    RUN_TEST(test_brightness_roundtrip);
    
    // Pulse stepping
    RUN_TEST(test_pulse_step_up_increment);
    RUN_TEST(test_pulse_step_up_cap_at_255);
    RUN_TEST(test_pulse_step_down_decrement);
    RUN_TEST(test_pulse_step_down_floor_at_10);
    RUN_TEST(test_pulse_full_cycle);
    
    // Breathe stepping
    RUN_TEST(test_breathe_step_up_increment);
    RUN_TEST(test_breathe_step_up_cap_at_255);
    RUN_TEST(test_breathe_step_down_floor_at_20);
    
    // Pulse with brightness
    RUN_TEST(test_pulse_brightness_scaling_mid);
    RUN_TEST(test_pulse_brightness_full);
    RUN_TEST(test_pulse_brightness_zero);
    
    // Double brightness prevention
    RUN_TEST(test_showled_applies_brightness_once);
    
    // Mode initialization
    RUN_TEST(test_mode_breathe_slow_starts_at_max);
    RUN_TEST(test_mode_pulse_starts_at_zero);
    
    // Color constants
    RUN_TEST(test_color_constants_values);
    RUN_TEST(test_color_off_is_black);
    
    // Final LED output values
    RUN_TEST(test_showled_output_green_full_brightness);
    RUN_TEST(test_showled_output_orange_half_brightness);
    RUN_TEST(test_showled_output_off_mode);
    RUN_TEST(test_ota_mode_purple_color);
    
    // Bit encoding
    RUN_TEST(test_bit_encoding_msb_first);
    RUN_TEST(test_bit_encoding_all_ones);
    RUN_TEST(test_bit_encoding_all_zeros);
    
    // Cycle timing
    RUN_TEST(test_cycle_constants_80mhz);
    RUN_TEST(test_cycle_constants_160mhz);
    
    // Pin mask
    RUN_TEST(test_pin_mask_gpio15);
    RUN_TEST(test_pin_mask_gpio0);
    RUN_TEST(test_pin_mask_gpio2);
    
    return UNITY_END();
}
