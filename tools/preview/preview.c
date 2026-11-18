/* Render the receiver screen on a PC: synthetic IQ (a few FM stations plus noise) through the
 * real spectrum.c and view.c, into a 480x320 frame buffer written as a PNG.
 *
 * Build: make -C tools/preview */
#include <math.h>
static int preview_battery_mv = -1;
int battery_voltage_get(void) { return preview_battery_mv; }
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include "rpi_lcd.h"
#include "rpi_text.h"
#include "pins.h"
#include "spectrum.h"
#include "view.h"
#include "panel.h"
#include "adsb.h"
#include "adsb_view.h"
#include "amap.h"
#include "scan_view.h"
#include "sensor_view.h"
#include "screen_idle.h"
#include "waveshare_geometry.h"

#define RATE 2400000
#define NAVG 24
#define NFFT 1024

static uint16_t fb[LCD_LONG * LCD_SHORT];   /* LCD_W x LCD_H, either way round */
#define FB(x, y) fb[(y) * LCD_W + (x)]
static int s_blit_count, s_last_y, s_last_h;

int rpi_lcd_w = LCD_LONG, rpi_lcd_h = LCD_SHORT;
#ifdef CONFIG_SDR_WAVESHARE_43
int rpi_lcd_rotation;
#endif

esp_err_t rpi_lcd_set_portrait(bool on)
{
    rpi_lcd_w = on ? LCD_SHORT : LCD_LONG;
    rpi_lcd_h = on ? LCD_LONG : LCD_SHORT;
    return ESP_OK;
}

esp_err_t rpi_lcd_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < LCD_W && j >= 0 && j < LCD_H) FB(i, j) = c;
    return ESP_OK;
}

esp_err_t rpi_lcd_blit(int x, int y, int w, int h, const uint16_t *p)
{
    if (x < 0 || y < 0 || x + w > LCD_W || y + h > LCD_H) return ESP_ERR_INVALID_ARG;
    s_blit_count++;
    s_last_y = y; s_last_h = h;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) FB(x + i, y + j) = p[j * w + i];
    return ESP_OK;
}

/* ---- synthetic IQ ---- */
typedef struct { double off_hz, amp, dev_hz, bw_hz, phase, audio, fade; } station_t;   /* last 3: state */
static station_t st[] = {
    {-760e3, 0.30, 75e3, 6000}, {-560e3, 0.010, 60e3, 3000}, {-300e3, 0.05, 70e3, 9000},
    {0, 0.12, 70e3, 5000},      {430e3, 0.02, 50e3, 4000},   {880e3, 0.20, 75e3, 7000},
    {1050e3, 0.008, 3e3, 300},
};
#define NST (sizeof st / sizeof st[0])

static double gauss(void)
{
    double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
    return sqrt(-2 * log(u)) * cos(2 * M_PI * v);
}

/* FM stations whose "audio" is low-passed noise, plus a slow fade per frame. */
static void synth(uint8_t *iq, int n)
{
    for (int s = 0; s < (int)NST; s++) {
        station_t *p = &st[s];
        p->fade += 0.15 * (gauss() * 0.3 - p->fade);
    }
    for (int k = 0; k < n; k++) {
        double i = 0.012 * gauss(), q = 0.012 * gauss();
        for (int s = 0; s < (int)NST; s++) {
            station_t *p = &st[s];
            double a = 2 * M_PI * p->bw_hz / RATE;
            p->audio += a * (2.2 * gauss() - p->audio);
            double m = p->audio > 1 ? 1 : p->audio < -1 ? -1 : p->audio;
            p->phase += 2 * M_PI * (p->off_hz + p->dev_hz * m) / RATE;
            double amp = p->amp * (1 + p->fade);
            i += amp * cos(p->phase);
            q += amp * sin(p->phase);
        }
        int a = (int)lrint(127.4 + 128 * i), b = (int)lrint(127.4 + 128 * q);
        iq[2 * k] = a < 0 ? 0 : a > 255 ? 255 : a;
        iq[2 * k + 1] = b < 0 ? 0 : b > 255 ? 255 : b;
    }
}

/* ---- PNG ---- */
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    put32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t crc = crc32(crc32(0, (const uint8_t *)type, 4), d, n);
    put32(h, crc);
    fwrite(h, 1, 4, f);
}

static void write_png(const char *path, const uint16_t *px, int W, int H)
{
    size_t raw_n = (size_t)H * (1 + W * 3);
    uint8_t *raw = malloc(raw_n), *z = malloc(compressBound(raw_n));
    uint8_t *o = raw;
    for (int y = 0; y < H; y++) {
        *o++ = 0;
        for (int x = 0; x < W; x++) {
            uint16_t c = px[y * W + x];
            int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
            *o++ = r << 3 | r >> 2; *o++ = g << 2 | g >> 4; *o++ = b << 3 | b >> 2;
        }
    }
    uLongf zn = compressBound(raw_n);
    compress2(z, &zn, raw, raw_n, 9);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13];
    put32(ih, W); put32(ih + 4, H);
    ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", z, zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

static const int s_g[] = {0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496};
static int gain_tenths(int i) { return i <= 0 ? -1 : s_g[(i - 1) % 29]; }

static const char *gain_label(int i)
{
    static char b[24];
    if (i <= 0) return "AGC";
    snprintf(b, sizeof b, "%d.%d dB", gain_tenths(i) / 10, gain_tenths(i) % 10);
    return b;
}

/* centre of top-bar key i of n on the ADS-B and scanner screens (lx: its landscape centre) */
static int key_cx(int i, int n, int lx)
{
    if (!LCD_PORTRAIT) return lx;
    int w = (LCD_W - 6 * (n + 1)) / n;
    return 6 + i * (w + 6) + w / 2;
}
#define KEY_CY (LCD_PORTRAIT ? 66 : 20)

