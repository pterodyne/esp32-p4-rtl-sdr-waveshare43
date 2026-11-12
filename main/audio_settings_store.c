#include <stdint.h>
#include "audio_settings_store.h"

#define AUDIO_STORE_KEY "audctl"
#define AUDIO_STORE_VERSION 1

static int clamp(int value, int max)
{
    return value < 0 ? 0 : value > max ? max : value;
}

esp_err_t audio_settings_store_load(nvs_handle_t handle, audio_settings_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (audio_settings_t){.volume = 4, .squelch = 0, .muted = false};
    uint8_t data[4];
    size_t len = sizeof data;
    esp_err_t err = nvs_get_blob(handle, AUDIO_STORE_KEY, data, &len);
    if (err == ESP_OK) {
        if (len != sizeof data || data[0] != AUDIO_STORE_VERSION) {
            out->muted = true; /* corrupted/newer format: fail silent */
            return ESP_ERR_INVALID_ARG;
        }
        out->volume = clamp(data[1], 10);
        out->squelch = clamp(data[2], 9);
        out->muted = data[3] != 0;
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        out->muted = true;
        return err;
    }
    /* Preserve the earlier firmware's saved volume and squelch if present. */
    int32_t legacy = 0;
    err = nvs_get_i32(handle, "vol", &legacy);
    if (err == ESP_OK) out->volume = clamp(legacy, 10);
    else if (err != ESP_ERR_NVS_NOT_FOUND) { out->muted = true; return err; }
    err = nvs_get_i32(handle, "sql", &legacy);
    if (err == ESP_OK) out->squelch = clamp(legacy, 9);
    else if (err != ESP_ERR_NVS_NOT_FOUND) { out->muted = true; return err; }
    return ESP_OK;
}

esp_err_t audio_settings_store_save(nvs_handle_t handle, const audio_settings_t *in)
{
    if (!in || in->volume < 0 || in->volume > 10 || in->squelch < 0 || in->squelch > 9)
        return ESP_ERR_INVALID_ARG;
    const uint8_t data[4] = {AUDIO_STORE_VERSION, (uint8_t)in->volume,
                             (uint8_t)in->squelch, in->muted ? 1 : 0};
    esp_err_t err = nvs_set_blob(handle, AUDIO_STORE_KEY, data, sizeof data);
    if (err != ESP_OK) return err;
    return nvs_commit(handle);
}
