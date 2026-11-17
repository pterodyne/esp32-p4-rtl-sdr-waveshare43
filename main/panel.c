#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rpi_lcd.h"
#include "rpi_text.h"
#include "rpi_canvas.h"
#include "pins.h"
#include "view.h"
#include "panel.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define BIG_ALLOC(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM)
#else
#define BIG_ALLOC(n) calloc(1, (n))
#endif

/* ------------------------------------------------------------------------- tables -- */

static const uint32_t s_zoom_hz[PANEL_ZOOMS] = {2400000, 1200000, 600000, 300000};
static const char *s_zoom_lbl[PANEL_ZOOMS] = {"2.4 MHz", "1.2 MHz", "600 kHz", "300 kHz"};
static const char *s_zoom_sub[PANEL_ZOOMS] = {"whole span", "2x", "4x", "8x"};
/* Keep indexes 0..3 stable: they are persisted in NVS and channel-memory blobs. */
static const uint32_t s_bw_hz[PANEL_BWS] = {200000, 25000, 12500, 8000, 3000, 3000};
static const char *s_bw_sub[PANEL_BWS] = {"200k FM radio", "25k wide", "12.5k narrow", "8k airband",
                                          "3k upper side", "3k lower side"};
static const char *s_bw_mode[PANEL_BWS] = {"WFM", "NFM", "NFM", "AM", "USB", "LSB"};

typedef struct { const char *name, *sub; uint32_t freq; int bw, zoom; } band_t;
static const band_t s_band[PANEL_BANDS] = {
    {"FM", "98.0", 98000000, 0, 0},        {"AIR", "121.5", 121500000, 3, 0},
    {"MARINE", "156.8", 156800000, 1, 1},  {"NOAA", "162.475", 162475000, 1, 2},
    {"2M", "145.0", 145000000, 2, 0},      {"PMR", "446.1", 446100000, 2, 2},
    {"433/70CM", "433.92", 433920000, 1, 0}, {"ADS-B", "1090", 1090000000, 0, 0},
    {"315 RX", "Part 15", 315000000, 2, 2},
    {"345 RX", "Part 15", 345000000, 2, 2},
    {"915 ISM", "902-928", 915000000, 1, 1},
    {"AM 600", "broadcast", 600000, 3, 2},
    {"AM 1000", "broadcast", 1000000, 3, 2},
    {"AM 1600", "broadcast", 1600000, 3, 2},
    {"SW 3.95", "AM broadcast", 3950000, 3, 3},
    {"SW 4.90", "AM broadcast", 4900000, 3, 3},
    {"SW 6.05", "AM broadcast", 6050000, 3, 3},
    {"SW 7.25", "AM broadcast", 7250000, 3, 3},
    {"SW 9.65", "AM broadcast", 9650000, 3, 3},
    {"SW 11.85", "AM broadcast", 11850000, 3, 3},
    {"SW 13.70", "AM broadcast", 13700000, 3, 3},
    {"SW 15.35", "AM broadcast", 15350000, 3, 3},
    {"SW 17.70", "AM broadcast", 17700000, 3, 3},
    {"SW 21.60", "AM broadcast", 21600000, 3, 3},
    {"CB 1 AM", "26.965", 26965000, 3, 3},
    {"CB 9 AM", "27.065", 27065000, 3, 3},
    {"CB 19 AM", "27.185", 27185000, 3, 3},
    {"CB 38 LSB", "27.385", 27385000, 5, 3},
    {"CB 38 USB", "27.385", 27385000, 4, 3},
    {"CB 40 AM", "27.405", 27405000, 3, 3},
};

uint32_t panel_zoom_hz(int i) { return s_zoom_hz[i >= 0 && i < PANEL_ZOOMS ? i : 0]; }
uint32_t panel_bw_hz(int i) { return s_bw_hz[i >= 0 && i < PANEL_BWS ? i : 0]; }
const char *panel_bw_mode(int i) { return s_bw_mode[i >= 0 && i < PANEL_BWS ? i : 0]; }

const char *panel_band(int i, uint32_t *freq, int *bw, int *zoom)
{
    const band_t *b = &s_band[i >= 0 && i < PANEL_BANDS ? i : 0];
    *freq = b->freq; *bw = b->bw; *zoom = b->zoom;
    return b->name;
}

/* --------------------------------------------------------------------------- keys -- */

static bool s_delete_confirm;
enum { P_MENU, P_GAIN, P_RANGE, P_ZOOM, P_BW, P_BAND, P_SCAN, P_LOOK, P_MEM, P_SAVE, P_RF, P_SCREEN, P_DIAG, P_RECORDINGS };
static const char *s_title[] = {"CONTROLS", "GAIN", "RANGE", "ZOOM", "AUDIO", "BAND", "SCAN", "LOOK", "MEMORY", "SAVE TO", "RF INPUT", "SCREEN TIMEOUT", "HF DIAG", "SD AUDIO WAV"};
#define LOOKS 5
static const char *s_look_sub[LOOKS] = {"clean blue", "Mayhem style", "green CRT", "walnut and amber", "synthwave"};