/* Every screen in every look, file names starting with tag. */
static void render_all(const char *dir, const char *tag, uint32_t freq, int pressed)
{
    static uint8_t iq[NAVG * 1024 * 2];
    static float db[NFFT_MAX];
    static bool once;
    if (!once) {
        spectrum_init();
        view_init(RATE);
        panel_init();
    }
    view_set_audio("WFM P4 SDR", true);   /* an RDS station name */
    int n = view_themes(), cols = 2, rows = n, gap = 8;   /* left: receiver, right: Controls */
    int SW = cols * LCD_W + (cols + 1) * gap, SH = rows * LCD_H + (rows + 1) * gap;
    uint16_t *sheet = calloc((size_t)SW * SH, 2);
    for (int i = 0; i < SW * SH; i++) sheet[i] = 0x4208;
    for (int t = 0; t < n; t++) {
        srand(1);
        view_set_theme(t);
        view_reset();
        view_header(freq, "AGC", pressed);
        view_axis(freq);
        for (int frame = 0; frame < LCD_H; frame++) {
            synth(iq, NAVG * NFFT);
            spectrum_compute(iq, NAVG, db);
            view_render(db, NFFT, freq);
        }
        view_blit();
        view_audio_controls(5, 2, 4, 0, -1);
        char path[512];
        snprintf(path, sizeof path, "%s/%s%s.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        int ox = gap, oy = gap + t * (LCD_H + gap);
        for (int y = 0; y < LCD_H; y++) memcpy(&sheet[(oy + y) * SW + ox], &FB(0, y), LCD_W * 2);
        ctl_t ct = {.gain_idx = 17, .gain_n = 30, .gain_label = gain_label, .gain_tenths = gain_tenths,
                    .range_auto = false, .ref_db = -20, .range_db = 60, .zoom = 1, .bw = 0, .band = 0,
                    .vol = 5, .sql = 2};
        panel_open(&ct);
        snprintf(path, sizeof path, "%s/%s%s-controls.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        ox += LCD_W + gap;
        for (int y = 0; y < LCD_H; y++) memcpy(&sheet[(oy + y) * SW + ox], &FB(0, y), LCD_W * 2);
        /* every sub page: tap its menu tile, save, tap BACK */
        static const char *pg[] = {"gain", "range", "zoom", "audio", "band", "scan", "look", "rf"};
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3, top = view_tune_y(), ph = LCD_H - top;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols, chh = (ph - 12 - 6 * (rows - 1)) / rows;
        for (int i = 0; i < 8; i++) {
            int tx = 6 + cw / 2 + (i % cols) * (cw + 6), ty = top + 6 + chh / 2 + (i / cols) * (chh + 6);
            panel_touch(&ct, PANEL_PRESS, tx, ty, 0);
            panel_touch(&ct, PANEL_RELEASE, tx, ty, 0);
            snprintf(path, sizeof path, "%s/%s%s-%s.png", dir, tag, view_theme_name(t), pg[i]);
            write_png(path, fb, LCD_W, LCD_H);
            panel_touch(&ct, PANEL_PRESS, 60, top + 28, 0);           /* BACK */
            panel_touch(&ct, PANEL_RELEASE, 60, top + 28, 0);
        }
        /* the channel memories, and SAVE picking a slot */
        static const uint32_t mhz[PANEL_MEMS] = {146520000, 162550000, 121500000, 98100000, 0, 446006250, 0, 0};
        static const int8_t mbw[PANEL_MEMS] = {1, 1, 3, 0, 0, 2, 0, 0};
        memcpy(ct.mem_hz, mhz, sizeof mhz);
        memcpy(ct.mem_bw, mbw, sizeof mbw);
        ct.freq = 121500000;
        panel_open_memory(&ct, false);
        snprintf(path, sizeof path, "%s/%s%s-memory.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        panel_open_memory(&ct, true);
        snprintf(path, sizeof path, "%s/%s%s-save.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        panel_open(&ct);
        /* a key lit under the finger (pressed, not yet released) */
        panel_touch(&ct, PANEL_PRESS, 240, top + 6 + chh / 2, 0);
        snprintf(path, sizeof path, "%s/%s%s-pressed.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        panel_touch(&ct, PANEL_MOVE, 240, 10, 0);                    /* slide off: nothing fires */
        panel_touch(&ct, PANEL_RELEASE, 240, 10, 0);
        printf("%s\n", path);
    }
    /* ADS-B list and map with made-up traffic around a home point */
    if (!once) {
        adsb_init();
        adsb_view_init();
    }
    static const struct { uint32_t icao; const char *fl; int alt, spd, trk, vr; double dlat, dlon; } tr[] = {
        {0x4840D6, "KLM1023", 38000, 452, 95, 0, 0.18, -0.30},  {0x40621D, "BAW9KX", 24500, 388, 250, -1600, -0.10, 0.22},
        {0x3C6585, "DLH4AB", 36000, 470, 300, 0, 0.35, 0.40},    {0x485020, "", 7800, 210, 183, -832, -0.06, -0.05},
        {0x4CA2B1, "RYR81QP", 31000, 430, 40, 1200, -0.32, -0.42}, {0xA1B2C3, "", -2147483647 - 1, -1, 0, 0, 0, 0},
        {0x478F41, "SAS1405", 12000, 280, 140, -2200, 0.02, 0.12},
    };
    /* AMAP=path/MAP.BIN draws that base map under the traffic (tools/mapgen makes one) */
    const char *mp = getenv("AMAP");
    if (mp && !once) {
        FILE *f = fopen(mp, "rb");
        static uint8_t mapbuf[16 << 20];
        size_t len = f ? fread(mapbuf, 1, sizeof mapbuf, f) : 0;
        if (f) fclose(f);
        if (!amap_load(mapbuf, len)) { fprintf(stderr, "%s: not a map\n", mp); exit(1); }
        printf("map %s: %d features\n", amap_info()->name, amap_info()->nfeat);
    }
    const double hlat = mp ? amap_info()->lat : 52.31, hlon = mp ? amap_info()->lon : 4.76;
    for (int i = 0; i < (int)(sizeof tr / sizeof tr[0]); i++) {
        adsb_ac_t a = {0};
        a.icao = tr[i].icao;
        snprintf(a.flight, sizeof a.flight, "%s", tr[i].fl);
        a.alt_ft = tr[i].alt; a.speed_kt = tr[i].spd; a.track_deg = tr[i].trk; a.vrate_fpm = tr[i].vr;
        a.has_pos = tr[i].dlat != 0 || tr[i].dlon != 0;
        a.lat = hlat + tr[i].dlat; a.lon = hlon + tr[i].dlon;
        double h = tr[i].trk * M_PI / 180;
        for (int k = 0; k < 8 && a.has_pos; k++) {             /* trail behind it */
            a.trail_lat[k] = (float)(a.lat - (8 - k) * 0.006 * cos(h));
            a.trail_lon[k] = (float)(a.lon - (8 - k) * 0.010 * sin(h));
        }
        a.ntrail = a.has_pos ? 8 : 0;
        a.msgs = 40 + 37 * i;
        a.seen_ms = -1000 * i;
        if (!once) adsb_test_add(&a);
    }
    adsb_view_info_t info = {.msgs_per_s = 212, .gain_tenths = 496, .have_home = true, .home_lat = hlat, .home_lon = hlon, .rec = 1};
    for (int t = 0; t < n; t++) {
        view_set_theme(t);
        char path[512];
        adsb_view_open();
        adsb_view_draw(&info);
        snprintf(path, sizeof path, "%s/%s%s-adsb-list.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        adsb_view_touch(AV_PRESS, key_cx(0, 2, 340), KEY_CY);     /* MAP */
        adsb_view_touch(AV_RELEASE, key_cx(0, 2, 340), KEY_CY);
        adsb_view_draw(&info);
        snprintf(path, sizeof path, "%s/%s%s-adsb-map.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        if (mp) {                                              /* zoomed out twice, then in */
            static const struct { int x, n; const char *tag; } z[] = {{200, 2, "200"}, {264, 4, "10"}};
            for (int k = 0; k < 2; k++) {
                for (int j = 0; j < z[k].n; j++) {
                    int zx = key_cx(z[k].x == 200 ? 0 : 1, 4, z[k].x);
                    adsb_view_touch(AV_PRESS, zx, KEY_CY);
                    adsb_view_touch(AV_RELEASE, zx, KEY_CY);
                }
                adsb_view_draw(&info);
                snprintf(path, sizeof path, "%s/%s%s-adsb-map%s.png", dir, tag, view_theme_name(t), z[k].tag);
                write_png(path, fb, LCD_W, LCD_H);
            }
            for (int j = 0; j < 2; j++) {
                adsb_view_touch(AV_PRESS, key_cx(0, 4, 200), KEY_CY);
                adsb_view_touch(AV_RELEASE, key_cx(0, 4, 200), KEY_CY);
            }
        }
    }
    /* Wi-Fi and BLE scanners with made-up neighbours */
    static scan_ap_t ap[] = {
        {"Cafe-Guest", {0x3C, 0x84, 0x6A, 0x11, 0x20, 0x01}, -48, 6, "OPEN", -1000},
        {"NETGEAR52", {0xA0, 0x40, 0xA0, 0x7E, 0x22, 0x13}, -57, 1, "WPA2", -2000},
        {"ATT8kq2Pw", {0x88, 0x96, 0x4E, 0x01, 0x9A, 0xC2}, -63, 11, "WPA2/3", 0},
        {"", {0x8A, 0x96, 0x4E, 0x01, 0x9A, 0xC3}, -64, 11, "WPA2", 0},
        {"Spectrum-5G-Lobby", {0x10, 0x56, 0x11, 0x88, 0x01, 0x0F}, -71, 6, "WPA2", -3000},
        {"HP-Print-4B-OfficeJet", {0xDA, 0x12, 0x65, 0x4B, 0x00, 0x01}, -74, 6, "WPA2", -5000},
        {"Xfinity", {0x92, 0x1A, 0x22, 0x33, 0x44, 0x55}, -78, 3, "OPEN", -2000},
        {"MyRing-7731", {0x34, 0x3E, 0xA4, 0x77, 0x31, 0x02}, -81, 9, "WPA2", -9000},
        {"TP-Link_E2C4", {0x50, 0xC7, 0xBF, 0xE2, 0xC4, 0x10}, -84, 1, "WPA2", -4000},
        {"Tesla-Model-Y", {0x4C, 0xFC, 0xAA, 0x01, 0x02, 0x03}, -88, 13, "WPA2", -12000},
    };
    static scan_ble_t ble[] = {
        {{0x5A, 0x11, 0x2B, 0x9C, 0x00, 0x41}, true, true, -52, "", "Apple", 312, 0},
        {{0xD4, 0x3A, 0x2C, 0x18, 0x77, 0x01}, false, true, -60, "LE-Bose QC45", "", 88, -1000},
        {{0x71, 0x02, 0xC3, 0x55, 0xE1, 0x9D}, true, false, -66, "", "Microsoft", 140, 0},
        {{0xE8, 0x9F, 0x6D, 0x01, 0x22, 0x33}, false, true, -70, "Galaxy Watch5", "Samsung", 64, -2000},
        {{0x43, 0x9A, 0x10, 0x01, 0x02, 0x03}, true, false, -77, "", "Fast Pair", 22, -5000},
        {{0xC1, 0x77, 0x23, 0x90, 0x41, 0x0A}, true, false, -83, "", "Tile", 9, -14000},
        {{0x08, 0x3A, 0xF2, 0x10, 0x20, 0x30}, false, true, -89, "ESP32-C6 test", "Espressif", 5, -31000},
        {{0x66, 0x01, 0x02, 0x03, 0x04, 0x05}, true, false, -94, "", "", 3, -40000},
    };
    if (!once) scan_view_init();
    once = true;
    for (int t = 0; t < n; t++) {
        view_set_theme(t);
        char path[512];
        scan_view_info_t wi = {.kind = SV_WIFI, .ap = ap, .nap = sizeof ap / sizeof ap[0], .now_ms = 0, .scans = 14, .rec = 1};
        scan_view_open(SV_WIFI);
        scan_view_draw(&wi);
        snprintf(path, sizeof path, "%s/%s%s-wifi-list.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        scan_view_touch(SV_PRESS, key_cx(1, 3, 340), KEY_CY);     /* CHAN */
        scan_view_touch(SV_RELEASE, key_cx(1, 3, 340), KEY_CY);
        scan_view_draw(&wi);
        snprintf(path, sizeof path, "%s/%s%s-wifi-chan.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        scan_view_info_t bi = {.kind = SV_BLE, .ble = ble, .nble = sizeof ble / sizeof ble[0], .now_ms = 0, .rec = 1};
        scan_view_open(SV_BLE);
        scan_view_draw(&bi);
        snprintf(path, sizeof path, "%s/%s%s-ble.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
    }
    for (int t = 0; t < n; t++) {           /* boot splash: halfway, then tuned with status lines */
        view_set_theme(t);
        char path[512];
        for (int i = 0; i <= 6; i++) view_splash(i, 15, "RTL-SDR Blog V4 receiver");
        view_splash_line(LCD_H - 24, "hold screen to recalibrate", SPLASH_DIM);
        snprintf(path, sizeof path, "%s/%s%s-splash-mid.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
        view_splash(0, 1, "RTL-SDR Blog V4 receiver");
        view_splash_line(view_splash_text_y(), "USB link OK: 4.80 MB/s, 0 bytes lost", SPLASH_OK);
        view_splash_line(view_splash_text_y() + 32, "RTL-SDR Blog V4 (R828D)", SPLASH_OK);
        snprintf(path, sizeof path, "%s/%s%s-splash.png", dir, tag, view_theme_name(t));
        write_png(path, fb, LCD_W, LCD_H);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%sthemes.png", dir, tag);
    write_png(path, sheet, SW, SH);
    free(sheet);
    printf("%s\n", path);
}

/* usage: preview OUT_DIR [freq_hz] [pressed_button]
 * Writes OUT_DIR/<theme>.png, <theme>-controls.png and the other screens for every theme, and
 * OUT_DIR/themes.png; then the same in portrait, named portrait-*. */
static void test_audio_controls(void)
{
    assert(PANEL_BWS == 6);
    assert(!strcmp(panel_bw_mode(4), "USB") && panel_bw_hz(4) == 3000);
    assert(!strcmp(panel_bw_mode(5), "LSB") && panel_bw_hz(5) == 3000);
    view_init(RATE);
    panel_init();
    for (int orientation = 0; orientation < 2; orientation++) {
        rpi_lcd_set_portrait(orientation != 0);
        for (int theme = 0; theme < view_themes(); theme++) {
            view_set_theme(theme);
            view_blit_waterfall();
#ifdef CONFIG_SDR_WAVESHARE_43
            assert(s_last_y + s_last_h == LCD_H - 156); /* zoom and modes never get waterfall pixels */
#else
            assert(s_last_y + s_last_h == LCD_H - 80);
#endif
            view_audio_controls(5, 2, 4, 0, -1);
            int drawn = s_blit_count;
            view_audio_controls(5, 2, 4, 0, -1);
            assert(s_blit_count == drawn); /* untouched footer is not blitted every frame */
            view_blit_waterfall();
            drawn = s_blit_count;
            view_audio_controls(5, 2, 4, 0, -1);
            assert(s_blit_count == drawn); /* waterfall refresh cannot invalidate footer */
#ifdef CONFIG_SDR_WAVESHARE_43
            int zy = LCD_H - 156 + 18;
            int mw = LCD_W - 208 - 18;
            assert(view_zoom_button_at(30, zy) == -1); /* moved to header, no footer duplicate */
            assert(view_zoom_button_at(mw - 24, zy) == -1);
            assert(view_zoom_button_at(mw / 2, zy) == -1);
            assert(view_mode_button_at(30, zy) == -1);
            assert(view_audio_button_at(30, zy) == -1);
            view_audio_controls(5, 2, 4, 1, -1);
            assert(s_blit_count > drawn); /* changing scale refreshes the static zoom label */
            view_audio_controls(5, 2, 4, 0, -1);
#endif
            size_t header_bytes = (size_t)LCD_W * view_tune_y() * sizeof(uint16_t);
            uint16_t *old_header = malloc(header_bytes);
            assert(old_header);
            view_set_fine_tune(true);
            view_header(142000000, "AGC", -1);
            memcpy(old_header, fb, header_bytes);
            view_header(142000100, "AGC", -1);
            assert(memcmp(old_header, fb, header_bytes)); /* 100 Hz must be visible */
            view_set_fine_tune(false);
            view_header(142000000, "AGC", -1);
            memcpy(old_header, fb, header_bytes);
            view_header(142000100, "AGC", -1);
#ifdef CONFIG_SDR_WAVESHARE_43
            assert(memcmp(old_header, fb, header_bytes)); /* preserve a residual 100 Hz offset */
#else
            assert(!memcmp(old_header, fb, header_bytes)); /* original kit keeps whole-kHz FM */
#endif
#ifdef CONFIG_SDR_WAVESHARE_43
            int by = LCD_H - 22;
            assert(view_button_at(LCD_PORTRAIT ? 50 : 40, by) == VIEW_DN1M);
            assert(view_button_at(LCD_W / 2 + 10, by) == VIEW_STEP);
            assert(view_button_at(LCD_W - 45, by) == VIEW_UP1M);
            #ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(20, view_tune_y() - 10) == -1);
#else
            assert(view_button_at(LCD_PORTRAIT ? 200 : LCD_W / 2, 20) == -1);
#endif /* no theme cycling */
            assert(view_button_at(LCD_PORTRAIT ? 50 : 40, by) != VIEW_STEP);
            view_set_tune_step("1k");
            view_header(142000000, "AGC", -1);
            memcpy(old_header, fb, header_bytes);
            view_set_tune_step("12.5k");
            view_header(142000000, "AGC", -1);
            view_audio_controls(5, 2, 4, 0, -1);
            assert(view_button_at(LCD_W / 2 + 10, LCD_H - 22) == VIEW_STEP);
            assert(!memcmp(old_header, fb, header_bytes)); /* step moved out of header */
#endif
            free(old_header);
            int x = LCD_W - 8 - 208, y = LCD_H - 8 - 72;
#ifdef CONFIG_SDR_WAVESHARE_43
            y -= 44;
#endif
            assert(y > view_tune_y());
            assert(view_audio_button_at(x + 94, y + 19) == VIEW_MUTE);
            assert(view_audio_button_at(x + 94, y + 52) == -1);
            int before_mute = s_blit_count;
            view_set_mute(true);
            view_audio_controls(5, 2, 4, 0, -1);
            assert(s_blit_count > before_mute); /* mute updates the fixed footer */
            before_mute = s_blit_count;
            view_audio_controls(5, 2, 4, 0, -1);
            assert(s_blit_count == before_mute); /* but not on every waterfall frame */
            view_set_mute(false);
            view_audio_controls(5, 2, 4, 0, -1);
            assert(view_audio_button_at(x + 144, y + 19) == VIEW_VOL_DN);
            assert(view_audio_button_at(x + 184, y + 19) == VIEW_VOL_UP);
            assert(view_audio_button_at(x + 144, y + 52) == VIEW_SQL_DN);
            assert(view_audio_button_at(x + 184, y + 52) == VIEW_SQL_UP);
            assert(view_audio_button_at(x - 1, y + 19) == -1);
            assert(view_audio_button_at(x + 20, y + 19) == -1);
            assert(view_audio_button_at(x + 184, y - 1) == -1);
            assert(view_audio_button_at(x + 184, LCD_H - 1) == -1);
            assert(view_audio_overlay_at(x + 20, y + 19));
            assert(view_audio_overlay_at(x + 208 - 1, y + 71));
            assert(view_audio_overlay_at(x - 1, y + 19)); /* mode region also blocks tuning */
#ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_audio_overlay_at(x + 20, y - 1)); /* zoom strip also blocks tuning */
            assert(!view_audio_overlay_at(x + 20, LCD_H - 157));
#else
            assert(!view_audio_overlay_at(x + 20, y - 1));
#endif
            int cols = LCD_W >= 700 ? 6 : LCD_W >= 480 ? 3 : 0;
            if (cols) {
                int mw = LCD_W - 208 - 18;
                int kw = (mw - (cols - 1) * 4) / cols;
                for (int i = 0; i < 6; i++) {
                    int bx = 6 + i % cols * (kw + 4) + kw / 2;
                    int by = LCD_H - 80 + (cols == 6 ? 14 + 26 : 6 + i / cols * 36 + 16);
#ifdef CONFIG_SDR_WAVESHARE_43
                    by -= 44;
#endif
                    assert(view_mode_button_at(bx, by) == VIEW_MODE_0 + i);
                    assert(view_audio_button_at(bx, by) == -1);
                }
            }
            view_audio_controls(5, 2, 4, 0, -1);
        }
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths, .range_auto = true, .vol = 5};
        panel_open(&ct);
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int menu_x = 6 + 3 % cols * (cw + 6) + cw / 2;
        int menu_y = view_tune_y() + 6 + 3 / cols * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, menu_x, menu_y, 0);
        panel_touch(&ct, PANEL_RELEASE, menu_x, menu_y, 0);
        int body = LCD_H - view_tune_y() - 56 - 6;
        int hm = LCD_PORTRAIT ? (body - 18) / 2 : (body - 12) * 40 / 100;
        cols = LCD_PORTRAIT ? 2 : 3;
        cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        ch = (hm - 6 * ((PANEL_BWS / cols) - 1)) / (PANEL_BWS / cols);
        for (int i = 4; i <= 5; i++) {
            int px = 6 + (i % cols) * (cw + 6) + cw / 2;
            int py = view_tune_y() + 56 + (i / cols) * (ch + 6) + ch / 2;
            panel_touch(&ct, PANEL_PRESS, px, py, 0);
            assert(panel_touch(&ct, PANEL_RELEASE, px, py, 0) & CTL_BW);
            assert(ct.bw == i);
        }
        size_t screen_bytes = (size_t)LCD_W * LCD_H * sizeof(uint16_t);
        uint16_t *saved = malloc(screen_bytes);
        assert(saved);
        ct.mem_bw[0] = 4;
        ct.mem_hz[0] = 142000000;
        panel_open_memory(&ct, false);
        memcpy(saved, fb, screen_bytes);
        ct.mem_hz[0] = 142000100;
        panel_redraw(&ct);
        assert(memcmp(saved, fb, screen_bytes)); /* SSB memory shows 100 Hz */
        ct.mem_bw[0] = 0;
        panel_redraw(&ct);
        memcpy(saved, fb, screen_bytes);
        ct.mem_hz[0] = 142000000;
        panel_redraw(&ct);
        assert(!memcmp(saved, fb, screen_bytes)); /* FM memory remains whole kHz */
        free(saved);
    }
    puts("audio controls: hit regions and USB/LSB menu selection, all themes and orientations OK");
}

static void test_touch_tuning(void)
{
    static float db[1024];
    const uint32_t fc = 142000000;
    view_init(RATE);
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        for (int theme = 0; theme < view_themes(); theme++) {
            view_set_theme(theme);
            view_set_span(RATE);
            int sx = LCD_W / 2 + 31, sy = view_tune_y() + 30;
            uint32_t hz = 0;
            assert(!view_touch_frequency(sx, sy, fc, 10000, VIEW_TOUCH_FREE, &hz)); /* no fresh FFT */
            for (int i = 0; i < 1024; i++) db[i] = -100;
            view_render(db, 1024, fc);
            assert(view_touch_frequency(sx, sy, fc, 10000, VIEW_TOUCH_FREE, &hz));
            int64_t raw = (int64_t)fc + (int64_t)(sx - LCD_W / 2) * view_hz_per_px();
            assert(hz == (uint32_t)((raw + 5000) / 10000 * 10000));
            assert(view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_SNAP, &hz));
            assert(hz == (uint32_t)((raw + 6250) / 12500 * 12500));
            assert(!view_touch_frequency(sx, view_tune_y() - 1, fc, 10000, VIEW_TOUCH_FREE, &hz));
            assert(!view_touch_frequency(sx, view_spectrum_bottom(), fc, 10000, VIEW_TOUCH_FREE, &hz));
            assert(!view_touch_frequency(sx, view_spectrum_bottom() + 20, fc, 10000, VIEW_TOUCH_FREE, &hz));
            assert(!view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz)); /* no strong peak: no tune */
            /* Two peaks: the nearer one wins even when the farther one is taller. */
            int near_x = sx + 6, far_x = sx - 18;
            db[near_x * 1024 / LCD_W] = -65;
            db[far_x * 1024 / LCD_W] = -50;
            view_render(db, 1024, fc);
            assert(view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz));
            int64_t near_hz = (int64_t)fc + (int64_t)(near_x - LCD_W / 2) * view_hz_per_px();
            assert(llabs((int64_t)hz - near_hz) <= 2 * view_hz_per_px());
            assert(!view_touch_frequency(sx, sy, fc + 1000000, 12500, VIEW_TOUCH_PEAK, &hz));
            view_peak_invalidate(); /* retune or stream outage cannot reuse an old carrier */
            assert(!view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz));
            view_render(db, 1024, fc);
            assert(view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz));
            assert(!view_touch_frequency(sx + 60, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz));
            view_reset();
            assert(!view_touch_frequency(sx, sy, fc, 12500, VIEW_TOUCH_PEAK, &hz));
            /* The previous station's smoothed -100 dB floor must not let a
             * 9 dB bump at a new -70 dB station masquerade as a 12 dB peak. */
            for (int i = 0; i < 1024; i++) db[i] = -100;
            view_render(db, 1024, fc);
            view_peak_invalidate(); /* retune to a much noisier station */
            for (int i = 0; i < 1024; i++) db[i] = -70;
            db[sx * 1024 / LCD_W] = -61;
            view_render(db, 1024, fc + 1000000);
            assert(view_floor() > -72 && view_floor() < -68);
            assert(!view_touch_frequency(sx, sy, fc + 1000000, 12500, VIEW_TOUCH_PEAK, &hz));
#ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(30, 20) == VIEW_TOUCH_MODE);
            #ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(LCD_W - 30, 20) == VIEW_GAIN);
#else
            assert(view_button_at(LCD_W - 30, 20) == VIEW_GAIN);
#endif
            #ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(20, view_tune_y() - 10) == -1);
#else
            assert(view_button_at(LCD_PORTRAIT ? 200 : LCD_W / 2, 20) == -1);
#endif
            view_set_touch_mode(VIEW_TOUCH_FREE);
            view_header(fc, "AGC", -1);
            uint16_t free_button[74 * 128];
            for (int yy = 0; yy < view_tune_y(); yy++)
                memcpy(free_button + yy * 74, &FB(4, yy), 74 * sizeof(uint16_t));
            view_set_touch_mode(VIEW_TOUCH_SNAP);
            view_header(fc, "AGC", -1);
            assert(view_button_at(30, 20) == VIEW_TOUCH_MODE);
            bool changed = false;
            for (int yy = 0; yy < view_tune_y(); yy++)
                if (memcmp(free_button + yy * 74, &FB(4, yy), 74 * sizeof(uint16_t))) changed = true;
            assert(changed); /* mode label actually changes on screen */
            view_set_touch_mode(VIEW_TOUCH_PEAK);
            assert(view_touch_mode() == VIEW_TOUCH_PEAK);
            view_set_touch_mode(VIEW_TOUCH_FREE);
#endif
        }
    }
    puts("touch tune: spectrum-only, STEP snap, local strong FFT peaks, mode button in every look/orientation");
}

static void assert_phosphor_pixel(int i)
{
#ifdef CONFIG_SDR_WAVESHARE_43
    /* Explicit safety-color exception: the requested red record/stop glyph only. */
    int cx=LCD_PORTRAIT ? 356 : 380, x=i%LCD_W, y=i/LCD_W;
    if(x>=cx-5 && x<=cx+5 && y>=7 && y<=17) return;
#endif
    assert(!(fb[i] & (0xf800 | 0x001f)));
}
static void test_phosphor_green(void)
{
    static float db[1024];
    view_init(RATE);
    panel_init();
    for (int orientation = 0; orientation < 2; orientation++) {
        rpi_lcd_set_portrait(orientation != 0);
        view_set_theme(2); /* Phosphor */
        view_header(142000100, "AGC", -1);
        view_axis(142000100);
        for (int i = 0; i < 1024; i++) db[i] = -65 + 3 * sin(i / 10.0);
        view_set_audio("USB 5", true);
        view_render(db, 1024, 142000100);
        view_blit();
        view_audio_controls(5, 2, 4, 0, -1);
        for (int i = 0; i < LCD_W * LCD_H; i++)
            assert_phosphor_pixel(i);
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths, .range_auto = true, .vol = 5};
        panel_open(&ct);
        for (int i = 0; i < LCD_W * LCD_H; i++)
            assert_phosphor_pixel(i);
        view_splash(0, 1, "RTL-SDR receiver");
        view_splash_line(view_splash_text_y(), "USB link OK", SPLASH_OK);
        view_splash_line(view_splash_text_y() + 20, "USB link failed", SPLASH_BAD);
        for (int i = 0; i < LCD_W * LCD_H; i++)
            assert(!(fb[i] & (0xf800 | 0x001f)));
        view_message("RTL-SDR not streaming", "Check USB link");
        for (int i = 0; i < LCD_W * LCD_H; i++)
            assert(!(fb[i] & (0xf800 | 0x001f)));
    }
    puts("Phosphor: monochrome except explicit red record safety glyph");
}

static void test_footer_stream_recovery(void)
{
    view_init(RATE);
    view_set_theme(0);
    view_audio_controls(5, 2, 4, 0, -1);
    int x = LCD_W / 2, y = LCD_H - 40;
    uint16_t expected = FB(x, y);
    view_message("RTL-SDR not streaming", "USB link lost");
    assert(FB(x, y) != expected); /* stream-loss message paints over footer */
    view_header(142000100, "AGC", -1);
    view_axis(142000100);
    view_blit();
    int drawn = s_blit_count;
    view_audio_controls(5, 2, 4, 0, -1);
    assert(s_blit_count > drawn); /* footer must be restored without a value change */
    assert(FB(x, y) == expected);
    puts("stream recovery restores footer after the message clears it");
}

static void test_hf_diagnostic_page(void)
{
    view_init(RATE);
    panel_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        view_set_theme(0);
        ctl_t ct = {.gain_label = gain_label, .gain_tenths = gain_tenths};
        hf_diag_identity(&ct.hf_diag, "RTLSDRBlog", "Blog V4", true);
        hf_diag_tune(&ct.hf_diag, 10000000, 0,
                     (hf_diag_tuner_t){.pll = 0, .gpio5_attempted = false});
        panel_open(&ct);
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + 7 % cols * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + 7 / cols * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* RF INPUT */
        x = LCD_W - 60;
        y = view_tune_y() + 28;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* HF DIAG */
        char line[96];
        for (int i = 0; i < 10; i++) {
            const char *text = hf_diag_line(&ct.hf_diag, i, line, sizeof line);
            if (rpi_cv_text_w(text, 1) > LCD_W - 24)
                fprintf(stderr, "diag line %d width %d screen %d: %s\n", i, rpi_cv_text_w(text, 1), LCD_W, text);
            assert(rpi_cv_text_w(text, 1) <= LCD_W - 24);
        }
        assert(58 + 9 * 22 + 16 < LCD_H - view_tune_y());
        uint16_t before[LCD_LONG * LCD_SHORT];
        memcpy(before, fb, sizeof before);
        hf_diag_tune(&ct.hf_diag, 10000000, -3,
                     (hf_diag_tuner_t){.pll = 1, .gpio5_attempted = true, .gpio5_rc = -3});
        panel_redraw(&ct);
        assert(memcmp(before, fb, sizeof before)); /* diagnostic text actually reaches framebuffer */
        panel_touch(&ct, PANEL_PRESS, 60, y, 0);
        panel_touch(&ct, PANEL_RELEASE, 60, y, 0); /* BACK -> RF INPUT */
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* reopen */
        assert(!(panel_touch(&ct, PANEL_PRESS, 60, y, 0) & CTL_CLOSE));
        assert(!(panel_touch(&ct, PANEL_RELEASE, 60, y, 0) & CTL_CLOSE));
    }
    puts("HF diagnostic page: RF INPUT route, read-only text and BACK in both rotations");
}

static void test_rf_settings(void)
{
    view_init(RATE);
    panel_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        view_set_theme(0);
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths, .direct_supported = false};
        panel_open(&ct);
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + 7 % cols * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + 7 / cols * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* RF tile */
        int body = LCD_H - view_tune_y() - 62;
        int half = (body - 6) / 2;
        int bias_y = view_tune_y() + 56 + half / 2;
        x = 6 + (LCD_W - 18) / 2 + 6 + (LCD_W - 18) / 4;
        panel_touch(&ct, PANEL_PRESS, x, bias_y, 0);
        assert((panel_touch(&ct, PANEL_RELEASE, x, bias_y, 0) & CTL_BIAS) && ct.bias);
        panel_touch(&ct, PANEL_PRESS, 60, bias_y, 0);
        assert((panel_touch(&ct, PANEL_RELEASE, 60, bias_y, 0) & CTL_BIAS) && !ct.bias);
        ct.bias_error = true;
        panel_redraw(&ct);
        panel_touch(&ct, PANEL_PRESS, 60, bias_y, 0);
        assert(panel_touch(&ct, PANEL_RELEASE, 60, bias_y, 0) & CTL_BIAS);
        assert(!ct.bias); /* OFF retries despite last requested state being OFF */
        ct.bias_error = false;
        panel_redraw(&ct);
        int dw = (LCD_W - 12 - 12) / 3;
        int direct_y = view_tune_y() + 56 + half + 6 + (body - half - 6) / 2;
        x = 6 + 2 * (dw + 6) + dw / 2;
        panel_touch(&ct, PANEL_PRESS, x, direct_y, 0);
        assert(!(panel_touch(&ct, PANEL_RELEASE, x, direct_y, 0) & CTL_DIRECT));
        assert(ct.direct == 0); /* Blog V4 uses its HF upconverter instead */
        ct.direct_supported = true;
        panel_redraw(&ct);
        panel_touch(&ct, PANEL_PRESS, x, direct_y, 0);
        assert((panel_touch(&ct, PANEL_RELEASE, x, direct_y, 0) & CTL_DIRECT) && ct.direct == 2);
        ct.bias = true; ct.bias_forced = true;
        panel_redraw(&ct);
        panel_touch(&ct, PANEL_PRESS, 60, bias_y, 0);
        assert(!(panel_touch(&ct, PANEL_RELEASE, 60, bias_y, 0) & CTL_BIAS));
        assert(ct.bias);
    }
    puts("RF settings: bias, I/Q selection, V4 and EEPROM restrictions in both orientations");
}

