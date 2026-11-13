#pragma once
/* Persist the Waveshare spectrum TOUCH mode on selection, before power can be cut. */
#include <stdbool.h>
#include "nvs.h"
#include "view.h"

static inline bool touch_mode_valid(int mode)
{
    return mode >= VIEW_TOUCH_FREE && mode <= VIEW_TOUCH_PEAK;
}
static inline int touch_mode_loaded(int value)
{
    return touch_mode_valid(value) ? value : VIEW_TOUCH_FREE;
}
static inline esp_err_t touch_mode_store_save(nvs_handle_t handle, int mode)
{
    if (!touch_mode_valid(mode)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_i32(handle, "touchmode", mode);
    return err == ESP_OK ? nvs_commit(handle) : err;
}
static inline esp_err_t touch_mode_store_save_reopening(bool *ready, nvs_handle_t *handle, int mode)
{
    if (!touch_mode_valid(mode)) return ESP_ERR_INVALID_ARG;
    if (!*ready) {
        esp_err_t err = nvs_open("sdr", NVS_READWRITE, handle);
        if (err != ESP_OK) return err;
        *ready = true;
    }
    return touch_mode_store_save(*handle, mode);
}
