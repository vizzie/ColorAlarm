
#pragma once
#include "neopixel_driver.h"
#include <stdint.h>

typedef enum {
    NEOPIXEL_ANIM_NONE,
    NEOPIXEL_ANIM_BREATH,
    NEOPIXEL_ANIM_PULSE,
    NEOPIXEL_ANIM_RAINBOW
} neopixel_anim_mode_t;

void neopixel_animations_start(neopixel_t *strip, neopixel_anim_mode_t mode,
                               uint8_t r, uint8_t g, uint8_t b);
void neopixel_animations_stop(neopixel_t *strip);