static void test_ism_bands(void)
{
    assert(PANEL_BANDS >= 11); /* append; preserve eight existing preset indexes */
    static const uint32_t hz[] = {315000000, 345000000, 915000000};
    for (int i = 0; i < 3; i++) {
        uint32_t freq;
        int bw, zoom;
        const char *name = panel_band(8 + i, &freq, &bw, &zoom);
        assert(name && freq == hz[i]);
        assert(bw >= 0 && bw < PANEL_BWS && zoom >= 0 && zoom < PANEL_ZOOMS);
    }
    uint32_t freq;
    int bw, zoom;
    assert(panel_band(6, &freq, &bw, &zoom) && freq == 433920000); /* existing 70cm/433 slot */
    puts("North American RX preset frequencies: 315, 345, 433.92 and 915 MHz");
}

static void test_am_sw_cb_pages(void)
{
    static const struct { uint32_t hz; int mode; } added[] = {
        {600000, 3}, {1000000, 3}, {1600000, 3},
        {3950000, 3}, {4900000, 3}, {6050000, 3}, {7250000, 3},
        {9650000, 3}, {11850000, 3}, {13700000, 3}, {15350000, 3},
        {17700000, 3}, {21600000, 3},
        {26965000, 3}, {27065000, 3}, {27185000, 3},
        {27385000, 5}, {27385000, 4}, {27405000, 3},
    };
    assert(PANEL_BANDS == 11 + (int)(sizeof added / sizeof added[0]));
    for (int i = 11; i < PANEL_BANDS; i++) {
        uint32_t hz; int bw, zoom;
        assert(panel_band(i, &hz, &bw, &zoom));
        assert(hz == added[i - 11].hz && bw == added[i - 11].mode);
        assert(zoom >= 0 && zoom < PANEL_ZOOMS);
    }
    view_init(RATE);
    panel_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        view_set_theme(0);
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths};
        panel_open(&ct);
        int cols = portrait ? 2 : 4, rows = portrait ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + (4 % cols) * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + (4 / cols) * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* BAND page */
        int bh = LCD_H - view_tune_y() - 62;
        int grid_h = bh - 54;
        int page_size = cols * (portrait ? 5 : 3), page = 0;
        for (int i = 0; i < PANEL_BANDS; i++) {
            while (page < i / page_size) {
                int nx = LCD_W * 3 / 4, ny = view_tune_y() + 56 + bh - 24;
                panel_touch(&ct, PANEL_PRESS, nx, ny, 0);
                assert(!(panel_touch(&ct, PANEL_RELEASE, nx, ny, 0) & CTL_BAND));
                page++;
            }
            cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
            ch = (grid_h - 6 * ((page_size / cols) - 1)) / (page_size / cols);
            int cell = i % page_size;
            x = 6 + (cell % cols) * (cw + 6) + cw / 2;
            y = view_tune_y() + 56 + (cell / cols) * (ch + 6) + ch / 2;
            panel_touch(&ct, PANEL_PRESS, x, y, 0);
            assert((panel_touch(&ct, PANEL_RELEASE, x, y, 0) & (CTL_BAND | CTL_CLOSE)) == (CTL_BAND | CTL_CLOSE));
            assert(ct.band == i);
        }
    }
    puts("30 RX presets, AM/SW/CB demod modes, every paginated hit in both orientations");
}

