#include "alarm_manager.h"
#include "storage_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "time_manager.h"

#include <string.h>

#define STORAGE_KEY "alarms_blob_v2"
#define MAX_DURATION_TIMERS 8

typedef struct {
    bool in_use;
    char id[ALARM_ID_LEN];
    int day;
    int hour;
    int minute;
    int second;
    bool enabled;
} alarm_slot_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    alarm_slot_t alarms[ALARM_MANAGER_MAX_ALARMS];
    vacation_mode_t vacation_mode;
} alarm_store_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    alarm_slot_t alarms[ALARM_MANAGER_MAX_ALARMS];
} alarm_store_v2_t;

typedef struct {
    bool in_use;
    TimerHandle_t h;
    alarm_callback_t cb;
    void *user_data;
    uint32_t duration_ms;
    uint32_t start_tick;
} duration_timer_t;

static alarm_slot_t s_alarms[ALARM_MANAGER_MAX_ALARMS];
static duration_timer_t s_timers[MAX_DURATION_TIMERS];
static SemaphoreHandle_t s_lock;
static vacation_mode_t s_vacation_mode;

static alarm_callback_t s_alarm_cb;
static void *s_alarm_user_data;

static const uint32_t ALARM_STORE_MAGIC = 0x414C4D53; // "ALMS"
static const uint16_t ALARM_STORE_VERSION = 3;

static vacation_mode_t vacation_mode_default(void) {
    vacation_mode_t mode = {
        .enabled = false,
        .option = VACATION_MODE_OPTION_DO_NOTHING,
        .hour = 7,
        .minute = 0,
        .second = 0,
    };
    return mode;
}

static bool is_valid_alarm(const alarm_t *alarm) {
    if (!alarm || alarm->id[0] == '\0') {
        return false;
    }
    if (alarm->day < 0 || alarm->day > 6) {
        return false;
    }
    if (alarm->hour < 0 || alarm->hour > 23) {
        return false;
    }
    if (alarm->minute < 0 || alarm->minute > 59) {
        return false;
    }
    if (alarm->second < 0 || alarm->second > 59) {
        return false;
    }
    return true;
}

static bool is_valid_vacation_mode(const vacation_mode_t *mode) {
    if (!mode) {
        return false;
    }
    if (mode->option != VACATION_MODE_OPTION_ALARM &&
        mode->option != VACATION_MODE_OPTION_DO_NOTHING) {
        return false;
    }
    if (mode->hour < 0 || mode->hour > 23) {
        return false;
    }
    if (mode->minute < 0 || mode->minute > 59) {
        return false;
    }
    if (mode->second < 0 || mode->second > 59) {
        return false;
    }
    return true;
}

static void save_alarms_locked(void) {
    alarm_store_t store = {
        .magic = ALARM_STORE_MAGIC,
        .version = ALARM_STORE_VERSION,
        .count = ALARM_MANAGER_MAX_ALARMS,
    };

    memcpy(store.alarms, s_alarms, sizeof(s_alarms));
    store.vacation_mode = s_vacation_mode;
    storage_manager_set_blob(STORAGE_KEY, &store, sizeof(store));
}

static void load_alarms_locked(void) {
    alarm_store_t store = {0};
    alarm_store_v2_t store_v2 = {0};
    size_t len = 0;

    if (!storage_manager_get_blob(STORAGE_KEY, &store, sizeof(store), &len)) {
        return;
    }

    if (len == sizeof(store) &&
        store.magic == ALARM_STORE_MAGIC &&
        store.version == ALARM_STORE_VERSION) {
        memcpy(s_alarms, store.alarms, sizeof(s_alarms));
        if (is_valid_vacation_mode(&store.vacation_mode)) {
            s_vacation_mode = store.vacation_mode;
        } else {
            s_vacation_mode = vacation_mode_default();
        }
        return;
    }

    // Backwards compatibility: alarm store v2 had no vacation_mode field.
    memcpy(&store_v2, &store, sizeof(store_v2));
    if (len != sizeof(store_v2) || store_v2.magic != ALARM_STORE_MAGIC ||
        store_v2.version != 2) {
        return;
    }

    memcpy(s_alarms, store_v2.alarms, sizeof(s_alarms));
    s_vacation_mode = vacation_mode_default();
    save_alarms_locked();
}

