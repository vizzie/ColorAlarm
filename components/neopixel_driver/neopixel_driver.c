
#include "neopixel_driver.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "neopixel_driver";

/* RMT timing based on 40MHz (25ns per tick) by setting clk_div=2 on 80MHz APB */
#define RMT_CLK_DIV     2   // 80MHz/2 = 40MHz (25ns/tick)

// WS2812/SK6812 typical timings (ns)
#define T0H_NS  350
#define T0L_NS  800
#define T1H_NS  700
#define T1L_NS  600
#define RESET_US 80  // reset code >50us

// Convert ns/us to RMT ticks (25ns per tick)
#define NS_TO_TICKS(ns)  ((uint32_t)(((ns) + 24) / 25))   // round up
#define US_TO_TICKS(us)  NS_TO_TICKS((us) * 1000)

static inline rmt_item32_t bit0_item(void) {
    rmt_item32_t i = {0};
    i.level0 = 1; i.duration0 = NS_TO_TICKS(T0H_NS);
    i.level1 = 0; i.duration1 = NS_TO_TICKS(T0L_NS);
    return i;
}
static inline rmt_item32_t bit1_item(void) {
    rmt_item32_t i = {0};
    i.level0 = 1; i.duration0 = NS_TO_TICKS(T1H_NS);
    i.level1 = 0; i.duration1 = NS_TO_TICKS(T1L_NS);
    return i;
}

typedef struct {
    rmt_channel_t channel;
    rmt_item32_t *items;
    size_t items_len;
} neopixel_rmt_t;

static neopixel_rmt_t s_rmt = {0};

void neopixel_init(neopixel_t *strip, int pin, int count, neopixel_order_t order) {
    strip->pin = pin;
    strip->count = count;
    strip->order = order;
    strip->use_rgbw = (order == NEOPIXEL_ORDER_GRBW);
    int bpp = strip->use_rgbw ? 4 : 3;
    strip->pixels = (uint8_t*)calloc(count, bpp);

    // Configure RMT
    s_rmt.channel = RMT_CHANNEL_0;
    rmt_config_t cfg = {
        .rmt_mode = RMT_MODE_TX,
        .channel = s_rmt.channel,
        .gpio_num = pin,
        .clk_div = RMT_CLK_DIV,
        .mem_block_num = 2,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_output_en = true,
            .idle_level = RMT_IDLE_LEVEL_LOW
        }
    };
    rmt_config(&cfg);
    rmt_driver_install(s_rmt.channel, 0, 0);

    ESP_LOGI(TAG, "Init on GPIO %d, LEDs=%d, %s", pin, count, strip->use_rgbw ? "RGBW" : "RGB");
}

void neopixel_set_pixel(neopixel_t *strip, int i, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (!strip || !strip->pixels) return;
    if (i < 0 || i >= strip->count) return;
    int bpp = strip->use_rgbw ? 4 : 3;
    uint8_t *p = &strip->pixels[i*bpp];
    // Most strips expect GRB (and GRBW for SK6812)
    p[0] = g;
    p[1] = r;
    p[2] = b;
    if (strip->use_rgbw) p[3] = w;
}

void neopixel_fill(neopixel_t *strip, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (!strip) return;
    for (int i=0;i<strip->count;i++) neopixel_set_pixel(strip,i,r,g,b,w);
}

void neopixel_clear(neopixel_t *strip) {
    if (!strip || !strip->pixels) return;
    memset(strip->pixels, 0, strip->count * (strip->use_rgbw ? 4 : 3));
    neopixel_show(strip);
}

void neopixel_show(neopixel_t *strip) {
    if (!strip || !strip->pixels) return;
    int bpp = strip->use_rgbw ? 4 : 3;
    const size_t nbits = strip->count * bpp * 8;
    // Allocate items: one rmt item per bit + reset tail
    size_t reset_items = 1;
    size_t total_items = nbits + reset_items;
    if (s_rmt.items_len < total_items || !s_rmt.items) {
        free(s_rmt.items);
        s_rmt.items = (rmt_item32_t*)calloc(total_items, sizeof(rmt_item32_t));
        s_rmt.items_len = total_items;
    }

    rmt_item32_t b0 = bit0_item();
    rmt_item32_t b1 = bit1_item();

    size_t k = 0;
    for (int i = 0; i < strip->count; i++) {
        uint8_t *p = &strip->pixels[i*bpp];
        for (int j = 0; j < bpp; j++) {
            uint8_t byte = p[j];
            for (int bit = 7; bit >= 0; bit--) {
                bool one = (byte >> bit) & 0x01;
                s_rmt.items[k++] = one ? b1 : b0;
            }
        }
    }
    // Reset pulse (low) for >50us
    rmt_item32_t reset = {0};
    reset.level0 = 0;
    reset.duration0 = US_TO_TICKS(RESET_US);
    reset.level1 = 0;
    reset.duration1 = 0;
    s_rmt.items[k++] = reset;

    rmt_write_items(s_rmt.channel, s_rmt.items, k, true);
    rmt_wait_tx_done(s_rmt.channel, portMAX_DELAY);
}
