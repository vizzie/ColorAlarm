#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*alarm_callback_t)(void *user_data);

typedef struct {
    int hour;
    int minute;
    int second;
} alarm_time_t;

void alarm_manager_init(void);
bool alarm_manager_set_alarm(const char *id, alarm_time_t time,
                             alarm_callback_t cb, void *user_data);
bool alarm_manager_clear_alarm(const char *id);

/**
 * @brief Start a one-shot timer that expires after duration_ms and fires cb(user_data).
 * @return >=0 timer id on success, -1 on failure.
 *
 * NOTE: These duration timers are NOT persisted in NVS (by default).
 *       They survive only until reboot or explicit cancellation.
 */
int alarm_manager_start_timer(uint32_t duration_ms,
                              alarm_callback_t cb, void *user_data);

/**
 * @brief Cancel a one-shot timer.
 * @param timer_id id returned by alarm_manager_start_timer()
 * @return true if cancelled; false if id invalid or already expired.
 */
bool alarm_manager_cancel_timer(int timer_id);

/**
 * @brief Get remaining time (best-effort) for an active timer.
 * @return remaining milliseconds; 0 if expired or id invalid.
 */
uint32_t alarm_manager_timer_remaining_ms(int timer_id);
