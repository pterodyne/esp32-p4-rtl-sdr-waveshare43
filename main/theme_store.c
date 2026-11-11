#include "theme_store.h"

bool theme_store_should_save(int previous, int selected, bool save_failed)
{
    return previous != selected || save_failed;
}

esp_err_t theme_store_save(nvs_handle_t handle, int selected)
{
    esp_err_t err = nvs_set_i32(handle, "theme", selected);
    if (err != ESP_OK) return err;
    return nvs_commit(handle);
}
