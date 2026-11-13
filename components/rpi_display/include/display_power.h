#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef esp_err_t (*display_switch_t)(bool on);
static inline esp_err_t display_power_set(bool on, display_switch_t panel, display_switch_t backlight)
{
    esp_err_t err = on ? panel(true) : backlight(false);
    if (err != ESP_OK) return err;
    return on ? backlight(true) : panel(false);
}