enum {
    K_LABEL = -1,                            /* text only, not touchable */
    K_BACK, K_DONE, K_WIFI, K_BLE, K_TURN, K_TILE,   /* K_TILE + page: menu tiles */
    K_GAIN_DN = K_TILE + 12, K_GAIN_UP, K_GAIN_P,   /* K_GAIN_P + 0..3: presets */
    K_TOP_DN = K_GAIN_P + 4, K_TOP_UP, K_SPAN_DN, K_SPAN_UP, K_AUTO,
    K_ZOOM, K_BW = K_ZOOM + PANEL_ZOOMS, K_BAND = K_BW + PANEL_BWS, K_LOOK = K_BAND + PANEL_BANDS,
    K_VOL_DN = K_LOOK + LOOKS, K_VOL_UP, K_SQL_DN, K_SQL_UP,
    K_MEMORY, K_SAVE, K_MSCAN, K_SLOT,      /* K_SLOT + 0..7: memory slots */
    K_DIRECT = K_SLOT + PANEL_MEMS, K_BIAS_OFF = K_DIRECT + 3, K_BIAS_ON,
    K_PAGE_PREV, K_PAGE_NEXT, K_SENSOR, K_SCREEN, K_HF_DIAG = K_SCREEN + 4,
    K_SD_DELETE, K_SD_CANCEL, K_SD_CONFIRM, K_SD_REFRESH, K_SD_RECORD, K_SD_PLAY, K_SD_STOP, K_SD_PREV, K_SD_NEXT, K_SD_FILE,
};

typedef struct {
    int x, y, w, h, id;
    char l1[16], l2[80];                     /* l1 large, l2 small underneath */
    bool sel, rep, big, disabled;            /* selected, repeats, 2x title, or read-only */
} pkey_t;

#define MAXK 16
#define GAP 6
static pkey_t s_k[MAXK];
static int s_nk, s_page, s_band_page, s_audio_page, s_hot = -1; /* selected BAND page survives redraw */
static int64_t s_next_rep;
static uint16_t *s_px;
static int s_h;                               /* panel height */
static view_colors_t K;
static hf_diag_state_t s_diag_snapshot;

void panel_init(void)
{
    s_px = BIG_ALLOC(LCD_W * LCD_H * 2);
    if (!s_px) abort();
}

static pkey_t *add(int x, int y, int w, int h, int id, const char *l1, const char *l2)
{
    pkey_t *k = &s_k[s_nk++];
    memset(k, 0, sizeof *k);
    k->x = x; k->y = y; k->w = w; k->h = h; k->id = id;
    snprintf(k->l1, sizeof k->l1, "%s", l1 ? l1 : "");
    snprintf(k->l2, sizeof k->l2, "%s", l2 ? l2 : "");
    return k;
}

/* cols x rows cells over a rectangle, GAP apart; returns the first key */
static pkey_t *grid(int x, int y, int w, int h, int cols, int rows, int i, int id, const char *l1, const char *l2)
{
    int cw = (w - (cols - 1) * GAP) / cols, ch = (h - (rows - 1) * GAP) / rows;
    return add(x + (i % cols) * (cw + GAP), y + (i / cols) * (ch + GAP), cw, ch, id, l1, l2);
}

static void gain_text(const ctl_t *c, char *b, size_t n)
{
    snprintf(b, n, "%s", c->gain_idx ? c->gain_label(c->gain_idx) : "AGC");
}

/* three keys on a row: [-] value [+] */
static void stepper(int y, int h, int dn, int up, const char *value, const char *caption)
{
    const int kw = LCD_PORTRAIT ? 80 : 120;
    add(GAP, y, kw, h, dn, "-", NULL)->rep = true;
    pkey_t *v = add(GAP + kw + GAP, y, LCD_W - 4 * GAP - 2 * kw, h, K_LABEL, value, caption);
    v->big = true;
    add(LCD_W - GAP - kw, y, kw, h, up, "+", NULL)->rep = true;
}

