#include "wifi_manager.h"
#include "alarm_manager.h"
#include "storage_manager.h"

#include "cJSON.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIFI_KEY "wifi_creds"
#define WIFI_CONNECT_RETRIES 5
#define MDNS_HOSTNAME "coloralarm"
#define WEB_BASE_PATH "/spiffs"
#define WEB_PARTITION_LABEL "spiffs"

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
static bool s_web_fs_ready = false;
static bool s_mdns_ready = false;

static void start_captive_portal(void);
static void ensure_http_server_started(void);
static void ensure_mdns_started(void);
static bool register_uri_checked(const httpd_uri_t *uri);

static bool wifi_manager_load_credentials(wifi_credentials_t *out) {
    size_t len = sizeof(*out);
    return storage_manager_get_blob(WIFI_KEY, out, len, NULL);
}

bool wifi_manager_set_credentials(const char *ssid, const char *pass) {
    wifi_credentials_t creds = {0};
    strncpy(creds.ssid, ssid, sizeof(creds.ssid) - 1);
    strncpy(creds.pass, pass, sizeof(creds.pass) - 1);
    return storage_manager_set_blob(WIFI_KEY, &creds, sizeof(creds));
}

bool wifi_manager_is_connected(void) { return s_connected; }

static bool file_exists(const char *path) {
    struct stat st;
    return (path != NULL) && (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

static bool path_ends_with(const char *path, const char *suffix) {
    if (!path || !suffix) {
        return false;
    }
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > path_len) {
        return false;
    }
    return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool request_accepts_gzip(httpd_req_t *req) {
    if (!req) {
        return false;
    }

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
    if (hdr_len == 0) {
        return false;
    }

    char *enc = calloc(1, hdr_len + 1);
    if (!enc) {
        return false;
    }

    bool accepts = false;
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", enc, hdr_len + 1) == ESP_OK &&
        strstr(enc, "gzip") != NULL) {
        accepts = true;
    }

    free(enc);
    return accepts;
}

static bool file_has_extension(const char *path) {
    if (!path) {
        return false;
    }
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    return (dot != NULL) && (slash == NULL || dot > slash);
}

static const char *mime_type_from_path(const char *path) {
    if (!path) {
        return "application/octet-stream";
    }
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".svg")) return "image/svg+xml";
    if (strstr(path, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t send_file_response(httpd_req_t *req, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_type_from_path(path));
    if (path_ends_with(path, ".gz")) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    }

    char buf[1024];
    size_t read_sz = 0;
    while ((read_sz = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_sz) != ESP_OK) {
            fclose(f);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static void web_fs_init(void) {
    if (s_web_fs_ready) {
        return;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_BASE_PATH,
        .partition_label = WEB_PARTITION_LABEL,
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK) {
        s_web_fs_ready = true;
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
}

static bool read_req_body(httpd_req_t *req, char **out_body) {
    if (!req || !out_body || req->content_len <= 0 || req->content_len > 2048) {
        return false;
    }

    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        return false;
    }

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            return false;
        }
        received += ret;
    }

    body[req->content_len] = '\0';
    *out_body = body;
    return true;
}

static void send_json_error(httpd_req_t *req, const char *status, const char *message) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return;
    }

    cJSON_AddStringToObject(obj, "error", message);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status);
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
}

