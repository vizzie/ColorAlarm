#include "storage_manager.h"

#include "nvs.h"
#include "nvs_flash.h"

#define STORAGE_PARTITION_NAME "nvs_storage"
#define STORAGE_NAMESPACE "storage"

static bool get_blob_from_partition(const char *partition_name,
                                    const char *key,
                                    void *out_data,
                                    size_t len,
                                    size_t *out_len) {
    nvs_handle_t handle;
    if (nvs_open_from_partition(partition_name, STORAGE_NAMESPACE,
                                NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t required = len;
    esp_err_t err = nvs_get_blob(handle, key, out_data, &required);
    if (out_len) {
        *out_len = required;
    }

    nvs_close(handle);
    return err == ESP_OK;
}

static bool init_partition(const char *partition_name) {
    esp_err_t ret = nvs_flash_init_partition(partition_name);

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase_partition(partition_name) != ESP_OK) {
            return false;
        }
        ret = nvs_flash_init_partition(partition_name);
    }

    return ret == ESP_OK;
}

bool storage_manager_init(void) {
    if (!init_partition("nvs")) {
        return false;
    }

    return init_partition(STORAGE_PARTITION_NAME);
}

bool storage_manager_set_blob(const char *key, const void *data, size_t len) {
    nvs_handle_t handle;
    if (nvs_open_from_partition(STORAGE_PARTITION_NAME, STORAGE_NAMESPACE,
                                NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_manager_get_blob(const char *key, void *out_data, size_t len, size_t *out_len) {
    size_t legacy_len = 0;

    if (get_blob_from_partition(STORAGE_PARTITION_NAME, key, out_data, len, out_len)) {
        return true;
    }

    if (!get_blob_from_partition("nvs", key, out_data, len, &legacy_len)) {
        return false;
    }

    if (out_len) {
        *out_len = legacy_len;
    }

    // Migrate legacy values from default nvs partition to dedicated storage.
    storage_manager_set_blob(key, out_data, legacy_len);
    return true;
}