static void build(const ctl_t *c)
{
    s_nk = 0;
    char v[24];
    /* portrait turns the wide grids on their side */
    const bool p = LCD_PORTRAIT;
    const int c4 = p ? 2 : 4, r2 = p ? 4 : 2;
    if (s_page == P_MENU) {
        int y = GAP, h = s_h - 2 * GAP, w = LCD_W - 2 * GAP;
        int menu_rows = p ? 5 : 3;
        gain_text(c, v, sizeof v);
        grid(GAP, y, w, h, c4, menu_rows, 0, K_TILE + P_GAIN, "GAIN", v)->big = true;
        if (c->range_auto) snprintf(v, sizeof v, "auto");
        else snprintf(v, sizeof v, "%d/%d dB", c->ref_db, c->range_db);
        grid(GAP, y, w, h, c4, menu_rows, 1, K_TILE + P_RANGE, "RANGE", v)->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 2, K_TILE + P_ZOOM, "ZOOM", s_zoom_lbl[c->zoom])->big = true;
        if (c->vol) snprintf(v, sizeof v, "%s, vol %d", s_bw_mode[c->bw], c->vol);
        else snprintf(v, sizeof v, "%s, off", s_bw_mode[c->bw]);
        grid(GAP, y, w, h, c4, menu_rows, 3, K_TILE + P_BW, "AUDIO", v)->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 4, K_TILE + P_BAND, "BAND", c->band >= 0 ? s_band[c->band].name : "presets")->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 5, K_TILE + P_SCAN, "SCAN", "Wi-Fi, BLE, sensors")->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 6, K_TILE + P_LOOK, "LOOK", view_theme_name(view_theme()))->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 7, K_TILE + P_RF, "RF INPUT",
             c->bias_error ? (c->bias_forced ? "BIAS ? EEPROM ON" : "BIAS ? RETRY OFF")
                           : c->bias ? "BIAS-T ON" : "bias off")->big = true;
#ifdef CONFIG_SDR_WAVESHARE_43
        grid(GAP, y, w, h, c4, menu_rows, 8, K_TILE + P_SCREEN, "SETTINGS", "screen timeout")->big = true;
        grid(GAP, y, w, h, c4, menu_rows, 9, K_DONE, "DONE", "to radio")->big = true;
#else
        grid(GAP, y, w, h, c4, menu_rows, 8, K_DONE, "DONE", "to radio")->big = true;
