#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALARM_MANAGER_MAX_ALARMS 8
#define ALARM_ID_LEN 37

typedef void (*alarm_callback_t)(void *user_data);

typedef struct {
    char id[ALARM_ID_LEN];
    int day; // 0 = Sunday
    int hour;
    int minute;
    int second;
    bool enabled;
} alarm_t;

typedef enum {
    VACATION_MODE_OPTION_ALARM = 0,
    VACATION_MODE_OPTION_DO_NOTHING = 1,
} vacation_mode_option_t;

typedef struct {
    bool enabled;
    vacation_mode_option_t option;
    int hour;
    int minute;
    int second;
} vacation_mode_t;

void alarm_manager_init(alarm_callback_t cb, void *user_data);

bool alarm_manager_upsert_alarm(const alarm_t *alarm);
bool alarm_manager_delete_alarm(const char *id);
bool alarm_manager_get_alarm(const char *id, alarm_t *out_alarm);
size_t alarm_manager_list_alarms(alarm_t *out_alarms, size_t max_alarms);
bool alarm_manager_set_vacation_mode(const vacation_mode_t *mode);
bool alarm_manager_get_vacation_mode(vacation_mode_t *out_mode);

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