static bool parse_alarm_json(cJSON *obj, alarm_t *alarm, const char *id_override,
                             bool allow_missing_id) {
    if (!obj || !alarm) {
        return false;
    }

    memset(alarm, 0, sizeof(*alarm));

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    const cJSON *day = cJSON_GetObjectItemCaseSensitive(obj, "day");
    const cJSON *hour = cJSON_GetObjectItemCaseSensitive(obj, "hour");
    const cJSON *minute = cJSON_GetObjectItemCaseSensitive(obj, "minute");
    const cJSON *second = cJSON_GetObjectItemCaseSensitive(obj, "second");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(obj, "enabled");

    if (!cJSON_IsNumber(day) || !cJSON_IsNumber(hour) || !cJSON_IsNumber(minute) ||
        !cJSON_IsNumber(second) || !cJSON_IsBool(enabled)) {
        return false;
    }

    const char *alarm_id = id_override;
    if (!alarm_id) {
        if (cJSON_IsString(id) && id->valuestring != NULL) {
            alarm_id = id->valuestring;
        } else if (allow_missing_id) {
            alarm_id = "";
        } else {
            return false;
        }
    }

    if (strlen(alarm_id) >= ALARM_ID_LEN) {
        return false;
    }

    strncpy(alarm->id, alarm_id, ALARM_ID_LEN - 1);
    alarm->id[ALARM_ID_LEN - 1] = '\0';
    alarm->day = day->valueint;
    alarm->hour = hour->valueint;
    alarm->minute = minute->valueint;
    alarm->second = second->valueint;
    alarm->enabled = cJSON_IsTrue(enabled);
    return true;
}

