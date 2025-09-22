#include "neopixel_animations.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

// ===== Existing globals =====
static neopixel_t *s_strip = NULL;
static neopixel_anim_mode_t s_mode = NEOPIXEL_ANIM_NONE;
static TaskHandle_t s_task = NULL;
static uint8_t s_r=0, s_g=0, s_b=0;

// ===== NEW: fade-to-solid state =====
static uint8_t *s_fade_start = NULL;     // snapshot of starting pixels (GRB/GRBW)
static uint8_t  s_target_r=0, s_target_g=0, s_target_b=0, s_target_w=0;
static uint32_t s_fade_duration_ms = 0;
static uint32_t s_fade_elapsed_ms = 0;

static void free_fade_buf(void) {
    if (s_fade_start) { vPortFree(s_fade_start); s_fade_start = NULL; }
}

static void begin_fade_snapshot(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint32_t dur_ms) {
    free_fade_buf();
    int bpp = s_strip->use_rgbw ? 4 : 3;
    size_t bytes = (size_t)s_strip->count * bpp;
    s_fade_start = (uint8_t *)pvPortMalloc(bytes);
    if (s_fade_start) {
        memcpy(s_fade_start, s_strip->pixels, bytes); // capture current frame
    }
    s_target_r = r; s_target_g = g; s_target_b = b; s_target_w = w;
    s_fade_duration_ms = dur_ms;
    s_fade_elapsed_ms = 0;
}

static void anim_task(void *arg) {
    uint32_t t = 0;
    const TickType_t tick_20ms = pdMS_TO_TICKS(20);
    while (1) {
        switch (s_mode) {
            case NEOPIXEL_ANIM_BREATH: {
                float phase = (float)((t % 2000) / 2000.0);
                float inten = 0.5 * (1.0 + sin(phase * 2.0 * 3.1415926535));
                uint8_t br = (uint8_t)(inten * 120.0);
                for (int i=0;i<s_strip->count;i++) {
                    neopixel_set_pixel(s_strip,i,
                        (uint8_t)((s_r*br)/255),(uint8_t)((s_g*br)/255),(uint8_t)((s_b*br)/255),0);
                }
                neopixel_show(s_strip);
                vTaskDelay(tick_20ms);
                t += 20;
                break;
            }
            case NEOPIXEL_ANIM_PULSE: {
                for (int i=0;i<s_strip->count;i++) neopixel_set_pixel(s_strip,i,s_r,s_g,s_b,0);
                neopixel_show(s_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                for (int i=0;i<s_strip->count;i++) neopixel_set_pixel(s_strip,i,0,0,0,0);
                neopixel_show(s_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                t += 1000;
                break;
            }
            case NEOPIXEL_ANIM_RAINBOW: {
                for (int i=0;i<s_strip->count;i++) {
                    uint8_t r = (uint8_t)((i*23 + t/10) % 255);
                    uint8_t g = (uint8_t)((i*47 + t/15) % 255);
                    uint8_t b = (uint8_t)((i*89 + t/20) % 255);
                    neopixel_set_pixel(s_strip,i,r,g,b,0);
                }
                neopixel_show(s_strip);
                vTaskDelay(pdMS_TO_TICKS(50));
                t += 5;
                break;
            }
            case NEOPIXEL_ANIM_FADE_TO_SOLID: {
                // step ~20ms
                vTaskDelay(tick_20ms);
                s_fade_elapsed_ms += 20;
                float u = (s_fade_duration_ms == 0) ? 1.0f :
                          (float)s_fade_elapsed_ms / (float)s_fade_duration_ms;
                if (u > 1.0f) u = 1.0f;

                int bpp = s_strip->use_rgbw ? 4 : 3;
                for (int i=0;i<s_strip->count;i++) {
                    uint8_t sr = 0, sg = 0, sb = 0, sw = 0;
                    if (s_fade_start) {
                        uint8_t *sp = &s_fade_start[i*bpp];
                        sg = sp[0]; sr = sp[1]; sb = sp[2];
                        if (s_strip->use_rgbw) sw = sp[3];
                    } else {
                        // no snapshot → assume start = current buffer values
                        uint8_t *sp = &s_strip->pixels[i*bpp];
                        sg = sp[0]; sr = sp[1]; sb = sp[2];
                        if (s_strip->use_rgbw) sw = sp[3];
                    }
                    // Lerp per channel: start + (target - start) * u
                    uint8_t nr = (uint8_t)(sr + (int)((int)s_target_r - (int)sr) * u);
                    uint8_t ng = (uint8_t)(sg + (int)((int)s_target_g - (int)sg) * u);
                    uint8_t nb = (uint8_t)(sb + (int)((int)s_target_b - (int)sb) * u);
                    uint8_t nw = s_strip->use_rgbw
                                 ? (uint8_t)(sw + (int)((int)s_target_w - (int)sw) * u)
                                 : 0;
                    neopixel_set_pixel(s_strip, i, nr, ng, nb, nw);
                }
                neopixel_show(s_strip);

                if (u >= 1.0f) {
                    // finalize on the solid color, keep task alive (mode becomes NONE)
                    free_fade_buf();
                    s_mode = NEOPIXEL_ANIM_NONE;
                }
                break;
            }
            default: {
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }
        }
    }
}

void neopixel_animations_start(neopixel_t *strip, neopixel_anim_mode_t mode,
                               uint8_t r, uint8_t g, uint8_t b) {
    s_strip = strip; s_mode = mode; s_r = r; s_g = g; s_b = b;
    // cancel any pending fade buffer if switching modes
    free_fade_buf();
    if (!s_task) xTaskCreate(anim_task,"anim_task",4096,NULL,5,&s_task);
}

void neopixel_animations_stop(neopixel_t *strip) {
    (void)strip;
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    s_mode = NEOPIXEL_ANIM_NONE;
    free_fade_buf();
}

void neopixel_animations_fade_to(neopixel_t *strip,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                                 uint32_t duration_ms) {
    s_strip = strip;
    if (!s_task) xTaskCreate(anim_task,"anim_task",4096,NULL,5,&s_task);
    begin_fade_snapshot(r, g, b, w, duration_ms);
    s_mode = NEOPIXEL_ANIM_FADE_TO_SOLID;
}
