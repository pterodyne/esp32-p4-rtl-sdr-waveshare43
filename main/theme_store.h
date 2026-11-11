#pragma once
#include <stdbool.h>
#include "nvs.h"

/* Commit only the selected look immediately so loss of power does not undo it. */
esp_err_t theme_store_save(nvs_handle_t handle, int selected);
/* A failed save is retried even if the user taps the already-displayed look. */
bool theme_store_should_save(int previous, int selected, bool save_failed);