static void generate_alarm_uuid(char *out_id, size_t out_len) {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));

    bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant RFC 4122

    snprintf(out_id, out_len,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static bool generate_unique_alarm_id(char *out_id, size_t out_len) {
    alarm_t existing;
    for (int i = 0; i < 8; i++) {
        generate_alarm_uuid(out_id, out_len);
        if (!alarm_manager_get_alarm(out_id, &existing)) {
            return true;
        }
    }
    return false;
}

static void alarm_to_json_obj(const alarm_t *alarm, cJSON *obj) {
    cJSON_AddStringToObject(obj, "id", alarm->id);
    cJSON_AddNumberToObject(obj, "day", alarm->day);
    cJSON_AddNumberToObject(obj, "hour", alarm->hour);
    cJSON_AddNumberToObject(obj, "minute", alarm->minute);
    cJSON_AddNumberToObject(obj, "second", alarm->second);
    cJSON_AddBoolToObject(obj, "enabled", alarm->enabled);
}

static const char *vacation_option_to_str(vacation_mode_option_t option) {
    switch (option) {
    case VACATION_MODE_OPTION_ALARM:
        return "alarm";
    case VACATION_MODE_OPTION_DO_NOTHING:
        return "do_nothing";
    default:
        return "do_nothing";
    }
}

static bool vacation_option_from_str(const char *value, vacation_mode_option_t *out_option) {
    if (!value || !out_option) {
        return false;
    }
    if (strcmp(value, "alarm") == 0) {
        *out_option = VACATION_MODE_OPTION_ALARM;
        return true;
    }
    if (strcmp(value, "do_nothing") == 0) {
        *out_option = VACATION_MODE_OPTION_DO_NOTHING;
        return true;
    }
    return false;
}

static void vacation_mode_to_json_obj(const vacation_mode_t *mode, cJSON *obj) {
    cJSON_AddBoolToObject(obj, "enabled", mode->enabled);
    cJSON_AddStringToObject(obj, "option", vacation_option_to_str(mode->option));
    cJSON_AddNumberToObject(obj, "hour", mode->hour);
    cJSON_AddNumberToObject(obj, "minute", mode->minute);
    cJSON_AddNumberToObject(obj, "second", mode->second);
}

static bool parse_vacation_mode_json(cJSON *obj, vacation_mode_t *mode) {
    if (!obj || !mode) {
        return false;
    }

    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(obj, "enabled");
    const cJSON *option = cJSON_GetObjectItemCaseSensitive(obj, "option");
    const cJSON *hour = cJSON_GetObjectItemCaseSensitive(obj, "hour");
    const cJSON *minute = cJSON_GetObjectItemCaseSensitive(obj, "minute");
    const cJSON *second = cJSON_GetObjectItemCaseSensitive(obj, "second");

    if (!cJSON_IsBool(enabled) || !cJSON_IsString(option) || !option->valuestring) {
        return false;
    }
    if (!cJSON_IsNumber(hour) || !cJSON_IsNumber(minute) || !cJSON_IsNumber(second)) {
        return false;
    }

    vacation_mode_option_t parsed_option;
    if (!vacation_option_from_str(option->valuestring, &parsed_option)) {
        return false;
    }

    vacation_mode_t parsed = {
        .enabled = cJSON_IsTrue(enabled),
        .option = parsed_option,
        .hour = hour->valueint,
        .minute = minute->valueint,
        .second = second->valueint,
    };

    if (parsed.hour < 0 || parsed.hour > 23 ||
        parsed.minute < 0 || parsed.minute > 59 ||
        parsed.second < 0 || parsed.second > 59) {
        return false;
    }

    *mode = parsed;
    return true;
}

static bool get_alarm_id_from_uri(const httpd_req_t *req, char *out_id, size_t out_len) {
    const char *prefix = "/api/alarms/";
    size_t prefix_len = strlen(prefix);

    if (!req || !out_id || out_len == 0) {
        return false;
    }

    if (strncmp(req->uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *id_start = req->uri + prefix_len;
    if (*id_start == '\0' || strlen(id_start) >= out_len || strlen(id_start) >= ALARM_ID_LEN) {
        return false;
    }

    strncpy(out_id, id_start, out_len - 1);
    out_id[out_len - 1] = '\0';
    return true;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    if (s_connected && s_web_fs_ready) {
        if (request_accepts_gzip(req) && file_exists(WEB_BASE_PATH "/index.html.gz")) {
            return send_file_response(req, WEB_BASE_PATH "/index.html.gz");
        }
        return send_file_response(req, WEB_BASE_PATH "/index.html");
    }

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

static esp_err_t static_get_handler(httpd_req_t *req) {
    if (!s_connected || !s_web_fs_ready) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }

    const char *uri = req->uri;
    if (strncmp(uri, "/api/", 5) == 0 || strcmp(uri, "/connect") == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }
    if (strstr(uri, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    char clean_uri[256] = {0};
    size_t i = 0;
    for (; uri[i] != '\0' && uri[i] != '?' && i < sizeof(clean_uri) - 1; i++) {
        clean_uri[i] = uri[i];
    }
    clean_uri[i] = '\0';
    if (clean_uri[0] == '\0') {
        strncpy(clean_uri, "/", sizeof(clean_uri) - 1);
    }

    char full_path[384] = {0};
    if (strcmp(clean_uri, "/") == 0) {
        strncpy(full_path, WEB_BASE_PATH "/index.html", sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), WEB_BASE_PATH "%s", clean_uri);
    }

    bool accepts_gzip = request_accepts_gzip(req);
    if (!path_ends_with(full_path, ".gz")) {
        char gzip_path[sizeof(full_path) + 4] = {0};
        snprintf(gzip_path, sizeof(gzip_path), "%s.gz", full_path);
        if (file_exists(gzip_path) && accepts_gzip) {
            return send_file_response(req, gzip_path);
        }
    }

    if (file_exists(full_path)) {
        return send_file_response(req, full_path);
    }

    if (!file_has_extension(clean_uri)) {
        if (accepts_gzip && file_exists(WEB_BASE_PATH "/index.html.gz")) {
            return send_file_response(req, WEB_BASE_PATH "/index.html.gz");
        }
        return send_file_response(req, WEB_BASE_PATH "/index.html");
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    char ssid[32] = {0};
    char pass[64] = {0};
    sscanf(buf, "ssid=%31[^&]&pass=%63s", ssid, pass);
    for (char *p = ssid; *p; p++) {
        if (*p == '+') {
            *p = ' ';
        }
    }
    for (char *p = pass; *p; p++) {
        if (*p == '+') {
            *p = ' ';
        }
    }

    wifi_manager_set_credentials(ssid, pass);
    httpd_resp_sendstr(req, "Credentials saved. Rebooting...");
    esp_restart();
    return ESP_OK;
}

static esp_err_t alarms_get_handler(httpd_req_t *req) {
    alarm_t alarms[ALARM_MANAGER_MAX_ALARMS] = {0};
    size_t count = alarm_manager_list_alarms(alarms, ALARM_MANAGER_MAX_ALARMS);

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; i++) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(arr);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
            return ESP_FAIL;
        }
        alarm_to_json_obj(&alarms[i], obj);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t alarms_post_handler(httpd_req_t *req) {
    char *body = NULL;
    if (!read_req_body(req, &body)) {
        send_json_error(req, "400 Bad Request", "Invalid or missing request body");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        send_json_error(req, "400 Bad Request", "Body must be valid JSON");
        return ESP_FAIL;
    }

    alarm_t alarm;
    if (!parse_alarm_json(obj, &alarm, NULL, true)) {
        cJSON_Delete(obj);
        send_json_error(req, "400 Bad Request", "Invalid alarm payload");
        return ESP_FAIL;
    }

    cJSON_Delete(obj);

    if (alarm.id[0] == '\0') {
        if (!generate_unique_alarm_id(alarm.id, sizeof(alarm.id))) {
            send_json_error(req, "500 Internal Server Error", "Unable to generate alarm id");
            return ESP_FAIL;
        }
    }

    bool existed = false;
    alarm_t existing;
    if (alarm_manager_get_alarm(alarm.id, &existing)) {
        existed = true;
    }

    if (!alarm_manager_upsert_alarm(&alarm)) {
        send_json_error(req, "400 Bad Request", "Unable to save alarm (capacity or data issue)");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, existed ? "200 OK" : "201 Created");
    httpd_resp_set_type(req, "application/json");

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    alarm_to_json_obj(&alarm, resp);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t alarms_put_handler(httpd_req_t *req) {
    char id[ALARM_ID_LEN] = {0};
    if (!get_alarm_id_from_uri(req, id, sizeof(id))) {
        send_json_error(req, "400 Bad Request", "Invalid alarm id in URI");
        return ESP_FAIL;
    }

    char *body = NULL;
    if (!read_req_body(req, &body)) {
        send_json_error(req, "400 Bad Request", "Invalid or missing request body");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        send_json_error(req, "400 Bad Request", "Body must be valid JSON");
        return ESP_FAIL;
    }

    alarm_t alarm;
    if (!parse_alarm_json(obj, &alarm, id, false)) {
        cJSON_Delete(obj);
        send_json_error(req, "400 Bad Request", "Invalid alarm payload");
        return ESP_FAIL;
    }
    cJSON_Delete(obj);

    alarm_t existing;
    if (!alarm_manager_get_alarm(id, &existing)) {
        send_json_error(req, "404 Not Found", "Alarm not found");
        return ESP_FAIL;
    }

    if (!alarm_manager_upsert_alarm(&alarm)) {
        send_json_error(req, "400 Bad Request", "Unable to save alarm");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    alarm_to_json_obj(&alarm, resp);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t alarms_delete_handler(httpd_req_t *req) {
    char id[ALARM_ID_LEN] = {0};
    if (!get_alarm_id_from_uri(req, id, sizeof(id))) {
        send_json_error(req, "400 Bad Request", "Invalid alarm id in URI");
        return ESP_FAIL;
    }

    if (!alarm_manager_delete_alarm(id)) {
        send_json_error(req, "404 Not Found", "Alarm not found");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t vacation_mode_get_handler(httpd_req_t *req) {
    vacation_mode_t mode;
    if (!alarm_manager_get_vacation_mode(&mode)) {
        send_json_error(req, "500 Internal Server Error", "Unable to read vacation mode");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    vacation_mode_to_json_obj(&mode, obj);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t vacation_mode_put_handler(httpd_req_t *req) {
    char *body = NULL;
    if (!read_req_body(req, &body)) {
        send_json_error(req, "400 Bad Request", "Invalid or missing request body");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        send_json_error(req, "400 Bad Request", "Body must be valid JSON");
        return ESP_FAIL;
    }

    vacation_mode_t mode;
    if (!parse_vacation_mode_json(obj, &mode)) {
        cJSON_Delete(obj);
        send_json_error(req, "400 Bad Request", "Invalid vacation mode payload");
        return ESP_FAIL;
    }
    cJSON_Delete(obj);

    if (!alarm_manager_set_vacation_mode(&mode)) {
        send_json_error(req, "400 Bad Request", "Unable to save vacation mode");
        return ESP_FAIL;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    vacation_mode_to_json_obj(&mode, resp);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static httpd_uri_t uri_connect = {
    .uri = "/connect",
    .method = HTTP_POST,
    .handler = connect_post_handler,
};

static httpd_uri_t uri_alarms_get = {
    .uri = "/api/alarms",
    .method = HTTP_GET,
    .handler = alarms_get_handler,
};

static httpd_uri_t uri_alarms_post = {
    .uri = "/api/alarms",
    .method = HTTP_POST,
    .handler = alarms_post_handler,
};

static httpd_uri_t uri_alarms_put = {
    .uri = "/api/alarms/*",
    .method = HTTP_PUT,
    .handler = alarms_put_handler,
};

static httpd_uri_t uri_alarms_delete = {
    .uri = "/api/alarms/*",
    .method = HTTP_DELETE,
    .handler = alarms_delete_handler,
};

static httpd_uri_t uri_vacation_mode_get = {
    .uri = "/api/vacation-mode",
    .method = HTTP_GET,
    .handler = vacation_mode_get_handler,
};

static httpd_uri_t uri_vacation_mode_put = {
    .uri = "/api/vacation-mode",
    .method = HTTP_PUT,
    .handler = vacation_mode_put_handler,
};

static httpd_uri_t uri_static_get = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = static_get_handler,
};

static bool register_uri_checked(const httpd_uri_t *uri) {
    esp_err_t err = httpd_register_uri_handler(s_httpd, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI handler %s: %s", uri->uri, esp_err_to_name(err));
        return false;
    }
    return true;
}

static void ensure_http_server_started(void) {
    if (s_httpd) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 12;

    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    register_uri_checked(&uri_root);
    register_uri_checked(&uri_connect);
    register_uri_checked(&uri_alarms_get);
    register_uri_checked(&uri_alarms_post);
    register_uri_checked(&uri_alarms_put);
    register_uri_checked(&uri_alarms_delete);
    register_uri_checked(&uri_vacation_mode_get);
    register_uri_checked(&uri_vacation_mode_put);
    register_uri_checked(&uri_static_get);
}

static void ensure_mdns_started(void) {
    if (s_mdns_ready) {
        return;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set(MDNS_HOSTNAME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS hostname set failed: %s", esp_err_to_name(err));
        return;
    }

    mdns_instance_name_set("ColorAlarm");
    mdns_service_add("ColorAlarm", "_http", "_tcp", 80, NULL, 0);

    s_mdns_ready = true;
    ESP_LOGI(TAG, "mDNS ready: http://%s.local", MDNS_HOSTNAME);
}

static void start_captive_portal(void) {
    ESP_LOGI(TAG, "Starting AP + Captive Portal");
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Config",
            .ssid_len = 12,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_netif_create_default_wifi_ap();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    if (s_callback) {
        s_callback(WIFI_EVENT_AP_STARTED, s_user_data);
    }

    ensure_http_server_started();
    ensure_mdns_started();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;

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
        if (s_callback) {
            s_callback(WIFI_EVENT_DISCONNECTED, s_user_data);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_retry_count = 0;
        ensure_http_server_started();
        ensure_mdns_started();
        if (s_callback) {
            s_callback(WIFI_EVENT_GOT_IP, s_user_data);
        }
    }
}

void wifi_manager_init(wifi_manager_cb_t cb, void *user_data) {
    s_callback = cb;
    s_user_data = user_data;

    web_fs_init();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_credentials_t creds = {0};
    wifi_config_t wifi_config = {0};

    if (wifi_manager_load_credentials(&creds)) {
        strncpy((char *)wifi_config.sta.ssid, creds.ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, creds.pass, sizeof(wifi_config.sta.password));
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
