#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "theme_store.h"

static int stored, pending, writes, commits;
static int fail_set, fail_commit;

esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    assert(handle == 7);
    assert(strcmp(key, "theme") == 0);
    writes++;
    if (fail_set) return ESP_FAIL;
    pending = value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 7);
    commits++;
    if (fail_commit) return ESP_FAIL;
    stored = pending; /* only a committed value survives a simulated power loss */
    return ESP_OK;
}

int main(void)
{
    assert(theme_store_should_save(2, 3, false));
    assert(!theme_store_should_save(3, 3, false));
    assert(theme_store_should_save(3, 3, true)); /* same LOOK retries failed commit */
    assert(stored == 0);
    assert(theme_store_save(7, 3) == ESP_OK);
    assert(writes == 1 && commits == 1 && stored == 3);
    pending = 0; /* power loss: pending RAM discarded, committed NVS survives */
    assert(stored == 3);

    fail_set = 1;
    assert(theme_store_save(7, 4) != ESP_OK);
    assert(writes == 2 && commits == 1 && stored == 3);
    fail_set = 0;
    fail_commit = 1;
    assert(theme_store_save(7, 4) != ESP_OK);
    assert(writes == 3 && commits == 2 && stored == 3);
    fail_commit = 0;
    assert(theme_store_save(7, 4) == ESP_OK && stored == 4);
    puts("look selection committed immediately; failures detected; saved look survives reboot");
    return 0;
}
