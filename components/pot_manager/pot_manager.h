#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pot_cb_t)(uint16_t raw, uint8_t percent, void *user);

/**
 * Initialize potentiometer sampling on ADC1.
 * @param channel     ADC1 channel (e.g., ADC1_CHANNEL_6 for GPIO34)
 * @param sample_ms   Poll period (e.g., 50 ms)
 * @param cb          Optional callback on significant change (may be NULL)
 * @param user        Opaque pointer passed to cb
 * @return true on success
 */
bool pot_manager_init(adc1_channel_t channel, uint32_t sample_ms,
                      pot_cb_t cb, void *user);

/** Get last filtered raw (0..4095) */
uint16_t pot_manager_get_raw(void);

/** Get last mapped percent (0..100) */
uint8_t pot_manager_get_percent(void);

/** Configure how much change (%) triggers the callback (default 2%) */
void pot_manager_set_notify_threshold_percent(uint8_t pct);

/** Set IIR smoothing factor (0..100, higher = smoother; default 80) */
void pot_manager_set_smoothing(uint8_t alpha_percent);

#ifdef __cplusplus
}
#endif
