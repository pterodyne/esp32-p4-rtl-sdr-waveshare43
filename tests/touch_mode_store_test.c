#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "touch_mode_store.h"

static int committed = -1, staged = -1, writes, commits, opens;
static int fail_open, fail_set, fail_commit;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    assert(!strcmp(ns, "sdr") && mode == NVS_READWRITE);
    opens++;
    if (fail_open) return ESP_FAIL;
    *h = 7;
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t mode)
{
    assert(h == 7 && !strcmp(key, "touchmode"));
    writes++;
    if (fail_set) return ESP_FAIL;
    staged = mode;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == 7);
    commits++;
    if (fail_commit) return ESP_FAIL;
    committed = staged;
    return ESP_OK;
}
int main(void)
{
    assert(touch_mode_valid(0) && touch_mode_valid(1) && touch_mode_valid(2));
    assert(!touch_mode_valid(-1) && !touch_mode_valid(3));
    assert(touch_mode_loaded(-8) == VIEW_TOUCH_FREE);
    assert(touch_mode_loaded(99) == VIEW_TOUCH_FREE);
    assert(touch_mode_loaded(VIEW_TOUCH_PEAK) == VIEW_TOUCH_PEAK);
    assert(touch_mode_store_save(7, VIEW_TOUCH_PEAK) == ESP_OK);
    assert(committed == VIEW_TOUCH_PEAK && writes == 1 && commits == 1);
    staged = VIEW_TOUCH_FREE; /* simulates immediate loss of uncommitted RAM */
    assert(committed == VIEW_TOUCH_PEAK);
    fail_set = 1;
    assert(touch_mode_store_save(7, VIEW_TOUCH_SNAP) == ESP_FAIL);
    assert(committed == VIEW_TOUCH_PEAK && commits == 1);
    fail_set = 0; fail_commit = 1;
    assert(touch_mode_store_save(7, VIEW_TOUCH_SNAP) == ESP_FAIL);
    assert(committed == VIEW_TOUCH_PEAK);
    fail_commit = 0;
    assert(touch_mode_store_save(7, VIEW_TOUCH_SNAP) == ESP_OK);
    assert(committed == VIEW_TOUCH_SNAP);
    assert(touch_mode_store_save(7, 3) == ESP_ERR_INVALID_ARG);
    assert(committed == VIEW_TOUCH_SNAP);
    nvs_handle_t h = 0;
    bool ready = false;
    fail_open = 1;
    assert(touch_mode_store_save_reopening(&ready, &h, VIEW_TOUCH_FREE) == ESP_FAIL);
    assert(!ready && h == 0 && committed == VIEW_TOUCH_SNAP);
    fail_open = 0;
    assert(touch_mode_store_save_reopening(&ready, &h, VIEW_TOUCH_FREE) == ESP_OK);
    assert(ready && h == 7 && committed == VIEW_TOUCH_FREE && opens == 2);
    puts("touch mode: validated, committed immediately, retries open/set/commit failures");
}
