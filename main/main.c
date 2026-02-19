#include "alarm_manager.h"
#include "button_manager.h"
#include "esp_log.h"
#include "neopixel_animations.h"
#include "neopixel_driver.h"
#include "pot_manager.h"
#include "storage_manager.h"
#include "time_manager.h"
#include "wifi_manager.h"

#define BUTTON_PIN 18
#define POT_CH ADC_CHANNEL_6
#define LED_PIN 15
#define LED_COUNT 32

static const char *TAG = "MAIN";
static neopixel_t strip;

int timer_id = -1;
bool button_on = false;
uint8_t g_brightness = 100;
uint8_t g_brightnessCap = 100;

bool time_manager_ready = false;

static void wake_alarm_handler(void *user_data) {
    ESP_LOGI(TAG, "Wake up alarm triggered -> starting wake animation!");
    // Avoid forcing max current draw during Wi-Fi activity; respect knob-controlled cap.
    neopixel_set_brightness_cap(g_brightnessCap);
    neopixel_animations_rainbow_smooth_start(&strip, 12000, false, 255, 160);
    button_on = true;
}

static void timer_done(void *user) {
    neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000);
    button_on = false;
}

static void on_button_change(void *user) {
    button_on = !button_on;

    alarm_manager_cancel_timer(timer_id);
    ESP_LOGI("MAIN", "Button pressed! level=%d", button_manager_get_level());
    neopixel_animations_stop(&strip);
    neopixel_set_brightness_cap(g_brightness);

    if (button_on == true) {
        neopixel_animations_fade_to(&strip, 0, 0, 0, 255, 2000);
        timer_id = alarm_manager_start_timer(15 * 60 * 1000, timer_done, NULL);
    } else {
        neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000);
    }
}

static void on_pot_change(uint16_t raw, uint8_t pct, void *user) {
    g_brightness = (uint8_t)((pct * g_brightnessCap) / 100U) + 15;
    neopixel_set_brightness_cap(g_brightness);
    neopixel_show(&strip);
}

static void time_synced(void *user) {
    ESP_LOGI(TAG, "Time synced callback");
    neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000);
    button_on = false;
    time_manager_ready = true;
}

static void wifi_event_handler(wifi_manager_event_t event, void *user_data) {
    switch (event) {
    case WIFI_EVENT_GOT_IP:
        ESP_LOGI(TAG, "WiFi got IP, %s",
                 (time_manager_ready == false) ? "starting SNTP..." : "reconnect success");
        if (time_manager_ready == false) {
            time_manager_init("pool.ntp.org", "EST5EDT,M3.2.0/2,M11.1.0/2", time_synced, NULL);
        }
        break;
    case WIFI_EVENT_AP_STARTED:
        ESP_LOGI(TAG, "Captive portal active (SSID: ESP32_Config).");
        break;
    case WIFI_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WiFi disconnected!");
        break;
    default:
        break;
    }
}

void app_main(void) {
    if (!storage_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize storage manager");
    }

    button_manager_init(BUTTON_PIN,
                        false,
                        true,
                        50,
                        on_button_change,
                        NULL);

    pot_manager_init(POT_CH, 50, on_pot_change, NULL);

    g_brightness = (uint8_t)((pot_manager_get_percent() * (uint16_t)g_brightnessCap) / 100U);

    neopixel_init(&strip, LED_PIN, LED_COUNT, NEOPIXEL_ORDER_GRBW);
    neopixel_fill(&strip, 0, 0, 10, 0);
    neopixel_show(&strip);
    neopixel_animations_start(&strip, NEOPIXEL_ANIM_BREATH, 0, 0, 255);

    wifi_manager_init(wifi_event_handler, NULL);

    while (time_manager_ready == false) {
        vTaskDelay(10);
    }

    alarm_manager_init(wake_alarm_handler, NULL);
    ESP_LOGI(TAG, "Alarm manager initialized with persistent alarm storage.");
}
