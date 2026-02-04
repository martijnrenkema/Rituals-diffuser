/**
 * ws2812_minimal.h - Minimal WS2812B driver for ESP8266
 * 
 * Replaces NeoPixelBus for single-LED applications to save ~10KB Flash, ~280 bytes RAM.
 * Uses direct GPIO bit-banging with precise timing for WS2812B protocol.
 * 
 * WS2812B Timing (800KHz):
 * - T0H: 400ns (0 bit high time)
 * - T0L: 850ns (0 bit low time)  
 * - T1H: 800ns (1 bit high time)
 * - T1L: 450ns (1 bit low time)
 * - Reset: >50µs low
 */
#ifndef WS2812_MINIMAL_H
#define WS2812_MINIMAL_H

#include <Arduino.h>

#ifdef PLATFORM_ESP8266

class WS2812Minimal {
public:
    /**
     * Initialize the LED pin
     * @param pin GPIO pin number (default 15 for ESP8266)
     */
    void begin(uint8_t pin = 15) {
        _pin = pin;
        _pinMask = (1 << _pin);
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);
    }
    
    /**
     * Set the color (will be shown on next show() call)
     * @param r Red value 0-255
     * @param g Green value 0-255
     * @param b Blue value 0-255
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b) {
        _r = r; 
        _g = g; 
        _b = b;
    }
    
    /**
     * Set color from a 24-bit RGB value
     * @param color 0xRRGGBB format
     */
    void setColor(uint32_t color) {
        _r = (color >> 16) & 0xFF;
        _g = (color >> 8) & 0xFF;
        _b = color & 0xFF;
    }
    
    /**
     * Send the color data to the LED
     * IRAM_ATTR ensures this runs from RAM for consistent timing
     */
    void IRAM_ATTR show() {
        // WS2812B expects GRB order, not RGB
        uint8_t data[3] = { _g, _r, _b };
        
        // GPIO register addresses for ESP8266
        volatile uint32_t *set = (volatile uint32_t*)0x60000304;   // GPIO_OUT_W1TS (set bits)
        volatile uint32_t *clr = (volatile uint32_t*)0x60000308;   // GPIO_OUT_W1TC (clear bits)
        
        // Critical timing section - disable interrupts to prevent WiFi interference
        noInterrupts();
        
        // Send 24 bits (3 bytes) of color data
        for (uint8_t byteIdx = 0; byteIdx < 3; byteIdx++) {
            uint8_t byte = data[byteIdx];
            
            // Send each bit, MSB first
            for (int8_t bit = 7; bit >= 0; bit--) {
                if (byte & (1 << bit)) {
                    // Send '1' bit: ~800ns high, ~450ns low
                    // At 80MHz: 64 cycles = 800ns, 36 cycles = 450ns
                    *set = _pinMask;
                    __asm__ __volatile__(
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                    );
                    *clr = _pinMask;
                    __asm__ __volatile__(
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop;"
                    );
                } else {
                    // Send '0' bit: ~400ns high, ~850ns low  
                    // At 80MHz: 32 cycles = 400ns, 68 cycles = 850ns
                    *set = _pinMask;
                    __asm__ __volatile__(
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                    );
                    *clr = _pinMask;
                    __asm__ __volatile__(
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop; nop; nop; nop; nop;"
                        "nop; nop; nop; nop;"
                    );
                }
            }
        }
        
        // Re-enable interrupts
        interrupts();
        
        // Reset pulse - WS2812B needs >50µs low to latch data
        delayMicroseconds(60);
    }

private:
    uint8_t _pin = 15;
    uint32_t _pinMask = 0;
    uint8_t _r = 0, _g = 0, _b = 0;
};

#endif // PLATFORM_ESP8266

#endif // WS2812_MINIMAL_H