#endif
        return;
    }
    /* sub page: BACK + title, then the body */
    const int bar = 44, by = GAP + bar + GAP, bh = s_h - by - GAP, bw = LCD_W - 2 * GAP;
    char band_title[24];
    if (s_page == P_BAND) {
        int page_size = p ? 10 : 12;
        int pages = (PANEL_BANDS + page_size - 1) / page_size;
        if (s_band_page >= pages) s_band_page = pages - 1;
        snprintf(band_title, sizeof band_title, "BAND %d/%d", s_band_page + 1, pages);
    }
    add(GAP, GAP, 120, bar, K_BACK, "< BACK", NULL)->big = true;
    const int scan_w = (s_page == P_MEM || s_page == P_RF) ? 120 + GAP : 0;
    add(GAP + 120 + GAP, GAP, LCD_W - 3 * GAP - 120 - scan_w, bar, K_LABEL,
        s_page == P_BAND ? band_title : s_title[s_page], NULL)->big = true;
    if (s_page == P_MEM) add(LCD_W - GAP - 120, GAP, 120, bar, K_MSCAN, "SCAN", NULL)->big = true;
    if (s_page == P_RF) add(LCD_W - GAP - 120, GAP, 120, bar, K_HF_DIAG, "HF DIAG", NULL)->big = true;
    switch (s_page) {
    case P_RECORDINGS: {
        if(s_delete_confirm) {
            add(GAP,by,bw,40,K_LABEL,"DELETE FILE?",NULL);
            add(GAP,by+40,bw,40,K_LABEL,"",c->audio_delete_name);
            grid(GAP,by+100,bw,44,2,1,0,K_SD_CANCEL,"CANCEL",NULL);
            grid(GAP,by+100,bw,44,2,1,1,K_SD_CONFIRM,"YES DELETE",NULL);
            break;
        }
        int nav_h=44, status_h=92, row_h=(bh-nav_h*2-status_h-3*GAP)/4;
        const char *titles[]={"REFRESH","DELETE","PLAY","STOP"};
        const int ids[]={K_SD_REFRESH,K_SD_DELETE,K_SD_PLAY,K_SD_STOP};
        for(int i=0;i<4;i++) {
            pkey_t *key=grid(GAP,by,bw,nav_h,4,1,i,ids[i],titles[i],NULL);
            key->disabled=(i==0 && c->sd_audio.state!=SD_AUDIO_IDLE) ||
                (i==1 && (c->sd_audio.state==SD_AUDIO_RECORDING || c->sd_audio.record_requested)) ||
                (i==2 && c->sd_audio.state!=SD_AUDIO_IDLE) ||
                ((i==1 || i==2) && (c->audio_file<0 || c->audio_file>=c->sd_audio.count));
            key->sel=(i==1 && c->sd_audio.state==SD_AUDIO_RECORDING) || (i==2 && c->sd_audio.state==SD_AUDIO_PLAYING);
        }
        int sy=by+nav_h+GAP;
        add(GAP,sy,bw,40,K_LABEL,"DEMOD AUDIO", "48k mono WAV - NOT raw IQ");
        add(GAP,sy+40,bw,26,K_LABEL,"",c->sd_audio.status);
        add(GAP,sy+66,bw,26,K_LABEL,"",c->sd_audio.name);
        int first=s_audio_page*4;
        for(int i=0;i<4 && first+i<c->sd_audio.count;i++) {
            int at=first+i;
            pkey_t *key=add(GAP,sy+status_h+GAP+i*row_h,bw,row_h-GAP,K_SD_FILE+i,
                "",c->sd_audio.files[at]);
            key->sel=at==c->audio_file;
        }
        int ny=by+bh-nav_h;
        grid(GAP,ny,bw,nav_h,2,1,0,K_SD_PREV,"< PREV",NULL)->disabled=s_audio_page==0 && c->sd_audio.batch==0;
        grid(GAP,ny,bw,nav_h,2,1,1,K_SD_NEXT,"NEXT >",NULL)->disabled=first+4>=c->sd_audio.count && !c->sd_audio.more;
        break;
    }
    case P_DIAG: break; /* read-only lines are drawn below the title */
    case P_SCREEN: {
        static const char *labels[] = {"30 SECONDS", "60 SECONDS", "90 SECONDS", "NEVER"};
        static const int seconds[] = {30, 60, 90, 0};
        for (int i = 0; i < 4; i++)
            grid(GAP, by, bw, bh, 2, 2, i, K_SCREEN + i, labels[i],
                 i == 3 ? "always on" : "tap to wake")->sel = c->screen_timeout == seconds[i];
        break;
    }
    case P_GAIN: {
        int h1 = (bh - GAP) * 55 / 100;
        gain_text(c, v, sizeof v);
        stepper(by, h1, K_GAIN_DN, K_GAIN_UP, v, c->gain_idx ? "tuner gain" : "tuner AGC");
        static const char *pl[4] = {"AGC", "15 dB", "30 dB", "MAX"};
        int t = c->gain_tenths(c->gain_idx);
        for (int i = 0; i < 4; i++) {
            pkey_t *k = grid(GAP, by + h1 + GAP, bw, bh - h1 - GAP, p ? 2 : 4, p ? 2 : 1, i, K_GAIN_P + i, pl[i], NULL);
            k->sel = (i == 0 && c->gain_idx == 0) || (i == 3 && c->gain_idx == c->gain_n - 1 && c->gain_n > 1) ||
                     (i == 1 && t == 150) || (i == 2 && t == 300);
        }
        break;
    }
    case P_RANGE: {
        int h = (bh - 2 * GAP) / 3;
        snprintf(v, sizeof v, "%d dBFS", c->ref_db);
        stepper(by, h, K_TOP_DN, K_TOP_UP, v, c->range_auto ? "top (auto now)" : "top of scale");
        snprintf(v, sizeof v, "%d dB", c->range_db);
        stepper(by + h + GAP, h, K_SPAN_DN, K_SPAN_UP, v, c->range_auto ? "span (auto now)" : "scale height");
        add(GAP, by + 2 * (h + GAP), bw, h, K_AUTO, "AUTO", c->range_auto ? "follows the noise floor" : "off: tap to follow the floor")
            ->sel = c->range_auto;
        break;
    }
    case P_ZOOM:
        for (int i = 0; i < PANEL_ZOOMS; i++)
            grid(GAP, by, bw, bh, 2, 2, i, K_ZOOM + i, s_zoom_lbl[i], s_zoom_sub[i])->sel = i == c->zoom;
        break;
    case P_RF: {
        int half = (bh - GAP) / 2;
        pkey_t *off = grid(GAP, by, bw, half, 2, 1, 0, K_BIAS_OFF, "BIAS-T OFF",
                           c->bias_error ? (c->bias_forced ? "EEPROM FORCES ON" : "POWER ? RETRY OFF")
                                         : "no antenna power");
        off->sel = !c->bias && !c->bias_error; off->disabled = c->bias_forced;
        grid(GAP, by, bw, half, 2, 1, 1, K_BIAS_ON, "BIAS-T ON", "4.5V ACTIVE LNA ONLY")
            ->sel = c->bias && !c->bias_error;
        static const char *names[3] = {"TUNER", "ADC I", "ADC Q"};
        for (int i = 0; i < 3; i++) {
            pkey_t *k = grid(GAP, by + half + GAP, bw, bh - half - GAP, 3, 1, i,
                              K_DIRECT + i, names[i],
                              i && !c->direct_supported ? "V4: USE TUNER" : i ? "wired ADC only" : "V4 HF mixer/auto");
            k->sel = i == c->direct;
            k->disabled = i && !c->direct_supported;
        }
        break;
    }
    case P_BW: {                             /* filter and mode, then volume and squelch */
        int hm = p ? (bh - 3 * GAP) / 2 : (bh - 2 * GAP) * 40 / 100, hs = (bh - hm - 2 * GAP) / 2;
        for (int i = 0; i < PANEL_BWS; i++)
            grid(GAP, by, bw, hm, p ? 2 : 3, 2 + p, i, K_BW + i, s_bw_mode[i], s_bw_sub[i])->sel = i == c->bw;
        snprintf(v, sizeof v, c->vol ? "%d" : "OFF", c->vol);
        stepper(by + hm + GAP, hs, K_VOL_DN, K_VOL_UP, v, "volume");
        snprintf(v, sizeof v, c->sql ? "%d" : "OPEN", c->sql);
        stepper(by + hm + GAP + hs + GAP, hs, K_SQL_DN, K_SQL_UP, v, c->sql ? "squelch" : "squelch off");
        break;
    }
    case P_BAND: {
        /* Keep indexes stable across pagination: NVS and memory entries use the old slots. */
        int rows = p ? 5 : 3, page_size = c4 * rows;
        int first = s_band_page * page_size, last = first + page_size;
        if (last > PANEL_BANDS) last = PANEL_BANDS;
        int grid_h = bh - 54;
        for (int i = first; i < last; i++)
            grid(GAP, by, bw, grid_h, c4, rows,
                 i - first, K_BAND + i, s_band[i].name, s_band[i].sub)->sel = i == c->band;
        int half = (bw - GAP) / 2, nav_y = by + grid_h + GAP;
        add(GAP, nav_y, half, 48, K_PAGE_PREV, "< PREV", NULL)->disabled = s_band_page == 0;
        add(GAP + half + GAP, nav_y, bw - half - GAP, 48,
            K_PAGE_NEXT, "NEXT >", NULL)->disabled = last == PANEL_BANDS;
        break;
    }
    case P_SCAN:                             /* C6 scans and a foreground RTL sensor decoder */
        grid(GAP, by, bw, bh, 2, 3, 0, K_WIFI, "WIFI", p ? "networks" : "access points, channels")->big = true;
        grid(GAP, by, bw, bh, 2, 3, 1, K_BLE, "BLE", "nearby devices")->big = true;
        grid(GAP, by, bw, bh, 2, 3, 2, K_SAVE, "SAVE", "keep this channel")->big = true;
        grid(GAP, by, bw, bh, 2, 3, 3, K_MEMORY, "MEMORY", "tune or scan")->big = true;
        grid(GAP, by, bw, bh, 2, 3, 4, K_SENSOR, "SENSORS", "WH2 / Steelmate")->big = true;
        break;
    case P_MEM:
    case P_SAVE:
        for (int i = 0; i < PANEL_MEMS; i++) {
            char f[16], sub[24];
            if (c->mem_hz[i]) {
                if (c->mem_bw[i] == 4 || c->mem_bw[i] == 5)
                    snprintf(f, sizeof f, "%" PRIu32 ".%03" PRIu32 "%01" PRIu32,
                             c->mem_hz[i] / 1000000, c->mem_hz[i] / 1000 % 1000, c->mem_hz[i] / 100 % 10);
                else snprintf(f, sizeof f, "%" PRIu32 ".%03" PRIu32, c->mem_hz[i] / 1000000, c->mem_hz[i] / 1000 % 1000);
                snprintf(sub, sizeof sub, "M%d %s", i + 1, panel_bw_mode(c->mem_bw[i]));
            } else {
                snprintf(f, sizeof f, "M%d", i + 1);
                snprintf(sub, sizeof sub, "empty");
            }
            grid(GAP, by, bw, bh, c4, r2, i, K_SLOT + i, f, sub)->sel = c->mem_hz[i] && c->mem_hz[i] == c->freq;
        }
        break;
    case P_LOOK:
        for (int i = 0; i < LOOKS && i < view_themes(); i++) {
            char up[16];
            snprintf(up, sizeof up, "%s", view_theme_name(i));
            for (char *p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            pkey_t *k = grid(GAP, by, bw, bh, 2, 3, i, K_LOOK + i, up, s_look_sub[i]);
            k->big = true;
            k->sel = i == view_theme();
        }
#ifdef CONFIG_SDR_WAVESHARE_43
        static const char *const turn_sub[4] = {
            "next: 90 DEG", "next: 180 DEG", "next: 270 DEG", "next: 0 DEG"
        };
        grid(GAP, by, bw, bh, 2, 3, 5, K_TURN, "TURN", turn_sub[LCD_ROTATION])->big = true;
#else
        grid(GAP, by, bw, bh, 2, 3, 5, K_TURN, "TURN", p ? "back to landscape" : "portrait, USB up")->big = true;
#endif
        break;
    }
}

