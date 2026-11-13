#pragma once
#include "nvs.h"
#include "screen_idle.h"
static inline esp_err_t screen_timeout_save(nvs_handle_t handle, int seconds)
{
    if (!screen_timeout_valid(seconds)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_i32(handle, "screen_sec", seconds);
    return err == ESP_OK ? nvs_commit(handle) : err;
}
static inline esp_err_t screen_timeout_save_reopening(bool *ready, nvs_handle_t *handle, int seconds)
{
    if (!screen_timeout_valid(seconds)) return ESP_ERR_INVALID_ARG;
    if (!*ready) {
        esp_err_t err = nvs_open("sdr", NVS_READWRITE, handle);
        if (err != ESP_OK) return err;
        *ready = true;
    }
    return screen_timeout_save(*handle, seconds);
}
