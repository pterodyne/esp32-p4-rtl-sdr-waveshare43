#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_settings_store.h"

typedef struct { uint8_t bytes[4]; bool present; } cell_t;
static cell_t durable, pending;
static int32_t legacy_vol, legacy_sql;
static bool legacy_vol_present, legacy_sql_present;
static bool fail_set, fail_get, fail_commit;
static int writes, commits, unrelated_sentinel = 1234;

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *src, size_t len)
{
    assert(h == 7 && strcmp(key, "audctl") == 0 && len == 4);
    writes++;
    if (fail_set) return ESP_FAIL;
    memcpy(pending.bytes, src, len);
    pending.present = true;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *dst, size_t *len)
{
    assert(h == 7 && strcmp(key, "audctl") == 0);
    if (fail_get) return ESP_FAIL;
    if (!durable.present) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < 4) return ESP_ERR_INVALID_ARG;
    memcpy(dst, durable.bytes, 4); *len = 4;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *val)
{
    assert(h == 7);
    if (!strcmp(key, "vol") && legacy_vol_present) { *val = legacy_vol; return ESP_OK; }
    if (!strcmp(key, "sql") && legacy_sql_present) { *val = legacy_sql; return ESP_OK; }
    assert(!strcmp(key, "vol") || !strcmp(key, "sql"));
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == 7);
    commits++;
    if (fail_commit) return ESP_FAIL;
    durable = pending;
    return ESP_OK;
}
static void power_off(void) { pending = durable; }

int main(void)
{
    audio_settings_t out;
    assert(audio_settings_store_load(7, &out) == ESP_OK);
    assert(out.volume == 4 && out.squelch == 0 && !out.muted);
    legacy_vol_present = legacy_sql_present = true;
    legacy_vol = 6; legacy_sql = 2;
    assert(audio_settings_store_load(7, &out) == ESP_OK);
    assert(out.volume == 6 && out.squelch == 2 && !out.muted); /* old NVS format */

    audio_settings_t chosen = {.volume = 7, .squelch = 5, .muted = true};
    assert(audio_settings_store_save(7, &chosen) == ESP_OK);
    assert(writes == 1 && commits == 1 && unrelated_sentinel == 1234);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK);
    assert(out.volume == 7 && out.squelch == 5 && out.muted); /* blob overrides legacy */

    chosen = (audio_settings_t){.volume = 0, .squelch = 0, .muted = false};
    assert(audio_settings_store_save(7, &chosen) == ESP_OK);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK);
    assert(out.volume == 0 && !out.muted); /* zero volume is not a mute toggle */

    chosen = (audio_settings_t){.volume = 8, .squelch = 4, .muted = true};
    fail_set = true;
    int before = commits;
    assert(audio_settings_store_save(7, &chosen) == ESP_FAIL && commits == before);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK && out.volume == 0 && !out.muted);
    fail_set = false;
    assert(audio_settings_store_save(7, &chosen) == ESP_OK);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK && out.volume == 8 && out.muted);

    chosen.volume = 3;
    fail_commit = true;
    assert(audio_settings_store_save(7, &chosen) == ESP_FAIL);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK && out.volume == 8);
    fail_commit = false;
    assert(audio_settings_store_save(7, &chosen) == ESP_OK);
    power_off();
    assert(audio_settings_store_load(7, &out) == ESP_OK && out.volume == 3);

    durable.bytes[1] = 100; durable.bytes[2] = 50; durable.bytes[3] = 3;
    assert(audio_settings_store_load(7, &out) == ESP_OK);
    assert(out.volume == 10 && out.squelch == 9 && out.muted);
    fail_get = true;
    assert(audio_settings_store_load(7, &out) == ESP_FAIL && out.muted); /* fail safe */
    fail_get = false;
    chosen.volume = -1;
    assert(audio_settings_store_save(7, &chosen) == ESP_ERR_INVALID_ARG);
    durable.bytes[0] = 99; /* unknown format: do not produce audible output */
    assert(audio_settings_store_load(7, &out) == ESP_ERR_INVALID_ARG && out.muted);
    puts("audio controls commit promptly; failed writes retry; legacy NVS and mute/zero volume distinct");
    return 0;
}