/* ------------------------------------------------------------------------- drawing -- */

static void draw_key(const rpi_canvas_t *c, const pkey_t *k, bool hot)
{
    int r = K.square ? 0 : 8;
    bool label = k->id == K_LABEL;
    uint16_t fill = k->sel ? K.btn_on : hot ? rpi_mix565(K.btn, K.btn_on, 110) : K.btn;
    uint16_t fg = k->sel ? K.txt_on : k->disabled ? K.dim : K.key_txt;
    uint16_t sub = rpi_mix565(fill, fg, 170);
    if (label) { fill = K.bg; fg = K.accent; sub = K.dim; }
    else {
        rpi_cv_rrect(c, k->x, k->y, k->w, k->h, r, hot ? K.btn_on : K.edge);
        rpi_cv_rrect(c, k->x + (hot ? 2 : 1), k->y + (hot ? 2 : 1), k->w - (hot ? 4 : 2), k->h - (hot ? 4 : 2),
                     r > 2 ? r - 2 : 0, fill);
    }
    if(!k->l1[0]) {
        /* A filename-only row has no invisible title above it. Keep its pixels
         * inside the key; all truncation is local display text, never identity. */
        char text[sizeof k->l2];snprintf(text,sizeof text,"%s",k->l2);
        size_t n=strlen(text);
        while(n && rpi_cv_text_w(text,1)>k->w-12) text[--n]=0;
        rpi_cv_text(c,k->x+(k->w-rpi_cv_text_w(text,1))/2,k->y+(k->h-16)/2,text,sub,1);
        return;
    }
    int sc = (k->big || k->h >= 50) && rpi_cv_text_w(k->l1, 2) <= k->w - 12 ? 2 : 1;
    int th = 16 * sc + (k->l2[0] ? 18 : 0);
    int ty = k->y + (k->h - th) / 2;
    rpi_cv_text(c, k->x + (k->w - rpi_cv_text_w(k->l1, sc)) / 2, ty, k->l1, fg, sc);
    if (k->l2[0]) {
        int w = rpi_cv_text_w(k->l2, 1);
        rpi_cv_text(c, k->x + (k->w - w) / 2, ty + 16 * sc + 2, k->l2, sub, 1);
    }
}

