#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*button_cb_t)(void *user_data);

/**
 * Initialize a button on the given pin.
 * Triggers the callback on ANY edge (state change), with debounce.
 *
 * @param pin           GPIO number (e.g., GPIO_NUM_18)
 * @param pullup        Enable internal pull-up
 * @param pulldown      Enable internal pull-down
 * @param debounce_ms   Debounce window in milliseconds (e.g., 50)
 * @param cb            Callback invoked on debounced state change
 * @param user_data     Opaque pointer passed to cb
 * @return true on success
 */
bool button_manager_init(gpio_num_t pin,
                         bool pullup,
                         bool pulldown,
                         uint32_t debounce_ms,
                         button_cb_t cb,
                         void *user_data);

/** Optional: read the current (cached) level (0/1), or -1 if uninitialized */
int button_manager_get_level(void);

/** Optional: change debounce at runtime */
void button_manager_set_debounce(uint32_t debounce_ms);

#ifdef __cplusplus
}
#endif
