
#include "neopixel_animations.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static neopixel_t *s_strip = NULL;
static neopixel_anim_mode_t s_mode = NEOPIXEL_ANIM_NONE;
static TaskHandle_t s_task = NULL;
static uint8_t s_r=0, s_g=0, s_b=0;

static void anim_task(void *arg) {
    uint32_t t = 0;
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
                vTaskDelay(pdMS_TO_TICKS(20));
                t += 20;
                break;
            }
            case NEOPIXEL_ANIM_PULSE: {
                for (int i=0;i<s_strip->count;i++) {
                    neopixel_set_pixel(s_strip,i,s_r,s_g,s_b,0);
                }
                neopixel_show(s_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                for (int i=0;i<s_strip->count;i++) {
                    neopixel_set_pixel(s_strip,i,0,0,0,0);
                }
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
                t += 50;
                break;
            }
            default:
                vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void neopixel_animations_start(neopixel_t *strip, neopixel_anim_mode_t mode,
                               uint8_t r, uint8_t g, uint8_t b) {
    s_strip=strip; s_mode=mode; s_r=r; s_g=g; s_b=b;
    if (!s_task) {
        xTaskCreate(anim_task,"anim_task",4096,NULL,5,&s_task);
    }
}

void neopixel_animations_stop(neopixel_t *strip) {
    (void)strip;
    if (s_task) { vTaskDelete(s_task); s_task=NULL; }
    s_mode = NEOPIXEL_ANIM_NONE;
}
