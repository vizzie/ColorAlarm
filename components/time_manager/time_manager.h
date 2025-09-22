
#pragma once
#include <stdbool.h>
#include <time.h>

typedef void (*time_sync_cb_t)(void *user_data);

void time_manager_init(const char *ntp_server, const char *tz,
                       time_sync_cb_t cb, void *user_data);
bool time_manager_get_local_time(struct tm *out);
