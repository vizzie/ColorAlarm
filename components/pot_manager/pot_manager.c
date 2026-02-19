#include "pot_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"

static struct {
    adc_channel_t chan;
    uint32_t period_ms;
    pot_cb_t cb;
    void *user;
    uint16_t raw;
    uint8_t percent;
    uint8_t notify_thresh;
    uint8_t alpha;
} s_pm;

static TaskHandle_t s_task = NULL;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

static inline uint16_t lerp_u16(uint16_t a, uint16_t b, uint8_t alpha) {
    return (uint16_t)((a * alpha + b * (100 - alpha)) / 100);
}

static void pot_task(void *arg) {
    (void)arg;

    uint8_t last_notified = 255;

    for (;;) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, s_pm.chan, &raw);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(s_pm.period_ms));
            continue;
        }

        if (raw < 0) {
            raw = 0;
        }
        if (raw > 4095) {
            raw = 4095;
        }

        if (s_pm.alpha > 0 && s_pm.alpha < 100) {
            s_pm.raw = lerp_u16(s_pm.raw, (uint16_t)raw, s_pm.alpha);
        } else {
            s_pm.raw = (uint16_t)raw;
        }

        uint8_t pct = (uint8_t)((s_pm.raw * 100U) / 4095U);
        if (pct > 100) {
            pct = 100;
        }
        s_pm.percent = pct;

        if (s_pm.cb) {
            if (last_notified == 255 ||
                (pct > last_notified ? (pct - last_notified) >= s_pm.notify_thresh
                                     : (last_notified - pct) >= s_pm.notify_thresh)) {
                last_notified = pct;
                s_pm.cb(s_pm.raw, s_pm.percent, s_pm.user);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(s_pm.period_ms));
    }
}

bool pot_manager_init(adc_channel_t channel, uint32_t sample_ms,
                      pot_cb_t cb, void *user) {
    s_pm.chan = channel;
    s_pm.period_ms = (sample_ms == 0) ? 50 : sample_ms;
    s_pm.cb = cb;
    s_pm.user = user;
    s_pm.raw = 0;
    s_pm.percent = 0;
    s_pm.notify_thresh = 2;
    s_pm.alpha = 80;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    if (adc_oneshot_new_unit(&init_cfg, &s_adc_handle) != ESP_OK) {
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    if (adc_oneshot_config_channel(s_adc_handle, s_pm.chan, &chan_cfg) != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return false;
    }

    xTaskCreate(pot_task, "pot_task", 3072, NULL, 5, &s_task);
    return s_task != NULL;
}

uint16_t pot_manager_get_raw(void) {
    return s_pm.raw;
}

uint8_t pot_manager_get_percent(void) {
    return s_pm.percent;
}

void pot_manager_set_notify_threshold_percent(uint8_t pct) {
    if (pct > 50) {
        pct = 50;
    }
    s_pm.notify_thresh = pct;
}

void pot_manager_set_smoothing(uint8_t alpha_percent) {
    if (alpha_percent > 100) {
        alpha_percent = 100;
    }
    s_pm.alpha = alpha_percent;
}