static void test_theme_header_disabled(void)
{
    view_init(RATE);
    panel_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        for (int look = 0; look < view_themes(); look++) {
            view_set_theme(look);
            #ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(20, view_tune_y() - 10) == -1);
#else
            assert(view_button_at(LCD_PORTRAIT ? 200 : LCD_W / 2, 20) == -1);
#endif /* frequency is not a theme switch */
            #ifdef CONFIG_SDR_WAVESHARE_43
            assert(view_button_at(LCD_W - 30, 20) == VIEW_GAIN);
#else
            assert(view_button_at(LCD_W - 30, 20) == VIEW_GAIN);
#endif /* MENU still works */
            assert(view_theme() == look);
        }
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths};
        panel_open(&ct);
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + (6 % cols) * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + (6 / cols) * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* Controls > LOOK */
        int bh = LCD_H - view_tune_y() - 62;
        cw = (LCD_W - 18) / 2;
        ch = (bh - 12) / 3;
        x = 6 + (cw + 6) + cw / 2;
        y = view_tune_y() + 56 + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        assert(panel_touch(&ct, PANEL_RELEASE, x, y, 0) & CTL_LOOK);
        assert(ct.look == 1); /* explicit look selection remains available */
    }
    puts("header frequency inert; Controls remains accessible across looks and orientations");
}

