
#include "alarm_manager.h"
#include "storage_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_manager.h"
#include <string.h>

#define MAX_ALARMS 8
#define STORAGE_KEY "alarms_blob"

typedef struct {
    char id[16];
    int hour, minute, second;
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
                if (s_alarms[i].active &&
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
    alarm_load_nvs();
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
            alarm_save_nvs();
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
