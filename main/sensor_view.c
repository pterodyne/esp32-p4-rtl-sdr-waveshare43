#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rpi_lcd.h"
#include "rpi_canvas.h"
#include "pins.h"
#include "view.h"
#include "sensor_view.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define BIG_ALLOC(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM)
#else
#define BIG_ALLOC(n) calloc(1, (n))
#endif

#define BAR_H (LCD_PORTRAIT ? 96 : 50)
#define KEY_Y (LCD_PORTRAIT ? 46 : 4)
static uint16_t *s_px;
static sensor_view_info_t s_last;
static bool s_hot, s_band_hot;
static const uint32_t bands[SENSOR_BANDS] = {433920000, 315000000, 345000000, 868300000, 915000000};
uint32_t sensor_view_band_hz(int band) { return bands[band >= 0 && band < SENSOR_BANDS ? band : 0]; }
static bool band_hit(int x, int y)
{ return x >= LCD_W - 234 && x < LCD_W - 118 && y >= KEY_Y - 4 && y < KEY_Y + 44; }
static bool s_drawn, s_draw_hot, s_draw_band_hot;
static int s_draw_w, s_draw_h, s_draw_theme;
static view_colors_t K;

static bool exit_hit(int x, int y)
{
    return x >= LCD_W - 114 && x < LCD_W - 4 && y >= KEY_Y - 4 && y < KEY_Y + 44;
}

void sensor_view_init(void)
{
    s_px = BIG_ALLOC(LCD_W * LCD_H * sizeof *s_px);
    if (!s_px) abort();
}

void sensor_view_open(void) { s_hot = false; s_band_hot = false; s_drawn = false; }

static void exit_key(const rpi_canvas_t *c)
{
    int x = LCD_W - 112, y = KEY_Y;
    rpi_cv_rrect(c, x, y, 106, 40, K.square ? 0 : 8, s_hot ? K.btn_on : K.edge);
    rpi_cv_rrect(c, x + 1, y + 1, 104, 38, K.square ? 0 : 7, K.btn);
    rpi_cv_text(c, x + (106 - rpi_cv_text_w("EXIT", 2)) / 2, y + 4, "EXIT", K.key_txt, 2);
}

static void clipped_text(const rpi_canvas_t *c, int x, int y, const char *src, uint16_t color)
{
    char text[SENSOR_VIEW_LINE];
    snprintf(text, sizeof text, "%s", src);
    while (text[0] && x + rpi_cv_text_w(text, 1) > LCD_W - 6) text[strlen(text) - 1] = 0;
    rpi_cv_text(c, x, y, text, color, 1);
}

void sensor_view_draw(const sensor_view_info_t *info)
{
    if (!info || !s_px) return;
    bool full = !s_drawn || s_draw_w != LCD_W || s_draw_h != LCD_H || s_draw_theme != view_theme();
    bool status_changed = full || s_band_hot != s_draw_band_hot || info->band != s_last.band || s_hot != s_draw_hot || info->wh2 != s_last.wh2 ||
                          info->tpms != s_last.tpms || info->dropped_bytes != s_last.dropped_bytes ||
                          info->streaming != s_last.streaming || info->load_percent != s_last.load_percent;
    bool events_changed = full || info->rows != s_last.rows ||
                          memcmp(info->event, s_last.event, sizeof info->event) != 0;
    if (!status_changed && !events_changed) return;
    int old_rows = s_last.rows;
    s_last = *info;
    view_colors(&K);
    rpi_canvas_t c = {s_px, LCD_W, LCD_H};
    rpi_cv_fill(&c, 0, 0, LCD_W, LCD_H, K.bg);
    rpi_cv_fill(&c, 0, 0, LCD_W, BAR_H, rpi_mix565(K.bg, K.btn, 90));
    rpi_cv_fill(&c, 0, BAR_H - 1, LCD_W, 1, K.edge);
    rpi_cv_text(&c, 8, 8, "SENSORS", K.accent, 2);
    char band_label[24];
    uint32_t hz = sensor_view_band_hz(info->band);
    snprintf(band_label, sizeof band_label, "%" PRIu32 ".%02" PRIu32, hz / 1000000, hz / 10000 % 100);
    rpi_cv_rrect(&c, LCD_W - 232, KEY_Y, 110, 40, 6, s_band_hot ? K.btn_on : K.btn);
    rpi_cv_text(&c, LCD_W - 226, KEY_Y + 3, "BAND MHz >", K.key_txt, 1);
    rpi_cv_text(&c, LCD_W - 226, KEY_Y + 21, band_label, K.accent, 1);
    if (LCD_PORTRAIT) clipped_text(&c, 8, 58, "WH2/TP-S15 only", K.dim);
    char status[68];
    snprintf(status, sizeof status, "WH2 %u  TPMS %u  drop %" PRIu64 " B", info->wh2, info->tpms, info->dropped_bytes);
    exit_key(&c);
    clipped_text(&c, 8, BAR_H + 6, status, K.dim);
    char load[48];
    snprintf(load, sizeof load, "%s decoder %d%% WH2/TP-S15 only", info->streaming ? "LISTENING" : "STREAM STOPPED", info->load_percent);
    clipped_text(&c, 8, BAR_H + 28, load,
                 info->streaming ? K.accent : K.dim);
    if (!info->rows)
        clipped_text(&c, 8, BAR_H + 60, "No WH2/Steelmate packets in last 60s", K.dim);
    int top = BAR_H + 60;
    int max = info->rows < SENSOR_VIEW_ROWS ? info->rows : SENSOR_VIEW_ROWS;
    for (int row = 0; row < max; row++) {
        int y = top + row * 36;
        if (y + 34 > LCD_H) break;
        if (row % 2) rpi_cv_fill(&c, 0, y, LCD_W, 34, rpi_mix565(K.bg, K.btn, 80));
        clipped_text(&c, 8, y + 8, info->event[row], row ? K.txt : K.accent);
    }
    /* The pixel buffer is row-major: full-width strips remain contiguous. A
     * 1 MiB IQ ring cannot tolerate a whole SPI frame every 250 ms. */
    if (full) rpi_lcd_blit(0, 0, LCD_W, LCD_H, s_px);
    else {
        if (status_changed) {
            int h = BAR_H + 46;
            rpi_lcd_blit(0, 0, LCD_W, h, s_px);
        }
        if (events_changed) {
            int y = BAR_H + 60;
            int rows = info->rows > old_rows ? info->rows : old_rows;
            int h = rows ? rows * 36 : 18;
            if (y + h > LCD_H) h = LCD_H - y;
            rpi_lcd_blit(0, y, LCD_W, h, s_px + y * LCD_W);
        }
    }
    s_drawn = true;
    s_draw_hot = s_hot;
    s_draw_band_hot = s_band_hot;
    s_draw_w = LCD_W;
    s_draw_h = LCD_H;
    s_draw_theme = view_theme();
}

int sensor_view_touch(int event, int x, int y)
{
    bool hit = exit_hit(x, y), bh = band_hit(x, y), old_band = s_band_hot;
    int fired = 0;
    bool old = s_hot;
    if (event == SENSOR_RELEASE) {
        fired = s_hot && hit ? SENSOR_EXIT : s_band_hot && bh ? SENSOR_BAND : 0;
        s_hot = s_band_hot = false;
    } else { s_hot = hit; s_band_hot = bh; }
    if ((old != s_hot || old_band != s_band_hot) && !fired) sensor_view_draw(&s_last);
    return fired;
}