static void test_screen_settings(void)
{
    view_init(RATE); panel_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0); view_set_theme(0);
        ctl_t ct = {.gain_n = 30, .gain_label = gain_label, .gain_tenths = gain_tenths};
        panel_open(&ct);
        int cols = portrait ? 2 : 4, rows = portrait ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + 8 % cols * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + 8 / cols * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
#ifndef CONFIG_SDR_WAVESHARE_43
        assert(panel_touch(&ct, PANEL_RELEASE, x, y, 0) & CTL_CLOSE);
        assert(ct.screen_timeout == 0); /* unsupported board has DONE, not a dead timeout */
        continue;
#endif
        panel_touch(&ct, PANEL_RELEASE, x, y, 0);
        int bh = LCD_H - view_tune_y() - 62;
        cw = (LCD_W - 18) / 2; ch = (bh - 6) / 2;
        const int seconds[] = {30, 60, 90, 0};
        for (int i = 0; i < 4; i++) {
            x = 6 + i % 2 * (cw + 6) + cw / 2;
            y = view_tune_y() + 56 + i / 2 * (ch + 6) + ch / 2;
            panel_touch(&ct, PANEL_PRESS, x, y, 0);
            assert(panel_touch(&ct, PANEL_RELEASE, x, y, 0) & CTL_SCREEN);
            assert(ct.screen_timeout == seconds[i]);
        }
    }
    puts("settings screen timeout: 30/60/90 seconds/never in both orientations");
}

