
#include "time_manager.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <string.h>

static time_sync_cb_t s_cb = NULL;
static void *s_user_data = NULL;

static void time_sync_notification_cb(struct timeval *tv) {
    if (s_cb) s_cb(s_user_data);
}

void time_manager_init(const char *ntp_server, const char *tz,
                       time_sync_cb_t cb, void *user_data) {
    s_cb = cb;
    s_user_data = user_data;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, (char*)ntp_server);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    setenv("TZ", tz, 1);
    tzset();
}

bool time_manager_get_local_time(struct tm *out) {
    time_t now; time(&now);
    if (now < 1000) return false;
    localtime_r(&now, out);
    return true;
}
