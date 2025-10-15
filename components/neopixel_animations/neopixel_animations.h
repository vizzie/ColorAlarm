
#pragma once
#include "neopixel_driver.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NEOPIXEL_ANIM_NONE,
    NEOPIXEL_ANIM_BREATH,
    NEOPIXEL_ANIM_PULSE,
    NEOPIXEL_ANIM_RAINBOW,
    NEOPIXEL_ANIM_FADE_TO_SOLID,
    NEOPIXEL_ANIM_RAINBOW_SMOOTH
} neopixel_anim_mode_t;

void neopixel_animations_start(neopixel_t *strip, neopixel_anim_mode_t mode,
                               uint8_t r, uint8_t g, uint8_t b);
void neopixel_animations_stop(neopixel_t *strip);
void neopixel_animations_fade_to(neopixel_t *strip,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                                 uint32_t duration_ms);
void neopixel_animations_rainbow_smooth_start(neopixel_t *strip,
                                              uint32_t speed_ms_per_cycle,
                                              bool gradient,
                                              uint8_t saturation,
                                              uint8_t value);
