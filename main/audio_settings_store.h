#pragma once
#include <stdbool.h>
#include "nvs.h"

typedef struct {
    int volume;   /* 0..10 */
    int squelch;  /* 0..9 */
    bool muted;   /* independent of zero volume */
} audio_settings_t;

/* A single versioned NVS blob commits the three controls together. On first
 * boot after upgrade, legacy vol/sql keys are read if the blob is absent. */
esp_err_t audio_settings_store_load(nvs_handle_t handle, audio_settings_t *out);
esp_err_t audio_settings_store_save(nvs_handle_t handle, const audio_settings_t *in);
