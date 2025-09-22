
#pragma once
#include <stddef.h>
#include <stdbool.h>
bool storage_manager_init(void);
bool storage_manager_set_blob(const char *key, const void *data, size_t len);
bool storage_manager_get_blob(const char *key, void *out_data, size_t len, size_t *out_len);
