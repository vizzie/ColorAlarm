
#include "wifi_manager.h"
#include "storage_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <string.h>

#define WIFI_KEY "wifi_creds"
#define WIFI_CONNECT_RETRIES 5

static const char *TAG = "wifi_manager";

typedef struct {
    char ssid[32];
    char pass[64];
} wifi_credentials_t;

static wifi_manager_cb_t s_callback = NULL;
static void *s_user_data = NULL;
static bool s_connected = false;
static int s_retry_count = 0;
static httpd_handle_t s_httpd = NULL;

static void start_captive_portal(void);

static bool wifi_manager_load_credentials(wifi_credentials_t *out) {
    size_t len = sizeof(*out);
    return storage_manager_get_blob(WIFI_KEY, out, len, NULL);
}

bool wifi_manager_set_credentials(const char *ssid, const char *pass) {
    wifi_credentials_t creds = {0};
    strncpy(creds.ssid, ssid, sizeof(creds.ssid)-1);
    strncpy(creds.pass, pass, sizeof(creds.pass)-1);
    return storage_manager_set_blob(WIFI_KEY, &creds, sizeof(creds));
}

bool wifi_manager_is_connected(void) { return s_connected; }

static esp_err_t root_get_handler(httpd_req_t *req) {
    const char resp[] =
        "<!DOCTYPE html><html><body>"
        "<h2>WiFi Setup</h2>"
        "<form action=\"/connect\" method=\"post\">"
        "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
        "Password:<br><input type=\"password\" name=\"pass\"><br><br>"
        "<input type=\"submit\" value=\"Save\">"
        "</form></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[ret] = '\0';
    char ssid[32] = {0}, pass[64] = {0};
    sscanf(buf, "ssid=%31[^&]&pass=%63s", ssid, pass);
    for (char *p = ssid; *p; p++) if (*p == '+') *p = ' ';
    for (char *p = pass; *p; p++) if (*p == '+') *p = ' ';
    wifi_manager_set_credentials(ssid, pass);
    httpd_resp_sendstr(req, "Credentials saved. Rebooting...");
    esp_restart();
    return ESP_OK;
}

static httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
static httpd_uri_t uri_connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };

static void start_captive_portal(void) {
    ESP_LOGI(TAG, "Starting AP + Captive Portal");
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Config",
            .ssid_len = 12,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    esp_netif_create_default_wifi_ap();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    if (s_callback) s_callback(WIFI_EVENT_AP_STARTED, s_user_data);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(s_httpd, &uri_root);
        httpd_register_uri_handler(s_httpd, &uri_connect);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < WIFI_CONNECT_RETRIES) {
            s_retry_count++;
            esp_wifi_connect();
        } else {
            start_captive_portal();
        }
        if (s_callback) s_callback(WIFI_EVENT_DISCONNECTED, s_user_data);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_retry_count = 0;
        if (s_callback) s_callback(WIFI_EVENT_GOT_IP, s_user_data);
    }
}

void wifi_manager_init(wifi_manager_cb_t cb, void *user_data) {
    s_callback = cb; s_user_data = user_data;
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_credentials_t creds = {0};
    wifi_config_t wifi_config = {0};
    if (wifi_manager_load_credentials(&creds)) {
        strncpy((char*)wifi_config.sta.ssid, creds.ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, creds.pass, sizeof(wifi_config.sta.password));
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
    } else {
        start_captive_portal();
    }
}

void wifi_disconnect(void) {
    ESP_LOGI(TAG, "Attempting to disconnect wifi");
    esp_err_t err = esp_wifi_disconnect();
    ESP_LOGI(TAG, "Wifi disconnect %s", err == ESP_OK ? "success" : "failed");
}