#include <assert.h>
#include <stdio.h>
#include "nvs.h"
#include "display_power.h"
static int calls[8], count, fail;
static esp_err_t panel(bool on) { calls[count++] = on ? 1 : 2; return fail == 1 ? ESP_FAIL : ESP_OK; }
static esp_err_t light(bool on) { calls[count++] = on ? 3 : 4; return fail == 2 ? ESP_FAIL : ESP_OK; }
int main(void) {
    assert(display_power_set(false, panel, light) == ESP_OK);
    assert(count == 2 && calls[0] == 4 && calls[1] == 2);
    count = 0;
    assert(display_power_set(true, panel, light) == ESP_OK);
    assert(count == 2 && calls[0] == 1 && calls[1] == 3);
    count = 0; fail = 1;
    assert(display_power_set(true, panel, light) == ESP_FAIL);
    assert(count == 1); /* never light panel whose wake failed */
    count = 0; fail = 2;
    assert(display_power_set(false, panel, light) == ESP_FAIL);
    assert(count == 1); /* report failure, caller can retry */
    puts("display power: backlight first on off; panel first on wake; failures propagated");
}