static void alarm_task(void *arg) {
    (void)arg;

    while (1) {
        if (!s_lock) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct tm now;
        int triggers = 0;

        if (time_manager_get_local_time(&now)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_vacation_mode.enabled) {
                if (s_vacation_mode.option == VACATION_MODE_OPTION_ALARM &&
                    s_vacation_mode.hour == now.tm_hour &&
                    s_vacation_mode.minute == now.tm_min &&
                    s_vacation_mode.second == now.tm_sec) {
                    triggers++;
                }
            } else {
                for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS; i++) {
                    if (!s_alarms[i].in_use || !s_alarms[i].enabled) {
                        continue;
                    }
                    if (s_alarms[i].day == now.tm_wday &&
                        s_alarms[i].hour == now.tm_hour &&
                        s_alarms[i].minute == now.tm_min &&
                        s_alarms[i].second == now.tm_sec) {
                        triggers++;
                    }
                }
            }
            xSemaphoreGive(s_lock);

            while (triggers-- > 0 && s_alarm_cb) {
                s_alarm_cb(s_alarm_user_data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void alarm_manager_init(alarm_callback_t cb, void *user_data) {
    s_alarm_cb = cb;
    s_alarm_user_data = user_data;

    memset(s_alarms, 0, sizeof(s_alarms));
    memset(s_timers, 0, sizeof(s_timers));
    s_vacation_mode = vacation_mode_default();

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_alarms_locked();
    xSemaphoreGive(s_lock);

    xTaskCreate(alarm_task, "alarm_task", 4096, NULL, 5, NULL);
}

bool alarm_manager_upsert_alarm(const alarm_t *alarm) {
    if (!is_valid_alarm(alarm) || !s_lock) {
        return false;
    }

    bool updated = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS; i++) {
        if (s_alarms[i].in_use && strncmp(s_alarms[i].id, alarm->id, ALARM_ID_LEN) == 0) {
            strncpy(s_alarms[i].id, alarm->id, ALARM_ID_LEN - 1);
            s_alarms[i].id[ALARM_ID_LEN - 1] = '\0';
            s_alarms[i].day = alarm->day;
            s_alarms[i].hour = alarm->hour;
            s_alarms[i].minute = alarm->minute;
            s_alarms[i].second = alarm->second;
            s_alarms[i].enabled = alarm->enabled;
            updated = true;
            break;
        }
    }

    if (!updated) {
        for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS; i++) {
            if (!s_alarms[i].in_use) {
                s_alarms[i].in_use = true;
                strncpy(s_alarms[i].id, alarm->id, ALARM_ID_LEN - 1);
                s_alarms[i].id[ALARM_ID_LEN - 1] = '\0';
                s_alarms[i].day = alarm->day;
                s_alarms[i].hour = alarm->hour;
                s_alarms[i].minute = alarm->minute;
                s_alarms[i].second = alarm->second;
                s_alarms[i].enabled = alarm->enabled;
                updated = true;
                break;
            }
        }
    }

    if (updated) {
        save_alarms_locked();
    }

    xSemaphoreGive(s_lock);
    return updated;
}

bool alarm_manager_delete_alarm(const char *id) {
    if (!id || id[0] == '\0' || !s_lock) {
        return false;
    }

    bool deleted = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS; i++) {
        if (s_alarms[i].in_use && strncmp(s_alarms[i].id, id, ALARM_ID_LEN) == 0) {
            memset(&s_alarms[i], 0, sizeof(s_alarms[i]));
            deleted = true;
            break;
        }
    }

    if (deleted) {
        save_alarms_locked();
    }

    xSemaphoreGive(s_lock);
    return deleted;
}

