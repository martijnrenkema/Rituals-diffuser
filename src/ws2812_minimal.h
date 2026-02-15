/**
 * ws2812_minimal.h - Minimal WS2812B driver for ESP8266
 * 
 * Replaces NeoPixelBus for single-LED applications to save ~10KB Flash, ~280 bytes RAM.
 * Uses CPU cycle-counter for precise WS2812B timing that automatically adapts
 * to any CPU frequency (80MHz or 160MHz). Based on the same approach used by
 * the proven Adafruit NeoPixel library.
 * 
 * WS2812B Timing (800KHz, ±150ns tolerance):
 * - T0H: 400ns (0 bit high time)
 * - T0L: 850ns (0 bit low time)  
 * - T1H: 800ns (1 bit high time)
 * - T1L: 450ns (1 bit low time)
 * - Period: 1250ns per bit
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
    
    // Test accessors - allow verification of internal state
    uint8_t getRed() const { return _r; }
    uint8_t getGreen() const { return _g; }
    uint8_t getBlue() const { return _b; }
    uint8_t getPin() const { return _pin; }
    uint32_t getPinMask() const { return _pinMask; }
    
    /**
     * Send the color data to the LED
     * IRAM_ATTR ensures this runs from RAM for consistent timing
     * 
     * Uses CPU cycle counter (ccount register) instead of fixed NOP counts.
     * This automatically adapts to CPU frequency and is immune to compiler
     * optimization effects on timing.
     */
    void IRAM_ATTR show() {
        // WS2812B expects GRB byte order, not RGB
        uint8_t data[3] = { _g, _r, _b };
        
        // ESP8266 GPIO register addresses
        volatile uint32_t *set = (volatile uint32_t*)0x60000304;   // GPIO_OUT_W1TS (set bits)
        volatile uint32_t *clr = (volatile uint32_t*)0x60000308;   // GPIO_OUT_W1TC (clear bits)
        uint32_t pinMask = _pinMask;
        
        // Compute cycle thresholds from CPU frequency (auto-adapts to 80/160MHz)
        // F_CPU is defined by ESP8266 Arduino core based on board_build.f_cpu
        #ifndef F_CPU
            #define F_CPU 80000000L
        #endif
        
        // WS2812B timing in CPU cycles:
        // T0H = 400ns, T1H = 800ns, Period = 1250ns
        const uint32_t CYCLES_T0H    = (F_CPU / 2500000);   // 400ns: 32@80MHz, 64@160MHz
        const uint32_t CYCLES_T1H    = (F_CPU / 1250000);   // 800ns: 64@80MHz, 128@160MHz
        const uint32_t CYCLES_PERIOD = (F_CPU / 800000);    // 1250ns: 100@80MHz, 200@160MHz
        
        uint32_t startCycle, currentCycle;
        
        // Critical timing section - disable interrupts
        // Note: ESP8266 WiFi NMI cannot be disabled, but cycle-counter approach
        // handles minor jitter better than fixed NOP counts
        noInterrupts();
        
        // Send 24 bits (3 bytes) of GRB color data, MSB first
        for (uint8_t i = 0; i < 3; i++) {
            uint8_t byte = data[i];
            for (int8_t bit = 7; bit >= 0; bit--) {
                uint32_t highCycles = (byte & (1 << bit)) ? CYCLES_T1H : CYCLES_T0H;
                
                // Read CPU cycle counter (Xtensa ccount special register)
                __asm__ __volatile__("rsr %0, ccount" : "=a"(startCycle));
                
                // Set pin HIGH - start of bit
                *set = pinMask;
                
                // Spin until high-time elapsed
                do {
                    __asm__ __volatile__("rsr %0, ccount" : "=a"(currentCycle));
                } while ((currentCycle - startCycle) < highCycles);
                
                // Set pin LOW
                *clr = pinMask;
                
                // Spin until total bit period elapsed
                do {
                    __asm__ __volatile__("rsr %0, ccount" : "=a"(currentCycle));
                } while ((currentCycle - startCycle) < CYCLES_PERIOD);
            }
        }
        
        // Re-enable interrupts
        interrupts();
        
        // Reset/latch pulse - WS2812B needs >50µs low to latch data
        delayMicroseconds(60);
    }

private:
    uint8_t _pin = 15;
    uint32_t _pinMask = 0;
    uint8_t _r = 0, _g = 0, _b = 0;
};

#endif // PLATFORM_ESP8266

#endif // WS2812_MINIMAL_H