static void test_sensor_entry(void)
{
    view_init(RATE);
    panel_init();
    sensor_view_init();
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        view_set_theme(0);
        ctl_t ct = {.gain_idx = 0, .gain_n = 30, .gain_label = gain_label,
                    .gain_tenths = gain_tenths};
        panel_open(&ct);
        int cols = portrait ? 2 : 4, rows = portrait ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + (5 % cols) * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + (5 / cols) * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0); /* Controls > SCAN */
        int bh = LCD_H - view_tune_y() - 62;
        cw = (LCD_W - 18) / 2;
        ch = (bh - 12) / 3;
        x = 6 + cw / 2;
        y = view_tune_y() + 56 + 2 * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        assert(panel_touch(&ct, PANEL_RELEASE, x, y, 0) & CTL_SENSOR);
        sensor_view_open();
        int bx = LCD_W - 178, by = portrait ? 68 : 25;
        assert(sensor_view_band_hz(0) == 433920000);
        assert(sensor_view_band_hz(4) == 915000000);
        assert(sensor_view_touch(SENSOR_PRESS, bx, by) == 0);
        assert(sensor_view_touch(SENSOR_RELEASE, bx, by) == SENSOR_BAND);
        sensor_view_info_t info = {.wh2 = 1, .tpms = 2, .streaming = true,
                                   .rows = 2, .event = {"WH2  D1 24.6 C 33%", "TPMS 1A4E 259.4 kPa"}};
        sensor_view_info_t blank = {.wh2 = 1, .tpms = 2, .streaming = true};
        sensor_view_draw(&blank);
        int second_row = (portrait ? 96 : 50) + 60 + 36;
        uint32_t blank_crc = crc32(0, (const Bytef *)(fb + second_row * LCD_W),
                                   LCD_W * 34 * sizeof fb[0]);
        sensor_view_draw(&info);
        assert(crc32(0, (const Bytef *)(fb + second_row * LCD_W),
                     LCD_W * 34 * sizeof fb[0]) != blank_crc);
        int unchanged = s_blit_count;
        sensor_view_draw(&info);
        assert(s_blit_count == unchanged); /* live IQ without updates must not blit */
        uint32_t title_crc = crc32(0, (const Bytef *)fb, LCD_W * (portrait ? 96 : 50) * sizeof fb[0]);
        info.dropped_bytes = 1024;
        sensor_view_draw(&info);
        assert(crc32(0, (const Bytef *)fb, LCD_W * (portrait ? 96 : 50) * sizeof fb[0]) == title_crc); /* counters below title, not over it */
        assert(s_blit_count > unchanged && s_last_h < LCD_H); /* bounded SPI transfer */
        sensor_view_draw(&blank); /* a 60-second-old last event is removed */
        assert(crc32(0, (const Bytef *)(fb + second_row * LCD_W),
                     LCD_W * 34 * sizeof fb[0]) == blank_crc);
        sensor_view_draw(&info);
        int ex = LCD_W - 60, ey = portrait ? 68 : 25;
        int before = s_blit_count;
        assert(!sensor_view_touch(SENSOR_PRESS, ex, ey));
        assert(s_blit_count > before); /* EXIT visibly highlights when pressed */
        assert(!sensor_view_touch(SENSOR_RELEASE, 12, LCD_H - 12));
        sensor_view_touch(SENSOR_PRESS, ex, ey);
        assert(sensor_view_touch(SENSOR_RELEASE, ex, ey));
    }
    puts("SCAN > SENSORS entry touch hits in landscape and portrait");
}

static void test_quick_header(void)
{
#ifdef CONFIG_SDR_WAVESHARE_43
    view_init(RATE);
    for (int rotation = 0; rotation < 4; rotation++) {
        int portrait = !(rotation & 1);
        rpi_lcd_rotation = rotation;
        rpi_lcd_set_portrait(portrait != 0);
        for (int look = 0; look < view_themes(); look++) {
            view_set_theme(look);
            int y = look == 1 ? 27 : 17;
            int touch = portrait ? 37 : 41;
            int reset = portrait ? 102 : 118;
            int gain = portrait ? 168 : 190;
            assert(view_button_at(touch, y) == VIEW_TOUCH_MODE);
            assert(view_button_at(gain, y) == VIEW_AGC_CYCLE);
            assert(view_button_at(reset, y) == VIEW_RANGE_RESET);
            assert(view_button_at(LCD_W / 2 - 10, LCD_H - 22) == VIEW_STEP_DOWN);
            assert(view_button_at(LCD_W / 2 + 10, LCD_H - 22) == VIEW_STEP);
            int zoom = portrait ? 234 : 258;
            assert(view_button_at(zoom, y) == VIEW_ZOOM_TOGGLE); /* after GAIN, before the readout */
            assert(view_button_at(portrait ? 302 : 326, y) == VIEW_PRESETS);
            assert(view_button_at(portrait ? 398 : 422, y) == VIEW_PLAY); /* separate triangle after REC */
            assert(view_button_at(portrait ? 356 : 380, y) == VIEW_RECORD);
            assert(!view_header_repeats(VIEW_RECORD) && !view_header_repeats(VIEW_PLAY));
            view_set_recording(false);
            view_header(100000100,"AGC",-1);
            int rec_x=portrait ? 356 : 380, icon_y=(look==1 ? 19 : look==0 ? 4 : 5)+7;
            assert(FB(rec_x,icon_y)==RGB565(255,48,48));
            uint16_t circle_corner=FB(rec_x-5,icon_y-5);
            view_set_recording(true);view_header(100000100,"AGC",-1);
            assert(FB(rec_x-5,icon_y-5)==RGB565(255,48,48));
            assert(circle_corner!=FB(rec_x-5,icon_y-5)); /* stop-square, not a color-only change */
            view_set_recording(false);
            int ro_y = look == 1 ? 73 : 60;     /* readout row (below buttons in portrait) */
            assert(view_button_at(LCD_W / 2, ro_y) == -1);       /* frequency remains inert */
            assert(view_button_at(LCD_W - 34, y) == VIEW_GAIN); /* MENU shares the top control bar */
            assert(view_button_at((portrait ? 418 : 442) + 8, y) == VIEW_GAIN); /* fills remaining topbar, not frequency row */
            assert(view_button_at(LCD_W - 34, ro_y) == -1); /* no stale second-row menu hit */
            /* Real renderer consumes measured mV; boundary colors in every layout/look. */
            const int volts[] = {3599,3600,3900,3901,-1};
            const uint16_t inks[] = {RGB565(255,64,64),RGB565(255,190,40),
                                    RGB565(255,190,40),RGB565(64,230,96)};
            for (int v=0;v<5;v++) {
                preview_battery_mv=volts[v];
                view_header(1720000100,"AGC",-1);
                if(v<4) {
                    int count=0;
                    for(int by=(look==1?54:40);by<view_tune_y();by++)
                        for(int bx=LCD_W-150;bx<LCD_W;bx++) count+=FB(bx,by)==inks[v];
                    assert(count>10);
                }
            }
            preview_battery_mv=-1;
            /* Changing the widest readout cannot overwrite any header key. */
            view_set_fine_tune(true);
            view_header(100000100, "49.6 dB", -1);
            uint16_t before[800 * 128];
            memcpy(before, fb, LCD_W * view_tune_y() * sizeof fb[0]);
            view_header(1720000100, "49.6 dB", -1);
            for (int yy = 0; yy < view_tune_y(); yy++)
                for (int xx = 0; xx < LCD_W; xx++)
                    if (view_button_at(xx, yy) >= 0)
                        assert(FB(xx, yy) == before[yy * LCD_W + xx]);
            const int xs[] = {touch, reset, gain, zoom, LCD_W - 34, 45,
                              LCD_W / 2 - 10, LCD_W / 2 + 10, LCD_W - 45, portrait ? 302 : 326, portrait ? 398 : 422, portrait ? 356 : 380};
            const int ids[] = {VIEW_TOUCH_MODE, VIEW_RANGE_RESET, VIEW_AGC_CYCLE, VIEW_ZOOM_TOGGLE,
                               VIEW_GAIN, VIEW_DN1M, VIEW_STEP_DOWN, VIEW_STEP, VIEW_UP1M, VIEW_PRESETS, VIEW_PLAY, VIEW_RECORD};
            for (int k = 0; k < 12; k++) {
                int sy = k < 5 || k >= 9 ? y : LCD_H - 22, px, py, sx, yy;
                waveshare_screen_to_panel(rotation, xs[k], sy, &px, &py);
                waveshare_panel_to_screen(rotation, px, py, &sx, &yy);
                assert(view_button_at(sx, yy) == ids[k]);
                screen_idle_t idle = {.asleep = true};
                assert(screen_idle_update(&idle, 60, 1, true) & SCREEN_CONSUME);
                assert(screen_idle_update(&idle, 60, 2, false) & SCREEN_CONSUME);
                assert(screen_idle_update(&idle, 60, 50002, true) & SCREEN_CONSUME);
                assert(screen_idle_update(&idle, 60, 60000, false) & SCREEN_CONSUME);
                assert(screen_idle_update(&idle, 60, 160000, false) & SCREEN_CONSUME);
                assert(!(screen_idle_update(&idle, 60, 160001, true) & SCREEN_CONSUME));
            }
            /* Each selector half has independent pressed feedback and cached redraw. */
            view_audio_controls(5, 2, 4, 0, -1);
            uint32_t footer = crc32(0, (const Bytef *)(fb + (LCD_H - 44) * LCD_W), LCD_W * 44 * sizeof fb[0]);
            view_audio_controls(5, 2, 4, 0, VIEW_STEP_DOWN);
            assert(crc32(0, (const Bytef *)(fb + (LCD_H - 44) * LCD_W), LCD_W * 44 * sizeof fb[0]) != footer);
            view_audio_controls(5, 2, 4, 0, VIEW_STEP);
            assert(crc32(0, (const Bytef *)(fb + (LCD_H - 44) * LCD_W), LCD_W * 44 * sizeof fb[0]) != footer);
            assert(view_button_at(LCD_W / 2, LCD_H - 22) == -1);
            assert(view_button_at(-1, y) == -1 && view_button_at(LCD_W, y) == -1);
            view_header(142000100, "AGC", -1);
            uint32_t baseline = crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]);
            view_header(142000100, "AGC", VIEW_AGC_CYCLE);
            assert(crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]) != baseline);
            view_header(142000100, "AGC", VIEW_RANGE_RESET);
            assert(crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]) != baseline);
            /* The header ZOOM key shows the active preset and has pressed feedback. */
            view_set_zoom_cycle(0);
            view_header(142000100, "AGC", -1);
            uint32_t zoom0 = crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]);
            view_set_zoom_cycle(3);
            view_header(142000100, "AGC", -1);
            assert(crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]) != zoom0); /* label follows the preset */
            uint32_t zoom3 = crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]);
            view_header(142000100, "AGC", VIEW_ZOOM_TOGGLE);
            assert(crc32(0, (const Bytef *)fb, LCD_W * 54 * sizeof fb[0]) != zoom3); /* pressed feedback */
            view_set_zoom_cycle(0);
            view_header(142000100, "AGC", -1);
        }
    }
