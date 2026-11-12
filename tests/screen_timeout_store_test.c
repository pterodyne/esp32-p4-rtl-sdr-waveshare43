#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "screen_timeout_store.h"
static int stored, pending, commits, fail_set, fail_commit, fail_open, opens;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, "sdr") && mode == NVS_READWRITE);
    opens++;
    if (fail_open) return ESP_FAIL;
    *h = 7;
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t v) {
    assert(h == 7 && !strcmp(key, "screen_sec"));
    if (fail_set) return ESP_FAIL;
    pending = v; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 7); commits++;
    if (fail_commit) return ESP_FAIL;
    stored = pending; return ESP_OK;
}
int main(void) {
    assert(screen_timeout_save(7, 30) == ESP_OK);
    assert(stored == 30 && commits == 1);
    pending = 0; assert(stored == 30); /* simulated power loss */
    fail_set = 1;
    assert(screen_timeout_save(7, 60) == ESP_FAIL && commits == 1);
    fail_set = 0; fail_commit = 1;
    assert(screen_timeout_save(7, 90) == ESP_FAIL && stored == 30);
    fail_commit = 0;
    assert(screen_timeout_save(7, 90) == ESP_OK && stored == 90);
    assert(screen_timeout_save(7, 0) == ESP_OK && stored == 0);
    assert(screen_timeout_save(7, 45) == ESP_ERR_INVALID_ARG && stored == 0);
    nvs_handle_t h = 0;
    bool ready = false;
    fail_open = 1;
    assert(screen_timeout_save_reopening(&ready, &h, 30) == ESP_FAIL);
    assert(!ready && h == 0 && stored == 0);
    fail_open = 0;
    assert(screen_timeout_save_reopening(&ready, &h, 30) == ESP_OK);
    assert(ready && h == 7 && stored == 30 && opens == 2);
    assert(screen_timeout_save_reopening(&ready, &h, 60) == ESP_OK && opens == 2);
    puts("screen timeout: immediate commit, never, invalid value and NVS set/commit failures");
}