static void render(void)
{
    rpi_canvas_t c = {s_px, LCD_W, s_h};
    rpi_cv_fill(&c, 0, 0, LCD_W, s_h, K.bg);
    for (int i = 0; i < s_nk; i++) draw_key(&c, &s_k[i], i == s_hot);
    if (s_page == P_DIAG) {
        char line[96];
        for (int i = 0; i < 10; i++)
            rpi_cv_text(&c, 12, 58 + i * 22, hf_diag_line(&s_diag_snapshot, i, line, sizeof line), K.txt, 1);
    }
}

static void blit_rows(int y0, int h)
{
    if (y0 < 0) { h += y0; y0 = 0; }
    if (y0 + h > s_h) h = s_h - y0;
    if (h > 0) rpi_lcd_blit(0, K.top + y0, LCD_W, h, s_px + y0 * LCD_W);
}

static void show_all(const ctl_t *c)
{
    s_diag_snapshot = c->hf_diag;
    view_colors(&K);
    s_h = LCD_H - K.top;
    build(c);
    render();
    blit_rows(0, s_h);
}

/* redraw and push only the rows of keys a and b (indexes, or -1) */
static void show_keys(int a, int b)
{
    render();
    int y0 = s_h, y1 = 0;
    for (int i = 0; i < 2; i++) {
        int k = i ? b : a;
        if (k < 0 || k >= s_nk) continue;
        if (s_k[k].y < y0) y0 = s_k[k].y;
        if (s_k[k].y + s_k[k].h > y1) y1 = s_k[k].y + s_k[k].h;
    }
    if (y1 > y0) blit_rows(y0, y1 - y0);
}

void panel_open(const ctl_t *c)
{
    s_page = P_MENU;
    s_band_page = 0;
    s_hot = -1;
    show_all(c);
}

void panel_open_presets(const ctl_t *c) {
    s_page=P_BAND;s_band_page=0;s_hot=-1;show_all(c);
}
void panel_open_recordings(const ctl_t *c) {
    s_delete_confirm=false;
    s_page=P_RECORDINGS;s_audio_page=0;s_hot=-1;show_all(c);
}
bool panel_recordings_visible(void) { return s_page==P_RECORDINGS; }
void panel_redraw(const ctl_t *c) { show_all(c); }

void panel_open_memory(const ctl_t *c, bool save)
{
    s_page = save ? P_SAVE : P_MEM;
    s_hot = -1;
    show_all(c);
}

/* -------------------------------------------------------------------------- touch -- */

static int hit(int x, int y)
{
    y -= K.top;
    for (int i = 0; i < s_nk; i++) {
        const pkey_t *k = &s_k[i];
        if (k->id == K_LABEL || k->disabled) continue;
        if (x >= k->x - GAP / 2 - 1 && x < k->x + k->w + GAP / 2 + 1 &&
            y >= k->y - GAP / 2 - 1 && y < k->y + k->h + GAP / 2 + 1) return i;
    }
    return -1;
}

