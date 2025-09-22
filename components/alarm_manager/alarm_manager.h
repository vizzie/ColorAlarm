
#pragma once
#include <stdbool.h>

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
