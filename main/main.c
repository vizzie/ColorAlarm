
#include "esp_log.h"
#include "storage_manager.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "neopixel_driver.h"
#include "neopixel_animations.h"
#include "alarm_manager.h"
#include "button_manager.h"
#include "pot_manager.h"

#define BUTTON_PIN 18
#define POT_CH ADC1_CHANNEL_6
#define LED_PIN 15
#define LED_COUNT 32

static const char *TAG = "MAIN";
static neopixel_t strip;

int timer_id = -1;
bool button_on = false;
uint8_t g_brightness = 255;

bool time_manager_ready = false;

static void wake_alarm_handler(void *user_data) {
    ESP_LOGI(TAG, "Wake up alarm triggered → starting wake animation!");
    // neopixel_animations_fade_to(&strip, 255, 100, 0, 0, 2000);
    neopixel_set_brightness_cap(255);
    neopixel_animations_rainbow_smooth_start(&strip, 12000, false, 255, 255);  // rainbow!
    button_on = true;
}

static void timer_done(void *user) {
    neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000);
    button_on = false;
}

static void on_button_change(void *user) {
    button_on = !button_on;
    // Treat any edge as a "press" event
    alarm_manager_cancel_timer(timer_id);
    ESP_LOGI("MAIN", "Button pressed! level=%d, button_manager_get_level());
    neopixel_animations_stop(&strip);
    neopixel_set_brightness_cap(g_brightness);
    if (button_on == true)
    {
        // neopixel_animations_fade_to(&strip, 255, 80, 40, 255, 2000);
neopixel_animations_rainbow_smooth_start(&strip, 10000, false, 255, 255);
        timer_id = alarm_manager_start_timer(15 * 60 * 1000, timer_done, NULL);
    } else {
        neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000); // fade to black
    }
}

static void on_pot_change(uint16_t raw, uint8_t pct, void *user) {
    // Map 0..100% → 0..255 cap
    g_brightness = (uint8_t)((pct * 240U) / 100U) + 15;
    neopixel_set_brightness_cap(g_brightness);
    neopixel_show(&strip);            // <- force a resend so cap takes effect now
}

static void time_synced(void *user) {
    ESP_LOGI(TAG, "Time synced callback");
    // When time is synced, you might change LED state to solid green, etc.
    neopixel_animations_fade_to(&strip, 0, 0, 0, 0, 3000);
    button_on = false;
    time_manager_ready = true;
}

static void wifi_event_handler(wifi_manager_event_t event, void *user_data) {
    switch (event) {
        case WIFI_EVENT_GOT_IP:
            ESP_LOGI(TAG, "WiFi got IP, %s", (time_manager_ready == false) ? "starting SNTP..." : "reconnect success");
            if (time_manager_ready == false) {
                time_manager_init("pool.ntp.org",
                                "EST5EDT,M3.2.0/2,M11.1.0/2",
                                time_synced, NULL);
            }
            break;
        case WIFI_EVENT_AP_STARTED:
            ESP_LOGI(TAG, "Captive portal active (SSID: ESP32_Config).");
            break;
        case WIFI_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi disconnected!");
            break;
        default: break;
    }
}

void app_main(void) {
    // Init storage (NVS)
    storage_manager_init();

    // Button
    button_manager_init(BUTTON_PIN,
                    false,   // pull-up
                    true,  // pulldown (only one of these should be true)
                    50,     // debounce (ms)
                    on_button_change,
                    NULL);
    
    // Potentiometer
    pot_manager_init(POT_CH, 50, on_pot_change, NULL); // 50ms polling, notify on ~2% delta

    g_brightness = pot_manager_get_percent();

    // LEDs
    neopixel_init(&strip, LED_PIN, LED_COUNT, NEOPIXEL_ORDER_GRBW);
    neopixel_fill(&strip, 0, 0, 10, 0);
    neopixel_show(&strip);
    neopixel_animations_start(&strip, NEOPIXEL_ANIM_BREATH, 0, 0, 255); // blue breathing while booting

    // WiFi (loads saved creds or starts captive portal)
    wifi_manager_init(wifi_event_handler, NULL);

    // Alarms (persistent)
    alarm_manager_init();

    alarm_time_t weekend_alarm = { .day = 0, .hour = 7, .minute = 30, .second = 0 };
    alarm_manager_set_alarm("sunday", weekend_alarm, wake_alarm_handler, NULL);
    weekend_alarm.day = 6;
    alarm_manager_set_alarm("saturday", weekend_alarm, wake_alarm_handler, NULL);

    alarm_time_t weekday_alarm = { .day = 1, .hour = 6, .minute = 45, .second = 0 };
    alarm_manager_set_alarm("monday", weekday_alarm, wake_alarm_handler, NULL);
    weekday_alarm.day = 2;
    alarm_manager_set_alarm("tuesday", weekday_alarm, wake_alarm_handler, NULL);
    weekday_alarm.day = 3;
    alarm_manager_set_alarm("wednesday", weekday_alarm, wake_alarm_handler, NULL);
    weekday_alarm.day = 4;
    alarm_manager_set_alarm("thursday", weekday_alarm, wake_alarm_handler, NULL);
    weekday_alarm.day = 5;
    alarm_manager_set_alarm("friday", weekday_alarm, wake_alarm_handler, NULL);
}
