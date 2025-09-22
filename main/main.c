
#include "esp_log.h"
#include "storage_manager.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "neopixel_driver.h"
#include "neopixel_animations.h"
#include "alarm_manager.h"

#define LED_PIN 15
#define LED_COUNT 32

static const char *TAG = "MAIN";
static neopixel_t strip;

static void alarm_handler(void *user_data) {
    ESP_LOGI(TAG, "Alarm triggered → LED animation!");
    neopixel_animations_start(&strip, NEOPIXEL_ANIM_RAINBOW, 0, 0, 0);
}

static void time_synced(void *user) {
    ESP_LOGI(TAG, "Time synced callback");
    // When time is synced, you might change LED state to solid green, etc.
}

static void wifi_event_handler(wifi_manager_event_t event, void *user_data) {
    switch (event) {
        case WIFI_EVENT_GOT_IP:
            ESP_LOGI(TAG, "WiFi got IP, starting SNTP...");
            time_manager_init("pool.ntp.org",
                              "EST5EDT,M3.2.0/2,M11.1.0/2",
                              time_synced, NULL);
            break;
        case WIFI_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "Captive portal active (SSID: ESP32_Config).");
            break;
        default: break;
    }
}

void app_main(void) {
    // Init storage (NVS)
    storage_manager_init();

    // LEDs
    neopixel_init(&strip, LED_PIN, LED_COUNT, NEOPIXEL_ORDER_GRBW);
    neopixel_fill(&strip, 0, 0, 10, 0);
    neopixel_show(&strip);
    neopixel_animations_start(&strip, NEOPIXEL_ANIM_BREATH, 0, 0, 255); // blue breathing while booting

    // WiFi (loads saved creds or starts captive portal)
    wifi_manager_init(wifi_event_handler, NULL);

    // Alarms (persistent)
    alarm_manager_init();
    // Example: ensure one demo alarm exists at 12:00:00
    alarm_time_t alarm = { .hour = 13, .minute = 26, .second = 0 };
    alarm_manager_set_alarm("noon_alarm", alarm, alarm_handler, NULL);
}
