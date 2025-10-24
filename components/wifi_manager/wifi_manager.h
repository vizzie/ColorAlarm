
#pragma once
#include "esp_event.h"
#include <stdbool.h>

typedef enum {
    WIFI_EVENT_CONNECTED,
    WIFI_EVENT_DISCONNECTED,
    WIFI_EVENT_GOT_IP,
    WIFI_EVENT_AP_STARTED
} wifi_manager_event_t;

typedef void (*wifi_manager_cb_t)(wifi_manager_event_t event, void *user_data);

void wifi_manager_init(wifi_manager_cb_t cb, void *user_data);
bool wifi_manager_set_credentials(const char *ssid, const char *pass);
bool wifi_manager_is_connected(void);
void wifi_disconnect(void);
