
#include "alarm_manager.h"
#include "storage_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "time_manager.h"
#include <string.h>

#define MAX_ALARMS 8
#define STORAGE_KEY "alarms_blob"

#define MAX_DURATION_TIMERS 8

typedef struct {
    bool in_use;
    TimerHandle_t h;
    alarm_callback_t cb;
    void *user_data;
    uint32_t duration_ms;
    uint32_t start_tick;   // for remaining time calculation
} duration_timer_t;

static duration_timer_t s_timers[MAX_DURATION_TIMERS];

typedef struct {
    char id[16];
    int day, hour, minute, second;
    bool active;
} alarm_persist_t;

typedef struct {
    char id[16];
    alarm_time_t time;
    alarm_callback_t cb;
    void *user_data;
    bool active;
} alarm_entry_t;

static alarm_entry_t s_alarms[MAX_ALARMS];

static void alarm_save_nvs(void) {
    alarm_persist_t persist[MAX_ALARMS] = {0};
    for (int i = 0; i < MAX_ALARMS; i++) {
        strncpy(persist[i].id, s_alarms[i].id, sizeof(persist[i].id)-1);
        persist[i].day = s_alarms[i].time.day;
        persist[i].hour = s_alarms[i].time.hour;
        persist[i].minute = s_alarms[i].time.minute;
        persist[i].second = s_alarms[i].time.second;
        persist[i].active = s_alarms[i].active;
    }
    storage_manager_set_blob(STORAGE_KEY, persist, sizeof(persist));
}

static void alarm_load_nvs(void) {
    alarm_persist_t persist[MAX_ALARMS] = {0};
    size_t len = 0;
    if (storage_manager_get_blob(STORAGE_KEY, persist, sizeof(persist), &len)) {
        int count = (int)(len / sizeof(alarm_persist_t));
        if (count > MAX_ALARMS) count = MAX_ALARMS;
        for (int i = 0; i < count; i++) {
            strncpy(s_alarms[i].id, persist[i].id, sizeof(s_alarms[i].id)-1);
            s_alarms[i].time.day = persist[i].day;
            s_alarms[i].time.hour = persist[i].hour;
            s_alarms[i].time.minute = persist[i].minute;
            s_alarms[i].time.second = persist[i].second;
            s_alarms[i].active = persist[i].active;
            s_alarms[i].cb = NULL;
            s_alarms[i].user_data = NULL;
        }
    }
}

static void alarm_task(void *arg) {
    while (1) {
        struct tm now;
        if (time_manager_get_local_time(&now)) {
            for (int i = 0; i < MAX_ALARMS; i++) {
                // if (s_alarms[i].active)
                // {
                //     ESP_LOGI("alarm", "checking alarm D=%d H=%d M=%d S=%d", s_alarms[i].time.day, s_alarms[i].time.hour, s_alarms[i].time.minute, s_alarms[i].time.second);
                // }
                if (s_alarms[i].active &&
                    s_alarms[i].time.day == now.tm_wday &&
                    s_alarms[i].time.hour == now.tm_hour &&
                    s_alarms[i].time.minute == now.tm_min &&
                    s_alarms[i].time.second == now.tm_sec) {
                    if (s_alarms[i].cb) s_alarms[i].cb(s_alarms[i].user_data);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void alarm_manager_init(void) {
    memset(s_alarms, 0, sizeof(s_alarms));
    //alarm_load_nvs(); // dont need to load from nvs as alarms are set on boot

    /* init duration timers table */
    memset(s_timers, 0, sizeof(s_timers));

    xTaskCreate(alarm_task, "alarm_task", 4096, NULL, 5, NULL);
}

bool alarm_manager_set_alarm(const char *id, alarm_time_t time,
                             alarm_callback_t cb, void *user_data) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!s_alarms[i].active || strcmp(s_alarms[i].id, id) == 0) {
            strncpy(s_alarms[i].id, id, sizeof(s_alarms[i].id)-1);
            s_alarms[i].time = time;
            s_alarms[i].cb = cb;
            s_alarms[i].user_data = user_data;
            s_alarms[i].active = true;
            //alarm_save_nvs(); //no need to store as alarms are set on boot up
            return true;
        }
    }
    return false;
}

bool alarm_manager_clear_alarm(const char *id) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (s_alarms[i].active && strcmp(s_alarms[i].id, id) == 0) {
            s_alarms[i].active = false;
            alarm_save_nvs();
            return true;
        }
    }
    return false;
}

/* FreeRTOS timer callback */
static void duration_timer_cb(TimerHandle_t xTimer) {
    // The timer_id is stored as the timer's ID (pvTimerID)
    intptr_t id = (intptr_t) pvTimerGetTimerID(xTimer);
    if (id < 0 || id >= MAX_DURATION_TIMERS) return;

    duration_timer_t *dt = &s_timers[id];
    if (!dt->in_use) return;

    // Snapshot callback before freeing slot
    alarm_callback_t cb = dt->cb;
    void *ud = dt->user_data;

    // Mark slot free (one-shot)
    dt->in_use = false;
    dt->h = NULL;
    dt->cb = NULL;
    dt->user_data = NULL;
    dt->duration_ms = 0;
    dt->start_tick = 0;

    if (cb) cb(ud);
}

/* Find a free duration timer slot */
static int alloc_timer_slot(void) {
    for (int i = 0; i < MAX_DURATION_TIMERS; i++) {
        if (!s_timers[i].in_use) return i;
    }
    return -1;
}

int alarm_manager_start_timer(uint32_t duration_ms,
                              alarm_callback_t cb, void *user_data) {
    int id = alloc_timer_slot();
    if (id < 0) return -1;

    duration_timer_t *dt = &s_timers[id];
    dt->in_use = true;
    dt->cb = cb;
    dt->user_data = user_data;
    dt->duration_ms = duration_ms;
    dt->start_tick = xTaskGetTickCount();

    // Create one-shot timer; store id in pvTimerID
    dt->h = xTimerCreate("am_oneshot",
                         pdMS_TO_TICKS(duration_ms),
                         pdFALSE,                 // one-shot
                         (void*) (intptr_t) id,  // pvTimerID
                         duration_timer_cb);
    if (!dt->h) {
        dt->in_use = false;
        return -1;
    }
    if (xTimerStart(dt->h, 0) != pdPASS) {
        xTimerDelete(dt->h, 0);
        dt->h = NULL;
        dt->in_use = false;
        return -1;
    }
    return id;
}

bool alarm_manager_cancel_timer(int timer_id) {
    if (timer_id < 0 || timer_id >= MAX_DURATION_TIMERS) return false;
    duration_timer_t *dt = &s_timers[timer_id];
    if (!dt->in_use || !dt->h) return false;

    if (xTimerStop(dt->h, 0) != pdPASS) return false;
    xTimerDelete(dt->h, 0);

    dt->in_use = false;
    dt->h = NULL;
    dt->cb = NULL;
    dt->user_data = NULL;
    dt->duration_ms = 0;
    dt->start_tick = 0;
    return true;
}

uint32_t alarm_manager_timer_remaining_ms(int timer_id) {
    if (timer_id < 0 || timer_id >= MAX_DURATION_TIMERS) return 0;
    duration_timer_t *dt = &s_timers[timer_id];
    if (!dt->in_use || !dt->h) return 0;

    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (uint32_t) ((now - dt->start_tick) * 1000 / configTICK_RATE_HZ);
    if (elapsed_ms >= dt->duration_ms) return 0;
    return dt->duration_ms - elapsed_ms;
}
