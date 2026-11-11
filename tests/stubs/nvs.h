#pragma once
#include <stdint.h>
typedef unsigned nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