#endif
    puts("top-left TOUCH/GAIN/ZOOM/RANGE keys and inert frequency in every layout/look");
}

static void test_range_reset_waterfall(void)
{
    static float db[1024];
    view_init(RATE);
    for (int portrait = 0; portrait < 2; portrait++) {
        rpi_lcd_set_portrait(portrait != 0);
        view_set_theme(0);
        view_set_range(true, -20, 60);
        for (int i = 0; i < 1024; i++) db[i] = -70;
        for (int i = 0; i < 55; i++) view_render(db, 1024, 142000000);
        view_blit_waterfall();
        int y0 = view_spectrum_bottom() + 24; /* all looks have axis <= 24 px */
        int nonzero = 0;
        for (int y = y0; y < y0 + 16; y++)
            for (int x = 0; x < LCD_W; x++) nonzero += FB(x, y) != 0;
        assert(nonzero > 0); /* actual previously rendered waterfall pixels */
        view_reset();
        view_blit_waterfall(); /* new frames must not bring back old rows */
        for (int y = y0; y < y0 + 16; y++)
            for (int x = 0; x < LCD_W; x++) assert(FB(x, y) == 0);
        view_render(db, 1024, 142000000);
        view_blit_waterfall();
        for (int y = y0 + 2; y < y0 + 16; y++)
            for (int x = 0; x < LCD_W; x++) assert(FB(x, y) == 0);
    }
    puts("range reset clears stored waterfall lines, not just the visible framebuffer");
}


#ifdef CONFIG_SDR_WAVESHARE_43
static int sd_panel_click(ctl_t *ct,int x,int y) {
    int result=panel_touch(ct,PANEL_PRESS,x,y,0);
    result|=panel_touch(ct,PANEL_RELEASE,x,y,1);
    return result;
}
#endif
static void test_sd_audio_panel(void) {
#ifdef CONFIG_SDR_WAVESHARE_43
    view_init(RATE);panel_init();
    for(int rot=0;rot<4;rot++) {
        bool portrait=!(rot&1);
        rpi_lcd_set_portrait(portrait);
#ifdef CONFIG_SDR_WAVESHARE_43
        rpi_lcd_rotation=rot;
#endif
        for(int look=0;look<view_themes();look++) {
            view_set_theme(look);
            ctl_t ct={.gain_label=gain_label,.gain_tenths=gain_tenths,.audio_file=-1};
            ct.sd_audio.count=5;
            for(int i=0;i<5;i++)snprintf(ct.sd_audio.files[i],SD_AUDIO_NAME,"RX%06d.WAV",i+1);
            snprintf(ct.sd_audio.status,sizeof ct.sd_audio.status,"Synthetic host preview - not hardware");
            panel_open_recordings(&ct);
            int by=view_tune_y()+56,bw=LCD_W-12,cw=(bw-18)/4;
            int y=by+22;
            assert(sd_panel_click(&ct,6+cw/2,y)==CTL_SD_REFRESH);
            assert(sd_panel_click(&ct,6+(cw+6)+cw/2,y)==0); /* capture is main-header only */
            assert(sd_panel_click(&ct,6+2*(cw+6)+cw/2,y)==0); /* no selection */
            int bh=LCD_H-view_tune_y()-62;
            int row_h=(bh-88-92-18)/4;
            int gap_y=by+44+6+92+6+row_h-6+1;
            uint16_t clean_gap[800];
            ct.sd_audio.count=0;panel_redraw(&ct);
            for(int xx=0;xx<LCD_W;xx++) clean_gap[xx]=FB(xx,gap_y);
            ct.sd_audio.count=5;panel_redraw(&ct);
            for(int xx=0;xx<LCD_W;xx++) assert(clean_gap[xx]==FB(xx,gap_y));
            assert(sd_panel_click(&ct,LCD_W/2,by+44+6+92+6+row_h/2)==0);
            assert(ct.audio_file==0);
            assert(!strcmp(ct.audio_selected_name,"RX000001.WAV"));
            assert(sd_panel_click(&ct,6+2*(cw+6)+cw/2,y)==CTL_SD_PLAY);
            assert(sd_panel_click(&ct,6+3*(cw+6)+cw/2,y)==CTL_SD_STOP);
            const char *long_name="261116-220530_4294.967295M_NFM_1234567890123456789012345678.wav";
            snprintf(ct.sd_audio.files[0],SD_AUDIO_NAME,"%s",long_name);panel_redraw(&ct);
            sd_panel_click(&ct,LCD_W/2,by+44+6+92+6+row_h/2);
            /* Delete is a separate explicit confirm/cancel flow, with a frozen
             * full basename even if the worker refreshes the selected row. */
            assert(sd_panel_click(&ct,6+(cw+6)+cw/2,y)==0);
            assert(!strcmp(ct.audio_delete_name,long_name));
            assert(sd_panel_click(&ct,LCD_W/4,by+122)==0); /* CANCEL */
            assert(sd_panel_click(&ct,6+(cw+6)+cw/2,y)==0);
            snprintf(ct.sd_audio.files[0],SD_AUDIO_NAME,"RX000099.WAV");
            assert(sd_panel_click(&ct,3*LCD_W/4,by+122)==CTL_SD_DELETE);
            assert(!strcmp(ct.audio_delete_name,long_name));
            ct.sd_audio.state=SD_AUDIO_RECORDING;panel_redraw(&ct);
            assert(sd_panel_click(&ct,6+(cw+6)+cw/2,y)==0); /* cannot double-start */
            assert(sd_panel_click(&ct,6+3*(cw+6)+cw/2,y)==CTL_SD_STOP);
            assert(sd_panel_click(&ct,50,view_tune_y()+28)==CTL_SD_STOP); /* BACK stops */
            assert(!panel_recordings_visible());
            panel_open_presets(&ct); /* reuse the BAND selection routing */
            int cols=portrait?2:4,rows=portrait?5:3;
            int ch=(bh-54-6*(rows-1))/rows;
            int w=(bw-6*(cols-1))/cols;
            assert(sd_panel_click(&ct,6+w/2,by+ch/2)&CTL_BAND);
        }
    }
    puts("SD panel: select/play/REC/STOP, disabled busy controls, BACK stop, existing presets; five themes/four rotations");
#endif
}