static int nearest_gain(const ctl_t *c, int tenths)
{
    int best = 1;
    for (int i = 1; i < c->gain_n; i++)
        if (abs(c->gain_tenths(i) - tenths) < abs(c->gain_tenths(best) - tenths)) best = i;
    return best;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* run key id; returns CTL_* bits. *page_change is set when the page must be rebuilt. */
static int act(ctl_t *c, int id, bool *page_change)
{
    if(s_page==P_RECORDINGS) {
        if(s_delete_confirm) {
            if(id==K_SD_CONFIRM || id==K_SD_CANCEL || id==K_BACK) {
                s_delete_confirm=false;*page_change=true;
                return id==K_SD_CONFIRM ? CTL_SD_DELETE : 0;
            }
            return 0;
        }
        if(id==K_SD_DELETE && c->audio_file>=0 && c->audio_file<c->sd_audio.count) {
            snprintf(c->audio_delete_name,sizeof c->audio_delete_name,"%s",c->audio_selected_name);
            s_delete_confirm=true;*page_change=true;return 0;
        }
        if(id==K_SD_REFRESH) return CTL_SD_REFRESH;
        if(id==K_SD_RECORD) return CTL_SD_RECORD;
        if(id==K_SD_PLAY) return CTL_SD_PLAY;
        if(id==K_SD_STOP) return CTL_SD_STOP;
        if(id>=K_SD_FILE && id<K_SD_FILE+4) {
            c->audio_file=s_audio_page*4+id-K_SD_FILE;
            snprintf(c->audio_selected_name,sizeof c->audio_selected_name,"%s",c->sd_audio.files[c->audio_file]);
            *page_change=true;return 0;
        }
        if(id==K_SD_PREV && s_audio_page==0 && c->sd_audio.batch>0) return CTL_SD_PREV;
        if(id==K_SD_NEXT && (s_audio_page+1)*4>=c->sd_audio.count && c->sd_audio.more) return CTL_SD_NEXT;
        if(id==K_SD_PREV || id==K_SD_NEXT) {
            s_audio_page=clampi(s_audio_page+(id==K_SD_NEXT ? 1:-1),0,c->sd_audio.count ? (c->sd_audio.count-1)/4:0);
            *page_change=true;return 0;
        }
        if(id==K_BACK) {s_page=P_MENU;*page_change=true;return CTL_SD_STOP;}
    }
    if (id == K_BACK) { s_page = s_page == P_DIAG ? P_RF : P_MENU; *page_change = true; return 0; }
    if (id == K_HF_DIAG) { s_page = P_DIAG; *page_change = true; return 0; }
    if (id == K_DONE) return CTL_CLOSE;
    if (id == K_WIFI) return CTL_WIFI;
    if (id == K_BLE) return CTL_BLE;
    if (id == K_SENSOR) return CTL_SENSOR;
    if (id >= K_SCREEN && id < K_SCREEN + 4) {
        static const int seconds[] = {30, 60, 90, 0};
        c->screen_timeout = seconds[id - K_SCREEN];
        *page_change = true;
        return CTL_SCREEN; /* reselect also retries a failed save */
    }
    if (id == K_TURN) return CTL_TURN;
    if (id == K_PAGE_PREV || id == K_PAGE_NEXT) {
        int page_size = LCD_PORTRAIT ? 10 : 12;
        int last = (PANEL_BANDS - 1) / page_size;
        s_band_page = clampi(s_band_page + (id == K_PAGE_NEXT ? 1 : -1), 0, last);
        *page_change = true;
        return 0;
    }
    if (id >= K_TILE && id < K_TILE + 12) { s_page = id - K_TILE; *page_change = true; return 0; }
    if (id == K_MEMORY || id == K_SAVE) { s_page = id == K_SAVE ? P_SAVE : P_MEM; *page_change = true; return 0; }
    if (id == K_MSCAN) {
        for (int i = 0; i < PANEL_MEMS; i++) if (c->mem_hz[i]) return CTL_MEM_SCAN;
        return 0;
    }
    if (id >= K_SLOT && id < K_SLOT + PANEL_MEMS) {
        int i = id - K_SLOT;
        c->mem = i;
        *page_change = true;
        if (s_page == P_SAVE) {
            c->mem_hz[i] = c->freq;
            c->mem_bw[i] = (int8_t)c->bw;
            s_page = P_MEM;                  /* show where it went */
            return CTL_MEM_SAVE;
        }
        return c->mem_hz[i] ? CTL_MEM_TUNE : 0;
    }
    if (id == K_BIAS_OFF || id == K_BIAS_ON) {
        bool on = id == K_BIAS_ON;
        if (c->bias_forced && !on) return 0;
        if (c->bias == on && !c->bias_error) return 0;
        c->bias = on;
        *page_change = true;
        return CTL_BIAS;
    }
    if (id >= K_DIRECT && id < K_DIRECT + 3) {
        int mode = id - K_DIRECT;
        if (mode && !c->direct_supported) return 0;
        if (mode == c->direct) return 0;
        c->direct = mode;
        *page_change = true;
        return CTL_DIRECT;
    }
    if (c->gain_n > 1 && (id == K_GAIN_DN || id == K_GAIN_UP)) {
        int g = c->gain_idx;
        if (g == 0) g = nearest_gain(c, 300);            /* leave AGC from the middle */
        else g = clampi(g + (id == K_GAIN_UP ? 1 : -1), 1, c->gain_n - 1);
        if (g == c->gain_idx) return 0;
        c->gain_idx = g;
        return CTL_GAIN;
    }
    if (id >= K_GAIN_P && id < K_GAIN_P + 4 && c->gain_n > 1) {
        int i = id - K_GAIN_P;
        c->gain_idx = i == 0 ? 0 : i == 3 ? c->gain_n - 1 : nearest_gain(c, i == 1 ? 150 : 300);
        *page_change = true;
        return CTL_GAIN;
    }
    if (id >= K_TOP_DN && id <= K_SPAN_UP) {
        if (id == K_TOP_DN) c->ref_db = clampi(c->ref_db - 5, -120, 0);
        if (id == K_TOP_UP) c->ref_db = clampi(c->ref_db + 5, -120, 0);
        if (id == K_SPAN_DN) c->range_db = clampi(c->range_db - 5, 20, 100);
        if (id == K_SPAN_UP) c->range_db = clampi(c->range_db + 5, 20, 100);
        if (c->range_auto) { c->range_auto = false; *page_change = true; }
        return CTL_RANGE;
    }
    if (id >= K_VOL_DN && id <= K_SQL_UP) {
        int vol = c->vol, sql = c->sql;
        if (id == K_VOL_DN) c->vol = clampi(c->vol - 1, 0, 10);
        if (id == K_VOL_UP) c->vol = clampi(c->vol + 1, 0, 10);
        if (id == K_SQL_DN) c->sql = clampi(c->sql - 1, 0, 9);
        if (id == K_SQL_UP) c->sql = clampi(c->sql + 1, 0, 9);
        return vol != c->vol || sql != c->sql ? CTL_AUDIO : 0;
    }
    if (id == K_AUTO) { c->range_auto = !c->range_auto; *page_change = true; return CTL_RANGE; }
    if (id >= K_ZOOM && id < K_ZOOM + PANEL_ZOOMS) {
        c->zoom = id - K_ZOOM; *page_change = true; return CTL_ZOOM;
    }
    if (id >= K_BW && id < K_BW + PANEL_BWS) {
        c->bw = id - K_BW; *page_change = true; return CTL_BW;
    }
    if (id >= K_LOOK && id < K_LOOK + LOOKS) {
        c->look = id - K_LOOK;
        *page_change = true;
        return CTL_LOOK;
    }
    if (id >= K_BAND && id < K_BAND + PANEL_BANDS) {
        uint32_t f;
        c->band = id - K_BAND;
        panel_band(c->band, &f, &c->bw, &c->zoom);
        *page_change = true;
        return CTL_BAND | CTL_CLOSE;
    }
    return 0;
}

int panel_touch(ctl_t *c, int ev, int x, int y, int64_t now_us)
{
    int k = hit(x, y), old = s_hot, ch = 0;
    bool page = false;
    if (ev == PANEL_PRESS) {
        s_hot = k;
        if (k >= 0 && s_k[k].rep) {
            ch = act(c, s_k[k].id, &page);
            s_next_rep = now_us + 450000;
        }
    } else if (ev == PANEL_MOVE) {
        if (s_hot >= 0 && s_k[s_hot].rep) {                /* steppers: hold to repeat */
            if (k == s_hot && now_us >= s_next_rep) {
                ch = act(c, s_k[k].id, &page);
                s_next_rep = now_us + 150000;
            }
        } else {
            s_hot = k;                                     /* follow the finger */
        }
    } else {
        if (s_hot >= 0 && !s_k[s_hot].rep && s_hot == k) ch = act(c, s_k[s_hot].id, &page);
        s_hot = -1;
    }
    if (ch & CTL_CLOSE) return ch;
    if (page) {
        if (ev != PANEL_RELEASE) s_hot = -1;
        show_all(c);
    } else if (ch) {
        build(c);                                          /* same page: same keys, new text */
        show_keys(old, s_hot);                             /* a stepper's row holds its value */
    } else if (old != s_hot) {
        show_keys(old, s_hot);
    }
    return ch;
}
