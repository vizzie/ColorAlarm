
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NEOPIXEL_ORDER_GRB,   // WS2812(B)
    NEOPIXEL_ORDER_GRBW   // SK6812 RGBW
} neopixel_order_t;

typedef struct {
    int pin;
    int count;
    uint8_t *pixels;     // raw bytes (3 or 4 per LED depending on order)
    neopixel_order_t order;
    bool use_rgbw;
} neopixel_t;

/**
 * Initialize strip
 * @param order NEOPIXEL_ORDER_GRB or NEOPIXEL_ORDER_GRBW
 */
void neopixel_init(neopixel_t *strip, int pin, int count, neopixel_order_t order);
void neopixel_set_pixel(neopixel_t *strip, int i, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
void neopixel_fill(neopixel_t *strip, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
/** Transmit current buffer to the LEDs using RMT */
void neopixel_show(neopixel_t *strip);
/** Optional: set all to off and show */
void neopixel_clear(neopixel_t *strip);