static void test_hackrf_header(const char *dir) {
    (void)dir;
#ifdef CONFIG_SDR_BACKEND_HACKRF
    view_init(RATE);
    for(int rot=0;rot<4;rot++) {
        rpi_lcd_rotation=rot; rpi_lcd_set_portrait(!(rot&1));
        for(int t=0;t<view_themes();t++) {
            view_set_theme(t); view_set_fine_tune(true);
            view_set_stage_gains(40,62,true,true);
            view_header(100000100,"L40 V62",-1);
            int ids[]={VIEW_LNA_DN,VIEW_LNA_UP,VIEW_VGA_DN,VIEW_VGA_UP};
            int count[4]={0};
            uint16_t before[800*160];
            memcpy(before,fb,LCD_W*view_tune_y()*sizeof fb[0]);
            view_header(1765999900,"L40 V62",-1);
            for(int y=0;y<view_tune_y();y++) for(int x=0;x<LCD_W;x++) {
                int id=view_button_at(x,y); assert(id!=VIEW_AGC_CYCLE);
                if (id>=0 || (x<288 && y>=(t==1?54:40) && y<(t==1?94:80)) || x>=LCD_W-150)
                                    assert(FB(x,y)==before[y*LCD_W+x]);
                for(int k=0;k<4;k++) if(id==ids[k]) {
                    count[k]++; int px,py,sx,sy;
                    waveshare_screen_to_panel(rot,x,y,&px,&py);
                    waveshare_panel_to_screen(rot,px,py,&sx,&sy);
                    assert(view_button_at(sx,sy)==id);
                }
            }
            for(int k=0;k<4;k++) {
                assert(count[k]>500 && !view_header_repeats(ids[k]));
                uint32_t a=crc32(0,(Bytef*)fb,LCD_W*view_tune_y()*sizeof fb[0]);
                view_header(1765999900,"L40 V62",ids[k]);
                assert(a!=crc32(0,(Bytef*)fb,LCD_W*view_tune_y()*sizeof fb[0]));
                view_header(1765999900,"L40 V62",-1);
                screen_idle_t idle={.asleep=true};
                assert(screen_idle_update(&idle,60,1,true)&SCREEN_CONSUME);
                assert(screen_idle_update(&idle,60,2,false)&SCREEN_CONSUME);
                assert(screen_idle_update(&idle,60,50002,true)&SCREEN_CONSUME);
                assert(screen_idle_update(&idle,60,60000,false)&SCREEN_CONSUME);
                assert(screen_idle_update(&idle,60,160000,false)&SCREEN_CONSUME);
                assert(!(screen_idle_update(&idle,60,160001,true)&SCREEN_CONSUME));
            }
            if(dir) {
                static float db[1024];
                view_axis(1765999900);
                for(int i=0;i<1024;i++) db[i]=-95+3*sinf(i*0.7f);
                for(int f=0;f<12;f++) view_render(db,1024,1765999900);
                view_blit(); view_audio_controls(5,2,0,0,-1);
                char path[512]; snprintf(path,sizeof path,"%s/hackrf-%s-rot%d.png",dir,view_theme_name(t),rot);
                write_png(path,fb,LCD_W,LCD_H);
            }
            memcpy(before,fb,LCD_W*view_tune_y()*sizeof fb[0]);
            view_set_stage_gains(40,62,false,true);
            view_header(1765999900,"L40 V62",-1);
            assert(memcmp(before,fb,LCD_W*view_tune_y()*sizeof fb[0]));
        }
    }
#endif
    puts("HackRF independent stage paint/hit/rotation/wake contracts PASS");
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--test-hackrf-header")) { test_hackrf_header(argc>2?argv[2]:NULL); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-sd-audio")) { test_sd_audio_panel(); return 0; }
    if (argc > 5 && !strcmp(argv[1], "--sd-snapshot")) {
        int look=atoi(argv[3]),rot=atoi(argv[4]);
        rpi_lcd_set_portrait(!(rot&1));
#ifdef CONFIG_SDR_WAVESHARE_43
        rpi_lcd_rotation=rot;
#endif
        view_init(RATE);panel_init();view_set_theme(look);
        view_set_recording(!strcmp(argv[5],"recording"));
        view_set_fine_tune(true);view_set_zoom_cycle(0);
        view_header(1720000100,"49.6 dB",-1);view_axis(1720000100);
        ctl_t ct={.gain_label=gain_label,.gain_tenths=gain_tenths,.audio_file=-1};
        snprintf(ct.sd_audio.status,sizeof ct.sd_audio.status,"Synthetic host preview - no card data");
        if(!strcmp(argv[5],"recordings") || !strcmp(argv[5],"delete")) {
            ct.sd_audio.count=3;ct.audio_file=0;
            snprintf(ct.audio_selected_name,SD_AUDIO_NAME,"261116-220530_99.1M_FM.wav");
            snprintf(ct.sd_audio.files[0],SD_AUDIO_NAME,"261116-220530_99.1M_FM.wav");
            snprintf(ct.sd_audio.files[1],SD_AUDIO_NAME,"unsynced_7.1001M_USB_999999.wav");
            snprintf(ct.sd_audio.files[2],SD_AUDIO_NAME,"RX000001.WAV");
            panel_open_recordings(&ct);
#ifdef CONFIG_SDR_WAVESHARE_43
            if(!strcmp(argv[5],"delete")) {
                int cw=(LCD_W-30)/4;
                sd_panel_click(&ct,6+(cw+6)+cw/2,view_tune_y()+78);
            }
#endif
        }
        else view_message("Synthetic host preview", "No live RF or battery telemetry");
        /* view_message clears header, so redraw it for the receiver preview. */
        view_header(1720000100,"49.6 dB",-1);
        write_png(argv[2],fb,LCD_W,LCD_H);return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "--settings-snapshot")) {
        rpi_lcd_set_portrait(argc > 3 && !strcmp(argv[3], "--portrait"));
        view_init(RATE); panel_init();
        ctl_t ct = {.gain_n = 30, .gain_label = gain_label, .gain_tenths = gain_tenths, .screen_timeout = 60};
        panel_open(&ct);
        int cols = LCD_PORTRAIT ? 2 : 4, rows = LCD_PORTRAIT ? 5 : 3;
        int cw = (LCD_W - 12 - 6 * (cols - 1)) / cols;
        int ch = (LCD_H - view_tune_y() - 12 - 6 * (rows - 1)) / rows;
        int x = 6 + 8 % cols * (cw + 6) + cw / 2;
        int y = view_tune_y() + 6 + 8 / cols * (ch + 6) + ch / 2;
        panel_touch(&ct, PANEL_PRESS, x, y, 0);
        panel_touch(&ct, PANEL_RELEASE, x, y, 0);
        write_png(argv[2], fb, LCD_W, LCD_H);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "--sensor-snapshot")) {
        rpi_lcd_set_portrait(argc > 3 && !strcmp(argv[3], "--portrait"));
        view_init(RATE);
        view_set_theme(0);
        sensor_view_init();
        sensor_view_open();
        sensor_view_info_t in = {.wh2 = 1, .tpms = 3, .streaming = true,
                                 .rows = 2, .event = {"TPMS 1A4E 259.4 kPa 25 C", "WH2 D1 24.6 C 33%"}};
        if (argc > 4) {
            in.band = atoi(argv[4]);
            in.wh2 = in.tpms = in.rows = 0; /* band preview is not RF evidence */
            memset(in.event, 0, sizeof in.event);
        }
        sensor_view_draw(&in);
        write_png(argv[2], fb, LCD_W, LCD_H);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--test-bands")) { test_am_sw_cb_pages(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-theme")) { test_theme_header_disabled(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-audio")) { test_audio_controls(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-touch-tuning")) { test_touch_tuning(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-phosphor")) { test_phosphor_green(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-footer-recovery")) { test_footer_stream_recovery(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-rf")) { test_rf_settings(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-hf-diag")) { test_hf_diagnostic_page(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-ism")) { test_ism_bands(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-screen")) { test_screen_settings(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-sensors")) { test_sensor_entry(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-quick-header")) { test_quick_header(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--test-range-reset-waterfall")) { test_range_reset_waterfall(); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--audio-snapshot")) {
        static float db[1024];
        bool portrait = false, phosphor = false;
        int touch_mode = VIEW_TOUCH_FREE;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--portrait")) portrait = true;
            else if (!strcmp(argv[i], "--phosphor")) phosphor = true;
            else if (!strcmp(argv[i], "--snap")) touch_mode = VIEW_TOUCH_SNAP;
            else if (!strcmp(argv[i], "--peak")) touch_mode = VIEW_TOUCH_PEAK;
            else { fprintf(stderr, "unknown snapshot option: %s\n", argv[i]); return 2; }
        }
        view_init(RATE);
        if (portrait) rpi_lcd_set_portrait(true);
        view_set_theme(phosphor ? 2 : 0);
        view_set_touch_mode(touch_mode);
        view_set_passband(3000);
        view_set_sideband(1);
        view_set_fine_tune(true);
        view_set_audio("USB 5", true);
        view_header(142000100, "AGC", -1);
        view_axis(142000100);
        for (int frame = 0; frame < 80; frame++) {
            for (int i = 0; i < 1024; i++)
                db[i] = -89 + 3 * sin(i * 0.7 + frame * 0.4) +
                        (i > 510 && i < 514 ? 30 : 0);
            view_render(db, 1024, 142000100);
        }
        view_blit();
        view_audio_controls(5, 2, 4, 0, -1);
        write_png(argv[2], fb, LCD_W, LCD_H);
        printf("%s\n", argv[2]);
        return 0;
    }
    const char *dir = argc > 1 ? argv[1] : ".";
    uint32_t freq = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 100000000;
    int pressed = argc > 3 ? atoi(argv[3]) : -1;
    render_all(dir, "", freq, pressed);
    rpi_lcd_set_portrait(true);
    render_all(dir, "portrait-", freq, pressed);
    return 0;
}
