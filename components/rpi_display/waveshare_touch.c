/* Compatibility input for the existing UI, backed by GT911 capacitive touch.
 * Raw values are native panel pixels; no resistive calibration is needed. */
#include "esp_lcd_touch.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4_3.h"
#include "bsp/touch.h"
#include "xpt2046.h"
#include "touch_cal.h"
#include "pins.h"
#include "waveshare_geometry.h"

static esp_lcd_touch_handle_t s_touch;

esp_err_t xpt2046_init(void)
{
    bsp_display_cfg_t cfg = {0};
    return bsp_touch_new(&cfg, &s_touch);
}

bool xpt2046_read_raw(uint16_t *x, uint16_t *y, uint16_t *z)
{
    uint8_t count = 0;
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK) return false;
    if (!esp_lcd_touch_get_coordinates(s_touch, x, y, z, &count, 1)) return false;
    return count != 0;
}

bool xpt2046_pressed(void)
{
    uint16_t x, y, z;
    return xpt2046_read_raw(&x, &y, &z);
}

esp_err_t touch_cal_load(touch_cal_t *c, float *error)
{
    *c = (touch_cal_t){.a = 1, .e = 1};
    if (error) *error = 0;
    return ESP_OK;
}
esp_err_t touch_cal_save(const touch_cal_t *c, float error)
{
    (void)c; (void)error;
    return ESP_OK;
}
esp_err_t touch_cal_run(touch_cal_t *c, float *error)
{
    return touch_cal_load(c, error);
}
void touch_cal_map(const touch_cal_t *c, uint16_t raw_x, uint16_t raw_y, int *x, int *y)
{
    (void)c;
    waveshare_panel_to_screen(LCD_ROTATION, raw_x, raw_y, x, y);
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= LCD_W) *x = LCD_W - 1;
    if (*y >= LCD_H) *y = LCD_H - 1;
}
