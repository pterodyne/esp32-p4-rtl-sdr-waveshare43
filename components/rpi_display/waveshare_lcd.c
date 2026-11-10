/* ST7701 MIPI DPI adapter for the Waveshare 4.3in board. Keep the existing
 * RGB565 drawing API; portrait is native, landscape rotates dirty rectangles
 * in software because the DPI framebuffer has fixed 480x800 timing. */
#include <stdint.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4_3.h"
#include "rpi_lcd.h"
#include "pins.h"
#include "waveshare_geometry.h"

static const char *TAG = "waveshare_lcd";
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_buf;
static SemaphoreHandle_t s_copy_done;
int rpi_lcd_w = 480, rpi_lcd_h = 800, rpi_lcd_rotation = 0;

static bool on_copy_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *event, void *ctx)
{
    (void)panel; (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &wake);
    return wake == pdTRUE;
}

static esp_err_t draw_and_wait(int x0, int y0, int x1, int y1)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, s_buf);
    if (err != ESP_OK) return err;
    /* The BSP enables async DMA2D copies: do not reuse the scratch buffer
     * until the panel has finished reading it. */
    return xSemaphoreTake(s_copy_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t rpi_lcd_init(void)
{
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(bsp_display_new(NULL, &s_panel, &io), TAG, "MIPI display");
    s_buf = heap_caps_aligned_alloc(128, 480 * 800 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_buf, ESP_ERR_NO_MEM, TAG, "rotation buffer");
    s_copy_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_copy_done, ESP_ERR_NO_MEM, TAG, "DMA completion semaphore");
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_color_trans_done = on_copy_done};
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks, s_copy_done), TAG, "DMA callback");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
    return bsp_display_backlight_on();
}

esp_err_t rpi_lcd_set_rotation(int quarter_turns)
{
    if (quarter_turns < 0 || quarter_turns > 3) return ESP_ERR_INVALID_ARG;
    rpi_lcd_rotation = quarter_turns;
    rpi_lcd_w = quarter_turns & 1 ? 800 : 480;
    rpi_lcd_h = quarter_turns & 1 ? 480 : 800;
    return ESP_OK;
}

esp_err_t rpi_lcd_set_portrait(bool on)
{
    return rpi_lcd_set_rotation(waveshare_legacy_rotation(on));
}

esp_err_t rpi_lcd_blit(int x, int y, int w, int h, const uint16_t *pix)
{
    if (!s_panel || !s_buf) return ESP_ERR_INVALID_STATE;
    if (!pix || w < 0 || h < 0 || x < 0 || y < 0 || x + w > LCD_W || y + h > LCD_H)
        return ESP_ERR_INVALID_ARG;
    if (!w || !h) return ESP_OK;
    int rotation = LCD_ROTATION;
    int px0, py0, px1, py1;
    waveshare_screen_to_panel(rotation, x, y, &px0, &py0);
    waveshare_screen_to_panel(rotation, x + w - 1, y + h - 1, &px1, &py1);
    if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
    if (py0 > py1) { int t = py0; py0 = py1; py1 = t; }
    int pw = px1 - px0 + 1, ph = py1 - py0 + 1;
    for (int row = 0; row < ph; row++) {
        for (int col = 0; col < pw; col++) {
            int sx, sy;
            waveshare_panel_to_screen(rotation, px0 + col, py0 + row, &sx, &sy);
            s_buf[row * pw + col] = pix[(sy - y) * w + (sx - x)];
        }
    }
    return draw_and_wait(px0, py0, px1 + 1, py1 + 1);
}

esp_err_t rpi_lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return ESP_OK;
    int rotation = LCD_ROTATION;
    int px0, py0, px1, py1;
    waveshare_screen_to_panel(rotation, x, y, &px0, &py0);
    waveshare_screen_to_panel(rotation, x + w - 1, y + h - 1, &px1, &py1);
    if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
    if (py0 > py1) { int t = py0; py0 = py1; py1 = t; }
    for (int i = 0; i < w * h; i++) s_buf[i] = color;
    return draw_and_wait(px0, py0, px1 + 1, py1 + 1);
}

/* The 16-bit SPI command stream is specific to the old ILI9486 panel. */
esp_err_t rpi_lcd_set_window(int x0, int y0, int x1, int y1)
{
    (void)x0; (void)y0; (void)x1; (void)y1;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t rpi_lcd_push_pixels_be(const uint8_t *bytes, size_t n)
{
    (void)bytes; (void)n;
    return ESP_ERR_NOT_SUPPORTED;
}
spi_host_device_t rpi_lcd_spi_host(void) { return SPI2_HOST; }
