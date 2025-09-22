
#include "storage_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

bool storage_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    return ret == ESP_OK;
}

bool storage_manager_set_blob(const char *key, const void *data, size_t len) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_manager_get_blob(const char *key, void *out_data, size_t len, size_t *out_len) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t required = len;
    esp_err_t err = nvs_get_blob(handle, key, out_data, &required);
    if (out_len) *out_len = required;
    nvs_close(handle);
    return err == ESP_OK;
}
