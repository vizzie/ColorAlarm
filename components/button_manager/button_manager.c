#include "button_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "button_manager";

typedef struct {
    gpio_num_t pin;
    uint32_t debounce_us;
    volatile int last_level;        // cached stable level
    volatile int64_t last_isr_t_us; // last accepted edge time
    button_cb_t cb;
    void *cb_user;
} bm_ctx_t;

static bm_ctx_t s_ctx;
static QueueHandle_t s_evtq = NULL;

typedef struct {
    int level;     // 0/1 at the time we accepted the edge
    int64_t t_us;  // timestamp (us)
} bm_evt_t;

static void IRAM_ATTR isr_handler(void *arg) {
    (void)arg;
    const int lvl = gpio_get_level(s_ctx.pin);
    const int64_t now = esp_timer_get_time();

    // Simple time-based debounce in ISR (fast)
    if ((now - s_ctx.last_isr_t_us) >= (int64_t)s_ctx.debounce_us) {
        s_ctx.last_isr_t_us = now;
        bm_evt_t evt = { .level = lvl, .t_us = now };
        BaseType_t hp_task_woken = pdFALSE;
        if (s_evtq) {
            xQueueSendFromISR(s_evtq, &evt, &hp_task_woken);
            if (hp_task_woken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

static void button_task(void *arg) {
    (void)arg;
    bm_evt_t evt;
    while (1) {
        if (xQueueReceive(s_evtq, &evt, portMAX_DELAY) == pdTRUE) {
            // Update cached level and call user callback (task context)
            s_ctx.last_level = evt.level;
            if (s_ctx.cb) {
                s_ctx.cb(s_ctx.cb_user);
            }
        }
    }
}

bool button_manager_init(gpio_num_t pin,
                         bool pullup,
                         bool pulldown,
                         uint32_t debounce_ms,
                         button_cb_t cb,
                         void *user_data) {
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.pin = pin;
    s_ctx.debounce_us = debounce_ms * 1000U;
    s_ctx.cb = cb;
    s_ctx.cb_user = user_data;
    s_ctx.last_level = -1;
    s_ctx.last_isr_t_us = 0;

    // Configure GPIO
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // Install ISR service (ignore already-installed)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, isr_handler, NULL));

    // Initialize queue + task
    s_evtq = xQueueCreate(8, sizeof(bm_evt_t));
    if (!s_evtq) return false;
    xTaskCreate(button_task, "button_task", 3072, NULL, 5, NULL);

    // Seed cached level; do NOT fire callback
    s_ctx.last_level = gpio_get_level(pin);
    return true;
}

int button_manager_get_level(void) {
    return s_ctx.last_level;
}

void button_manager_set_debounce(uint32_t debounce_ms) {
    s_ctx.debounce_us = debounce_ms * 1000U;
}