bool alarm_manager_get_alarm(const char *id, alarm_t *out_alarm) {
    if (!id || !out_alarm || !s_lock) {
        return false;
    }

    bool found = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS; i++) {
        if (s_alarms[i].in_use && strncmp(s_alarms[i].id, id, ALARM_ID_LEN) == 0) {
            memset(out_alarm, 0, sizeof(*out_alarm));
            strncpy(out_alarm->id, s_alarms[i].id, ALARM_ID_LEN - 1);
            out_alarm->day = s_alarms[i].day;
            out_alarm->hour = s_alarms[i].hour;
            out_alarm->minute = s_alarms[i].minute;
            out_alarm->second = s_alarms[i].second;
            out_alarm->enabled = s_alarms[i].enabled;
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);

    return found;
}

size_t alarm_manager_list_alarms(alarm_t *out_alarms, size_t max_alarms) {
    if (!out_alarms || max_alarms == 0 || !s_lock) {
        return 0;
    }

    size_t count = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < ALARM_MANAGER_MAX_ALARMS && count < max_alarms; i++) {
        if (!s_alarms[i].in_use) {
            continue;
        }

        memset(&out_alarms[count], 0, sizeof(out_alarms[count]));
        strncpy(out_alarms[count].id, s_alarms[i].id, ALARM_ID_LEN - 1);
        out_alarms[count].day = s_alarms[i].day;
        out_alarms[count].hour = s_alarms[i].hour;
        out_alarms[count].minute = s_alarms[i].minute;
        out_alarms[count].second = s_alarms[i].second;
        out_alarms[count].enabled = s_alarms[i].enabled;
        count++;
    }
    xSemaphoreGive(s_lock);

    return count;
}

bool alarm_manager_set_vacation_mode(const vacation_mode_t *mode) {
    if (!is_valid_vacation_mode(mode) || !s_lock) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_vacation_mode = *mode;
    save_alarms_locked();
    xSemaphoreGive(s_lock);
    return true;
}

bool alarm_manager_get_vacation_mode(vacation_mode_t *out_mode) {
    if (!out_mode || !s_lock) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out_mode = s_vacation_mode;
    xSemaphoreGive(s_lock);
    return true;
}

static void duration_timer_cb(TimerHandle_t xTimer) {
    intptr_t id = (intptr_t)pvTimerGetTimerID(xTimer);
    if (id < 0 || id >= MAX_DURATION_TIMERS) {
        return;
    }

    duration_timer_t *dt = &s_timers[id];
    if (!dt->in_use) {
        return;
    }

    alarm_callback_t cb = dt->cb;
    void *ud = dt->user_data;

    dt->in_use = false;
    dt->h = NULL;
    dt->cb = NULL;
    dt->user_data = NULL;
    dt->duration_ms = 0;
    dt->start_tick = 0;

    if (cb) {
        cb(ud);
    }
}

static int alloc_timer_slot(void) {
    for (int i = 0; i < MAX_DURATION_TIMERS; i++) {
        if (!s_timers[i].in_use) {
            return i;
        }
    }
    return -1;
}

int alarm_manager_start_timer(uint32_t duration_ms,
                              alarm_callback_t cb, void *user_data) {
    int id = alloc_timer_slot();
    if (id < 0) {
        return -1;
    }

    duration_timer_t *dt = &s_timers[id];
    dt->in_use = true;
    dt->cb = cb;
    dt->user_data = user_data;
    dt->duration_ms = duration_ms;
    dt->start_tick = xTaskGetTickCount();

    dt->h = xTimerCreate("am_oneshot",
                         pdMS_TO_TICKS(duration_ms),
                         pdFALSE,
                         (void *)(intptr_t)id,
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
    if (timer_id < 0 || timer_id >= MAX_DURATION_TIMERS) {
        return false;
    }

    duration_timer_t *dt = &s_timers[timer_id];
    if (!dt->in_use || !dt->h) {
        return false;
    }

    if (xTimerStop(dt->h, 0) != pdPASS) {
        return false;
    }

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
    if (timer_id < 0 || timer_id >= MAX_DURATION_TIMERS) {
        return 0;
    }

    duration_timer_t *dt = &s_timers[timer_id];
    if (!dt->in_use || !dt->h) {
        return 0;
    }

    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed_ms = (uint32_t)((now - dt->start_tick) * 1000 / configTICK_RATE_HZ);
    if (elapsed_ms >= dt->duration_ms) {
        return 0;
    }

    return dt->duration_ms - elapsed_ms;
}
