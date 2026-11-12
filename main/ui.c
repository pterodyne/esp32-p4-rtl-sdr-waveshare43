/* Receiver loop: IQ -> spectrum -> view (drawing lives in view.c and panel.c), touch, settings.
 *
 * Tap the spectrum (not the axis or waterfall) to tune. Hold a step button to repeat. Tap the frequency
 * readout is display-only; pick a look on the LOOK page. Tap MENU (top right, gain shown under it) for Controls (gain, dB range, zoom,
 * audio: filter and mode, volume, squelch; bands). Touch is sampled three times a frame; menu keys and tuning act when the finger lifts. Everything is kept in NVS namespace "sdr": freq, gaint (tenths of a dB,
 * -1 = AGC), theme, rauto, ref, rspan, zoom, bw, audctl (versioned audio-state blob), mem (channel memories); logn is the SD log session (sdlog.c). Legacy vol/sql keys are read when audctl is absent. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "theme_store.h"
#include "audio_settings_store.h"
#include "audio_save_pending.h"
#include "tune_step.h"
#include "rpi_lcd.h"
#include "rpi_text.h"
#include "xpt2046.h"
#include "pins.h"
#ifdef CONFIG_SDR_WAVESHARE_43
#include "waveshare_geometry.h"
#endif
#include "sdr.h"
#include "rtlsdr_usb.h"
#include "spectrum.h"
#include "view.h"
#include "panel.h"
#include "adsb.h"
#include "adsb_view.h"
#include "amap.h"
#include "sdcard.h"
#include "sdlog.h"
#include "scan_view.h"
#include "sensor_view.h"
#include "sensor_event_age.h"
#include "settings_save_due.h"
#include "screen_timeout_store.h"
#include "peak_iq_gate.h"
#include "sensor_pipeline.h"
#include "radio_scan.h"
#include "audio.h"
#include "ui_audio_policy.h"
#include "rds.h"
#include "ui.h"

static const char *TAG = "ui";

#define IQ_PER_FRAME (24 * 1024)            /* samples per frame (10 ms), split into FFTs */
/* With the Controls panel up nothing is blitted, so a frame would take 25 ms and the FFT would run
 * at 38 fps, crowding core 0's audio out of the shared cache. Frames there last as long as on the
 * receiver instead, and the waterfall behind the panel fills at the same rate. */
#define PANEL_FRAME_US 100000

static uint8_t *s_iq;
static sensor_pipeline_t s_sensor_pipeline;
static float s_db[NFFT_MAX];
static ctl_t s_ctl = {.range_auto = true, .ref_db = -20, .range_db = 60, .band = -1,
                      .gain_label = sdr_gain_label, .gain_tenths = sdr_gain_tenths};
static bool s_panel;                        /* Controls panel is up */
static int s_audio_pressed = -1;            /* compact waterfall control held under the finger */
static bool s_muted;                        /* saved independently from volume zero */
static bool s_adsb, s_adsb_req;             /* in ADS-B mode / asked for it */
static int s_av_action;                     /* last ADS-B screen key fired */
static bool s_scan;                         /* in a Wi-Fi / BLE scanner */
static int s_scan_req = -1;                 /* SV_WIFI / SV_BLE asked for */
static int s_sv_action;                     /* last scanner key fired */
static int s_sensor_band_action;
static bool s_sensor, s_sensor_req, s_sensor_exit; /* same SDR ring, foreground 433.92 MHz */
static bool s_mscan;                        /* hopping through the channel memories */
static int s_mscan_i = -1;                  /* slot tuned by the scan */
static int64_t s_hop_at, s_dwell_until;

static void draw_header(int pressed) {
    view_header(sdr_freq(), sdr_gain_label(sdr_gain_index()), pressed);
    if (!s_panel) view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, pressed);
}

/* ---------------------------------------------------------------------- settings -- */

static nvs_handle_t s_nvs;
static bool s_nvs_ready;
static int64_t s_save_at;
static bool s_theme_save_failed;
static audio_save_pending_t s_audio_pending;
static screen_idle_t s_screen_idle;
static peak_iq_gate_t s_peak_iq;
static int64_t s_screen_save_at;
static void screen_settings_try_save(void)
{
    int64_t now = esp_timer_get_time();
    if (!s_screen_save_at || now < s_screen_save_at) return;
    esp_err_t err = screen_timeout_save_reopening(&s_nvs_ready, &s_nvs, s_ctl.screen_timeout);
    s_screen_save_at = err == ESP_OK ? 0 : now + 5000000;
    if (err != ESP_OK) ESP_LOGW(TAG, "screen timeout save failed (%s); retry in 5 s", esp_err_to_name(err));
}
#ifdef CONFIG_SDR_WAVESHARE_43
static int s_tune_step = TUNE_STEP_DEFAULT;
#endif

static void settings_changed(void) { s_save_at = esp_timer_get_time() + 3000000; }

static void settings_maybe_save(bool force)
{
    if (!settings_save_due(s_save_at, esp_timer_get_time(), force)) return;
    s_save_at = 0;
    nvs_set_u32(s_nvs, "freq", sdr_freq());
    nvs_set_i32(s_nvs, "gaint", sdr_gain_tenths(sdr_gain_index()));
    nvs_set_i32(s_nvs, "theme", view_theme());
#ifdef CONFIG_SDR_WAVESHARE_43
    nvs_set_i32(s_nvs, "tstep", s_tune_step);
#endif
    nvs_set_i32(s_nvs, "rauto", s_ctl.range_auto);
    nvs_set_i32(s_nvs, "ref", s_ctl.ref_db);
    nvs_set_i32(s_nvs, "rspan", s_ctl.range_db);
    nvs_set_i32(s_nvs, "zoom", s_ctl.zoom);
    nvs_set_i32(s_nvs, "bw", s_ctl.bw);
    nvs_set_i32(s_nvs, "direct", s_ctl.direct); /* bias tee deliberately not persisted */
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "saved %" PRIu32 " Hz, gain %s, zoom %d, bw %d", sdr_freq(),
             sdr_gain_label(sdr_gain_index()), s_ctl.zoom, s_ctl.bw);
}

static int32_t nvs_int(const char *key, int32_t def)
{
    if (!s_nvs_ready) return def;
    int32_t v = def;
    nvs_get_i32(s_nvs, key, &v);
    return v;
}

/* All flash work stays on the UI task. Retry the latest desired state after a
 * failed set/commit; a new tap overrides the retry delay immediately. */
static void audio_settings_try_save(bool force)
{
    int64_t now = esp_timer_get_time();
    if (!s_audio_pending.dirty || (!force && !audio_save_pending_due(&s_audio_pending, now))) return;
    esp_err_t err = ESP_OK;
    if (!s_nvs_ready) {
        err = nvs_open("sdr", NVS_READWRITE, &s_nvs);
        s_nvs_ready = err == ESP_OK;
    }
    if (err == ESP_OK) {
        audio_settings_t desired = {.volume = s_ctl.vol, .squelch = s_ctl.sql, .muted = s_muted};
        err = audio_settings_store_save(s_nvs, &desired);
    }
    audio_save_pending_result(&s_audio_pending, err == ESP_OK, now);
    if (err != ESP_OK) ESP_LOGW(TAG, "audio settings save failed (%s); retry in 5 s", esp_err_to_name(err));
}

static void audio_settings_changed(void)
{
    audio_save_pending_mark(&s_audio_pending);
    audio_settings_try_save(false); /* commit before a fast power-off */
}

static void peak_wait_fresh_iq(void)
{
    peak_iq_gate_start(&s_peak_iq, sdr_total_bytes());
    view_peak_invalidate();
}

static bool fine_frequency_visible(void)
{
#ifdef CONFIG_SDR_WAVESHARE_43
    return s_ctl.bw >= 4 || tune_step_hz(s_tune_step) == 100;
#else
    return s_ctl.bw >= 4;
#endif
}

/* zoom, filter and scale from s_ctl into the FFT and the view */
static void apply_view(void)
{
    int n = 1024 << s_ctl.zoom;
    spectrum_set_size(n > NFFT_MAX ? NFFT_MAX : n);
    view_set_span(panel_zoom_hz(s_ctl.zoom));
    view_set_passband(panel_bw_hz(s_ctl.bw));
    view_set_sideband(s_ctl.bw == 4 ? 1 : s_ctl.bw == 5 ? -1 : 0);
    view_set_fine_tune(fine_frequency_visible());
    view_set_range(s_ctl.range_auto, s_ctl.ref_db, s_ctl.range_db);
}

/* Keep the original four persisted mode indexes; SSB adds upper and lower sidebands. */
static void apply_audio(void)
{
    demod_mode_t m = s_ctl.bw == 0 ? DEMOD_WFM : s_ctl.bw == 3 ? DEMOD_AM :
                     s_ctl.bw == 4 ? DEMOD_USB : s_ctl.bw == 5 ? DEMOD_LSB : DEMOD_NFM;
    audio_set_mode(m, panel_bw_hz(s_ctl.bw));
    audio_set_volume(s_ctl.vol);
    audio_set_squelch(s_ctl.sql);
}

/* ----------------------------------------------------------------------- memories -- */

static void save_memories(void)
{
    struct { uint32_t hz[PANEL_MEMS]; int8_t bw[PANEL_MEMS]; } m;
    memcpy(m.hz, s_ctl.mem_hz, sizeof m.hz);
    memcpy(m.bw, s_ctl.mem_bw, sizeof m.bw);
    nvs_set_blob(s_nvs, "mem", &m, sizeof m);
    nvs_commit(s_nvs);
}

static void load_memories(void)
{
    struct { uint32_t hz[PANEL_MEMS]; int8_t bw[PANEL_MEMS]; } m;
    size_t len = sizeof m;
    if (nvs_get_blob(s_nvs, "mem", &m, &len) != ESP_OK || len != sizeof m) return;
    for (int i = 0; i < PANEL_MEMS; i++) {
        s_ctl.mem_hz[i] = m.hz[i];
        s_ctl.mem_bw[i] = m.bw[i] >= 0 && m.bw[i] < PANEL_BWS ? m.bw[i] : 0;
    }
}

/* a memory's frequency and filter (so also its mode) */
static void tune_memory(int i)
{
    s_ctl.bw = s_ctl.mem_bw[i];
    s_ctl.band = -1;
    sdr_set_freq(s_ctl.mem_hz[i]);
    peak_wait_fresh_iq();
    audio_retuned();
    view_set_passband(panel_bw_hz(s_ctl.bw));   /* not apply_view: that clears the waterfall */
    view_set_sideband(s_ctl.bw == 4 ? 1 : s_ctl.bw == 5 ? -1 : 0);
    view_set_fine_tune(fine_frequency_visible());
    apply_audio();
    s_ctl.freq = sdr_freq();
    if (!s_panel) view_axis(sdr_freq());
    draw_header(-1);
    settings_changed();
}

static void mscan_stop(void)
{
    if (!s_mscan) return;
    s_mscan = false;
    ESP_LOGI(TAG, "memory scan stopped on M%d", s_mscan_i + 1);
}

/* Once a frame while scanning: stay while the squelch is open and 2 s after, otherwise hop to
 * the next stored channel every 300 ms. The squelch is not believed for the first 80 ms after a
 * hop, while IQ from the last channel is still coming through. */
static void mscan_step(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_hop_at > 80000 && audio_squelch_open()) {
        if (s_dwell_until < now + 2000000) s_dwell_until = now + 2000000;
        return;
    }
    if (now < s_dwell_until) return;
    for (int k = 1; k <= PANEL_MEMS; k++) {
        int i = (s_mscan_i + k) % PANEL_MEMS;
        if (!s_ctl.mem_hz[i]) continue;
        s_mscan_i = i;
        if (s_ctl.mem_hz[i] != sdr_freq() || s_ctl.mem_bw[i] != s_ctl.bw) tune_memory(i);
        s_hop_at = now;
        s_dwell_until = now + 300000;
        return;
    }
    mscan_stop();                            /* nothing stored any more */
}

static void mscan_start(void)
{
    if (!s_ctl.sql) {                        /* an open squelch would never let it move on */
        s_ctl.sql = 3;
        apply_audio();
        audio_settings_changed();
        ESP_LOGI(TAG, "memory scan: squelch was open, set to 3");
    }
    s_mscan = true;
    s_mscan_i = -1;
    s_dwell_until = 0;
    ESP_LOGI(TAG, "memory scan");
}

/* RDS names and texts go to the log when they change */
static void log_rds(const rds_info_t *r)
{
    static char ps[9], rt[65];
    if (r->has_ps && strcmp(ps, r->ps)) {
        strcpy(ps, r->ps);
        ESP_LOGI(TAG, "RDS PI %04X, station \"%s\"", r->pi, ps);
    }
    if (r->has_rt && strcmp(rt, r->rt)) {
        strcpy(rt, r->rt);
        ESP_LOGI(TAG, "RDS text \"%s\"", rt);
    }
}

/* the plate on the spectrum: mode and volume, the memory being scanned, or on broadcast FM the
 * RDS station name once it is in; lit while sound is coming out */
static void show_audio(void)
{
    if (!audio_ok()) { view_set_audio(NULL, false); return; }
    char l[24];
    rds_info_t r;
    rds_get(&r);
    if (ui_should_log_rds(s_ctl.bw, s_mscan, s_muted, r.has_ps)) log_rds(&r);
    if (s_mscan) {
        snprintf(l, sizeof l, "SCAN M%d %s", s_mscan_i + 1, panel_bw_mode(s_ctl.bw));
    } else if (s_muted) snprintf(l, sizeof l, "%s MUTED", panel_bw_mode(s_ctl.bw));
    else if (s_ctl.bw == 0 && r.has_ps) {
        int n = 8;
        while (n > 0 && r.ps[n - 1] == ' ') n--;
        snprintf(l, sizeof l, "WFM %.*s", n, r.ps);
    } else if (!s_ctl.vol) snprintf(l, sizeof l, "%s off", panel_bw_mode(s_ctl.bw));
    else if (s_ctl.sql && !audio_squelch_open()) snprintf(l, sizeof l, "%s %d  SQL", panel_bw_mode(s_ctl.bw), s_ctl.vol);
    else snprintf(l, sizeof l, "%s %d", panel_bw_mode(s_ctl.bw), s_ctl.vol);
    view_set_audio(l, !s_muted && s_ctl.vol && audio_squelch_open());
}

/* -------------------------------------------------------------------------- touch -- */

static void retune(uint32_t hz)
{
    sdr_set_freq(hz);
    peak_wait_fresh_iq();
    audio_retuned();
    view_axis(sdr_freq());
    settings_changed();
}

static void panel_show(bool on)
{
    s_panel = on;
    if (on) {
        s_ctl.freq = sdr_freq();
        s_ctl.gain_idx = sdr_gain_index();
        s_ctl.gain_n = sdr_gain_presets();
        s_ctl.bias = sdr_bias_tee();
        s_ctl.bias_forced = sdr_bias_tee_forced();
        s_ctl.bias_error = !sdr_bias_command_ok();
        s_ctl.direct = sdr_direct_mode();
        s_ctl.direct_supported = sdr_direct_supported();
        panel_open(&s_ctl);
    } else {
        view_axis(sdr_freq());                 /* spectrum and waterfall come back next frame */
        view_footer_invalidate();
    }
}

static void set_look(int i)
{
    int previous = view_theme();
    view_set_theme(i);                         /* clears the screen */
    apply_view();
    ESP_LOGI(TAG, "look: %s", view_theme_name(view_theme()));
    if (s_panel) panel_redraw(&s_ctl);
    else view_axis(sdr_freq());
    if (theme_store_should_save(previous, view_theme(), s_theme_save_failed)) {
        esp_err_t err = theme_store_save(s_nvs, view_theme());
        s_theme_save_failed = err != ESP_OK;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "look save failed (%s); retrying with settings", esp_err_to_name(err));
            settings_changed();
        }
    }
}

/* Saved at once (main.c reads it before the splash), then re-layout the UI. */
static void turn(void)
{
#ifdef CONFIG_SDR_WAVESHARE_43
    int rotation = waveshare_next_rotation(LCD_ROTATION);
    ESP_ERROR_CHECK(rpi_lcd_set_rotation(rotation));
    nvs_set_i32(s_nvs, "rot", rotation);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "rotation: %d degrees clockwise", rotation * 90);
#else
    bool p = !LCD_PORTRAIT;
    rpi_lcd_set_portrait(p);
    nvs_set_i32(s_nvs, "port", p);
    nvs_commit(s_nvs);
    ESP_LOGI(TAG, "%s", p ? "portrait" : "landscape");
#endif
    set_look(view_theme());
    draw_header(-1);
}

static void do_action(int act)
{
    if (act == VIEW_MUTE) {
        s_muted = !s_muted;
        audio_set_mute(s_muted); /* don't replace persisted volume or stop IQ/RDS processing */
        view_set_mute(s_muted);
        show_audio();
        view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, s_audio_pressed);
        audio_settings_changed();
        return;
    }
    if (act == VIEW_ZOOM_DN || act == VIEW_ZOOM_UP) {
        int z = s_ctl.zoom + (act == VIEW_ZOOM_UP ? 1 : -1);
        if (z >= 0 && z < PANEL_ZOOMS) {
            s_ctl.zoom = z;
            apply_view();
            view_axis(sdr_freq());
            settings_changed();
        }
        view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, s_audio_pressed);
        return;
    }
    if (act >= VIEW_MODE_0 && act < VIEW_MODE_0 + PANEL_BWS) {
        int bw = act - VIEW_MODE_0;
        if (s_ctl.bw != bw) {
            s_ctl.bw = bw;
            s_ctl.band = -1;
            view_set_passband(panel_bw_hz(bw));
            view_set_sideband(bw == 4 ? 1 : bw == 5 ? -1 : 0);
            view_set_fine_tune(fine_frequency_visible());
            apply_audio();
            draw_header(-1);
            settings_changed();
        }
        view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, s_audio_pressed);
        return;
    }
    if (act >= VIEW_VOL_DN && act <= VIEW_SQL_UP) {
        int prev = act <= VIEW_VOL_UP ? s_ctl.vol : s_ctl.sql;
        if (act == VIEW_VOL_DN && s_ctl.vol > 0) s_ctl.vol--;
        if (act == VIEW_VOL_UP && s_ctl.vol < 10) s_ctl.vol++;
        if (act == VIEW_SQL_DN && s_ctl.sql > 0) s_ctl.sql--;
        if (act == VIEW_SQL_UP && s_ctl.sql < 9) s_ctl.sql++;
        if (act <= VIEW_VOL_UP) {
            if (prev != s_ctl.vol) { audio_set_volume(s_ctl.vol); audio_settings_changed(); }
        } else if (prev != s_ctl.sql) { audio_set_squelch(s_ctl.sql); audio_settings_changed(); }
        view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, s_audio_pressed);
        return;
    }
#ifdef CONFIG_SDR_WAVESHARE_43
    if (act == VIEW_TOUCH_MODE) {
        view_set_touch_mode((view_touch_mode() + 1) % (VIEW_TOUCH_PEAK + 1));
        draw_header(act);                   /* ephemeral mode; never changes saved STEP */
        return;
    }
    if (act == VIEW_STEP) {
        s_tune_step = tune_step_next(s_tune_step);
        view_set_tune_step(tune_step_label(s_tune_step));
        view_set_fine_tune(fine_frequency_visible());
        settings_changed();
        draw_header(act);
        return;
    }
    if (act == VIEW_DN1M || act == VIEW_UP1M) {
        retune(tune_step_offset(sdr_freq(), s_tune_step, act == VIEW_UP1M ? 1 : -1));
        draw_header(act);
        return;
    }
    if (act == VIEW_GAIN) { panel_show(!s_panel); draw_header(act); }
#else
    uint32_t f = sdr_freq();
    uint32_t coarse = s_ctl.bw >= 4 ? 1000 : 1000000;
    uint32_t fine = s_ctl.bw >= 4 ? 100 : 100000;
    uint32_t smallest = s_ctl.bw >= 4 ? 100 : 10000;
    switch (act) {
    case VIEW_DN1M:   retune(f > coarse ? f - coarse : 0); break;
    case VIEW_DN100K: retune(f > fine ? f - fine : 0); break;
    case VIEW_UP100K: retune(f + fine); break;
    case VIEW_UP1M:   retune(f + coarse); break;
    case VIEW_DN100:  retune(f > smallest ? f - smallest : 0); break;
    case VIEW_UP100:  retune(f + smallest); break;
    case VIEW_GAIN:   panel_show(!s_panel); break;
    }
    draw_header(act);                       /* keep the key lit while held */
#endif
}

static void panel_event(int ev, int x, int y, int64_t now)
{
    int ch = panel_touch(&s_ctl, ev, x, y, now);
    if (ch & CTL_SCREEN) {
        s_screen_idle.last_touch = now;
        s_screen_save_at = now;
        screen_settings_try_save();
        return;
    }
    if ((ch & CTL_CLOSE) && !(ch & CTL_BAND)) { panel_show(false); return; }
    if (ch & CTL_TURN) {
        turn();
        return;
    }
    if (ch & CTL_LOOK) {
        set_look(s_ctl.look);
        draw_header(-1);
        return;
    }
    if (ch & (CTL_WIFI | CTL_BLE)) {         /* scanners on the C6: their own mode */
        panel_show(false);
        s_scan_req = ch & CTL_WIFI ? SV_WIFI : SV_BLE;
        return;
    }
    if (ch & CTL_SENSOR) {
        panel_show(false);
        s_sensor_req = true;
        return;
    }
    if (ch & CTL_GAIN) {
        sdr_set_gain_index(s_ctl.gain_idx);
        draw_header(-1);
    }
    if (ch & CTL_BIAS) {
        if (!sdr_set_bias_tee(s_ctl.bias)) {
            s_ctl.bias = sdr_bias_tee();
            s_ctl.bias_error = true;
            panel_redraw(&s_ctl);
            ESP_LOGW(TAG, "bias tee command rejected");
        } else {
            s_ctl.bias_error = false;
            panel_redraw(&s_ctl);
        }
    }
    if (ch & CTL_DIRECT) {
        if (!sdr_set_direct_mode(s_ctl.direct)) {
            s_ctl.direct = sdr_direct_mode();
            panel_redraw(&s_ctl);
            ESP_LOGW(TAG, "direct ADC selection rejected");
        } else {
            audio_retuned();
            view_reset();
            peak_wait_fresh_iq();
        }
    }
    if (ch & CTL_BAND) {
        uint32_t f;
        int bw, zoom;
        const char *name = panel_band(s_ctl.band, &f, &bw, &zoom);
        ESP_LOGI(TAG, "band %s", name);
        if (!strcmp(name, "ADS-B")) {        /* its own mode: decoder, list and map */
            panel_show(false);
            s_adsb_req = true;
            return;
        }
        sdr_set_freq(f);
        peak_wait_fresh_iq();
        audio_retuned();
        draw_header(-1);
    }
    if (ch & (CTL_ZOOM | CTL_BW | CTL_RANGE)) ESP_LOGI(TAG, "zoom %d, filter %d, range %s %d/%d", s_ctl.zoom,
                                                       s_ctl.bw, s_ctl.range_auto ? "auto" : "manual",
                                                       s_ctl.ref_db, s_ctl.range_db);
    if (ch & CTL_MEM_SAVE) {
        save_memories();
        ESP_LOGI(TAG, "M%d: %" PRIu32 " Hz, %s", s_ctl.mem + 1, s_ctl.mem_hz[s_ctl.mem], panel_bw_mode(s_ctl.mem_bw[s_ctl.mem]));
    }
    if (ch & CTL_MEM_TUNE) {                 /* the panel stays up: tap through and listen */
        tune_memory(s_ctl.mem);
        panel_redraw(&s_ctl);
        return;
    }
    if (ch & CTL_MEM_SCAN) {
        panel_show(false);
        mscan_start();
        return;
    }
    if (ch & (CTL_RANGE | CTL_ZOOM | CTL_BW | CTL_BAND)) apply_view();
    if (ch & CTL_BW) draw_header(-1);
    if (ch & (CTL_BW | CTL_BAND | CTL_AUDIO)) apply_audio();
    if (ch & CTL_AUDIO) audio_settings_changed();
    if (ch & ~CTL_AUDIO) settings_changed();
    if (ch & CTL_CLOSE) panel_show(false); /* tune/apply first, then restore waterfall */
}

/* Touch tracker. The XPT2046 reads noisily as the finger lands and lifts, so the first
 * sample after pen-down is dropped, later ones are smoothed, and the lift-off position is
 * the last smoothed one. Events: press (first good sample), move, release. */
static void poll_touch(const touch_cal_t *cal)
{
    static bool down;
    static int n, x, y, touch_x0;            /* Peak anchors to the press, not release drift */
    static int where;                       /* 0 header, 1 panel, 2 tuning, 3 ADS-B, 4 scanner, 5 none, 6 audio, 7 mode, 8 zoom, 9 sensors */
    static int act = -1;
    static int64_t next_repeat;
    uint16_t rx, ry, rz;
    int64_t now = esp_timer_get_time();

    bool contact = xpt2046_read_raw(&rx, &ry, &rz);
    screen_settings_try_save();
#ifdef CONFIG_SDR_WAVESHARE_43
    int screen_action = screen_idle_update(&s_screen_idle, s_ctl.screen_timeout, now, contact);
    if (screen_action & (SCREEN_SLEEP | SCREEN_WAKE)) {
        esp_err_t err = rpi_lcd_set_display_on(!(screen_action & SCREEN_SLEEP));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "display power transition failed (%s); tap to retry wake", esp_err_to_name(err));
            s_screen_idle.asleep = true; /* uncertain/dark: next gesture must only wake */
            s_screen_idle.last_touch = now; /* avoid a failed-off busy loop */
        }
    }
    if (screen_action & SCREEN_CONSUME) {
        down = false; n = 0; act = -1; where = 5;
        return; /* entire wake gesture, including lift, is not a UI event */
    }
#endif
    if (!contact) {
        if (down && n > 0) {                /* release */
            if (where == 3) s_av_action = adsb_view_touch(AV_RELEASE, x, y);
            else if (where == 4) s_sv_action = scan_view_touch(SV_RELEASE, x, y);
            else if (where == 9) {
                int action = sensor_view_touch(SENSOR_RELEASE, x, y);
                s_sensor_exit = action == SENSOR_EXIT;
                s_sensor_band_action = action == SENSOR_BAND;
            }
            else if (where == 0 && act >= 0) draw_header(-1);
            else if (where == 6) { s_audio_pressed = -1; view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, -1); }
            else if (where == 7) {
                s_audio_pressed = -1;
                if (view_mode_button_at(x, y) == act) do_action(act);
                else view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, -1);
            }
            else if (where == 8) {
                s_audio_pressed = -1;
                view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, -1);
            }
            else if (where == 1) panel_event(PANEL_RELEASE, x, y, now);
            else if (where == 2 && !s_panel && x >= 0 && x < LCD_W &&
                     y >= view_tune_y() && y < view_spectrum_bottom()) {
                uint32_t quantum = s_ctl.bw >= 4 ? (s_ctl.zoom >= 2 ? 100 : 1000) :
                                   s_ctl.zoom == 0 ? 10000 : s_ctl.zoom == 1 ? 5000 : 1000;
                int mode = VIEW_TOUCH_FREE;
#ifdef CONFIG_SDR_WAVESHARE_43
                mode = view_touch_mode();
                if (mode == VIEW_TOUCH_SNAP) quantum = tune_step_hz(s_tune_step);
#endif
                uint32_t hz;
                if ((mode != VIEW_TOUCH_PEAK || sdr_streaming()) &&
                    view_touch_frequency(mode == VIEW_TOUCH_PEAK ? touch_x0 : x,
                                         y, sdr_freq(), quantum, mode, &hz)) {
                    retune(hz);
                    draw_header(-1);
                }
            }
        }
        down = false;
        n = 0;
        act = -1;
        return;
    }
    int px, py;
    touch_cal_map(cal, rx, ry, &px, &py);
    if (!down) { down = true; n = 0; return; }       /* landing sample: drop it */
    if (n++ == 0) {                                   /* press */
        x = px; y = py; touch_x0 = x;
        if (s_mscan) {                                /* the first touch only stops a scan */
            mscan_stop();
            where = 5;
        } else if (s_adsb) {
            where = 3;
            adsb_view_touch(AV_PRESS, x, y);
        } else if (s_scan) {
            where = 4;
            scan_view_touch(SV_PRESS, x, y);
        } else if (s_sensor) {
            where = 9;
            sensor_view_touch(SENSOR_PRESS, x, y);
        } else if (y < view_tune_y() || (!s_panel && view_button_at(x, y) >= 0)) {
            where = 0;
            act = view_button_at(x, y);
            if (act >= 0) { do_action(act); next_repeat = now + 450000; }
        } else if (s_panel) {
            where = 1;
            panel_event(PANEL_PRESS, x, y, now);
        } else if ((act = view_audio_button_at(x, y)) >= 0) {
            where = 6;
            s_audio_pressed = act;
            do_action(act);
            next_repeat = now + 450000;
        } else if ((act = view_mode_button_at(x, y)) >= 0) {
            where = 7;
            s_audio_pressed = act;
            view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, act);
        } else if ((act = view_zoom_button_at(x, y)) >= 0) {
            where = 8;
            s_audio_pressed = act;
            do_action(act);
            next_repeat = now + 450000;
        } else if (view_audio_overlay_at(x, y)) {
            where = 5;                       /* label/padding: never retune underneath */
        } else {
            where = y >= view_tune_y() && y < view_spectrum_bottom() ? 2 : 5;
        }
        return;
    }
    x = (x + px) / 2;                                 /* move */
    y = (y + py) / 2;
    if (where == 3) adsb_view_touch(AV_MOVE, x, y);
    else if (where == 4) scan_view_touch(SV_MOVE, x, y);
    else if (where == 9) sensor_view_touch(SENSOR_MOVE, x, y);
    else if (where == 1) panel_event(PANEL_MOVE, x, y, now);
    else if (where == 7) {
        int hot = view_mode_button_at(x, y) == act ? act : -1;
        if (hot != s_audio_pressed) {
            s_audio_pressed = hot;
            view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, hot);
        }
    }
    else if (where == 8) {
        int hot = view_zoom_button_at(x, y) == act ? act : -1;
        if (hot != s_audio_pressed) {
            s_audio_pressed = hot;
            view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, hot);
        }
        if (hot >= 0 && now >= next_repeat) {
            do_action(act);
            next_repeat = now + 150000;
        }
    }
    else if (where == 6) {
        int hot = view_audio_button_at(x, y) == act ? act : -1;
        if (hot != s_audio_pressed) {
            s_audio_pressed = hot;
            view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, hot);
        }
        if (hot >= 0 && act != VIEW_MUTE && now >= next_repeat) {
            do_action(act);
            next_repeat = now + 150000;
        }
    }
    else if (where == 0 && act >= 0 && act < VIEW_GAIN && act != VIEW_STEP && now >= next_repeat) {
        do_action(act);
        next_repeat = now + 150000;
    }
}

/* -------------------------------------------------------------------------- ADS-B -- */

/* free internal RAM, its low-water mark, and the largest internal DMA block, in KB: the numbers
 * the USB transfers and the C6 link compete for */
static void ram_kb(unsigned *free_kb, unsigned *min_kb, unsigned *dma_kb)
{
    *free_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10;
    *min_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) >> 10;
    *dma_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) >> 10;
}

static double uptime_s(void) { return esp_timer_get_time() / 1e6; }

/* One row per aircraft heard since its last row, at most every 5 s each. */
enum { LOG_EVERY_MS = 5000, LOG_SLOTS = 2 * ADSB_MAX_AC };
typedef struct { uint32_t icao; int64_t at; } logged_t;
static logged_t s_logged[LOG_SLOTS];          /* last row per aircraft, adsb clock; cleared per entry */

static void log_aircraft(void)
{
    logged_t *done = s_logged;
    const int64_t now = adsb_now_ms();
    for (int i = 0; i < adsb_count(); i++) {
        const adsb_ac_t *a = adsb_get(i);
        int k = -1, old = 0;
        for (int j = 0; j < LOG_SLOTS; j++) {
            if (done[j].icao == a->icao && done[j].at) { k = j; break; }
            if (done[j].at < done[old].at) old = j;
        }
        if (k >= 0 && (a->seen_ms <= done[k].at || now - done[k].at < LOG_EVERY_MS)) continue;
        if (k < 0) { k = old; done[k].icao = a->icao; }
        done[k].at = now;
        char fl[24] = "", alt[12] = "", spd[12] = "", trk[12] = "", vr[12] = "", lat[16] = "", lon[16] = "";
        if (a->flight[0]) sdlog_str(fl, sizeof fl, a->flight);
        if (a->alt_ft != INT32_MIN) snprintf(alt, sizeof alt, "%d", a->alt_ft);
        if (a->speed_kt >= 0) {
            snprintf(spd, sizeof spd, "%d", a->speed_kt);
            snprintf(trk, sizeof trk, "%d", a->track_deg);
            snprintf(vr, sizeof vr, "%d", a->vrate_fpm);
        }
        if (a->has_pos) {
            snprintf(lat, sizeof lat, "%.5f", a->lat);
            snprintf(lon, sizeof lon, "%.5f", a->lon);
        }
        sdlog_row("%.1f,%06" PRIX32 ",%s,%s,%s,%s,%s,%s,%s,%" PRIu32, uptime_s(), a->icao, fl, alt, spd, trk,
                  vr, lat, lon, a->msgs);
    }
}

/* 1090 MHz at 2 MS/s, max gain, every sample through the decoder. Returns when EXIT is
 * tapped or the stream dies; the receiver settings are restored either way. */
static void adsb_mode(const touch_cal_t *cal)
{
    enum { BUF = 32 * 1024 };
    static uint8_t *buf;                       /* PSRAM: internal RAM is for USB DMA */
    if (!buf) buf = heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM);
    assert(buf);
    const uint32_t freq = sdr_freq();
    const int gain = sdr_gain_index();
    ESP_LOGI(TAG, "ADS-B mode");
    audio_pause(true);
    sdr_stop();
    memset(s_logged, 0, sizeof s_logged);   /* adsb_init restarts the decoder's clock */
    sdlog_begin("ADSB", "t_s,icao,callsign,alt_ft,speed_kt,track_deg,vrate_fpm,lat,lon,msgs");
    static uint8_t *map;                       /* MAP.BIN, kept in PSRAM once read */
    if (!map) {                                /* card read while the USB transfers are freed */
        size_t len;
        if (sdcard_read("MAP.BIN", &map, &len, 16 << 20)) {
            if (amap_load(map, len)) {
                const amap_info_t *m = amap_info();
                ESP_LOGI(TAG, "map \"%s\": %.4f %.4f, %d km, %d features", m->name, m->lat, m->lon,
                         m->radius_km, m->nfeat);
            } else {
                ESP_LOGW(TAG, "MAP.BIN is not a map");
                free(map);
                map = NULL;
            }
        }
    }
    sdr_set_sample_rate(ADSB_RATE);
    sdr_set_freq(1090000000);
    sdr_set_gain_index(sdr_gain_presets() - 1);
    sdr_start();
    adsb_init();
    adsb_view_open();
    s_adsb = true;
    s_av_action = AV_NONE;

    adsb_view_info_t info = {.gain_tenths = sdr_gain_tenths(sdr_gain_index()), .rec = sdlog_state()};
    if (amap_info()) {                         /* the map's centre is home */
        info.have_home = true;
        info.home_lat = amap_info()->lat;
        info.home_lon = amap_info()->lon;
    }
    uint64_t pos = sdr_total_bytes(), dropped = 0, ok_last = 0, pre_last = 0;
    int64_t t_draw = 0, t_touch = 0, t_yield = esp_timer_get_time(), t_log = t_yield, busy = 0;
    adsb_view_draw(&info);
    while (sdr_streaming() && s_av_action != AV_EXIT) {
        int64_t t0 = esp_timer_get_time();
        size_t n = sdr_ring_read(buf, &pos, BUF, &dropped);
        if (n) {
            adsb_process(buf, n / 2);
            busy += esp_timer_get_time() - t0;
        }
        int64_t now = esp_timer_get_time();
        audio_settings_try_save(false);
        if (n < BUF || now - t_yield > 50000) { vTaskDelay(1); t_yield = now; }
        if (now - t_touch > 20000) {
            t_touch = now;
            s_av_action = AV_NONE;
            poll_touch(cal);
            if (s_av_action == AV_TOGGLE || s_av_action == AV_ZOOM_IN || s_av_action == AV_ZOOM_OUT) t_draw = 0;
        }
        if (now - t_draw > 1000000) {
            t_draw = now;
            adsb_expire(60000);
            if (!info.have_home)                 /* centre the map on the first fix */
                for (int i = 0; i < adsb_count(); i++)
                    if (adsb_get(i)->has_pos) {
                        info.have_home = true;
                        info.home_lat = adsb_get(i)->lat;
                        info.home_lon = adsb_get(i)->lon;
                        break;
                    }
            log_aircraft();
            info.rec = sdlog_state();
            adsb_view_draw(&info);
        }
        if (now - t_log > 5000000) {
            adsb_stats_t st;
            adsb_get_stats(&st);
            double dt = (now - t_log) / 1e6;
            info.msgs_per_s = (st.msgs_ok - ok_last) / dt;
            unsigned fk, mk, dk;
            ram_kb(&fk, &mk, &dk);
            ESP_LOGI(TAG, "adsb: %.1f msg/s, %.0f preambles/s, ok %llu fixed %llu bad %llu, %d aircraft, "
                     "decoder load %.0f%%, dropped %llu B, ram %u KB (min %u, dma %u)", info.msgs_per_s,
                     (st.preambles - pre_last) / dt, (unsigned long long)st.msgs_ok, (unsigned long long)st.msgs_fixed,
                     (unsigned long long)st.msgs_bad, adsb_count(), 100.0 * busy / (now - t_log),
                     (unsigned long long)dropped, fk, mk, dk);
            ok_last = st.msgs_ok;
            pre_last = st.preambles;
            busy = 0;
            t_log = now;
        }
    }
    s_adsb = false;
    ESP_LOGI(TAG, "leaving ADS-B mode");
    sdr_stop();
    sdlog_end();
    sdr_set_sample_rate(SDR_SAMPLE_RATE);
    sdr_set_freq(freq);
    sdr_set_gain_index(gain);
    sdr_start();
    audio_pause(false);
    view_set_theme(view_theme());              /* clears the screen */
    apply_view();
    draw_header(-1);
    view_axis(sdr_freq());
}

/* Opt-in foreground decoder: reuse this USB owner's 2.4 MS/s CU8 IQ ring.
 * The waterfall and audio reader pause, but the USB task and its driver stay
 * untouched. Do not report a named protocol as support for other sensors. */
static void sensor_mode(const touch_cal_t *cal)
{
    if (!sdr_streaming()) return; /* the outer loop owns stream recovery */
    if (sdr_sample_rate() != SDR_SAMPLE_RATE || sdr_direct_mode() != 0 ||
        !sensor_pipeline_init(&s_sensor_pipeline, SDR_SAMPLE_RATE)) {
        ESP_LOGW(TAG, "sensors need the 2.4 MS/s tuner IQ path");
        return;
    }
    const uint32_t previous_freq = sdr_freq();
    settings_maybe_save(true); /* never persist the sensor center as receiver frequency */
    audio_settings_try_save(true);
    ESP_LOGI(TAG, "WH2 / Steelmate foreground decoder: 433.92 MHz");
    audio_pause(true);
    if (!audio_wait_paused(250)) {
        audio_pause(false);
        ESP_LOGW(TAG, "audio task did not pause; sensor tune cancelled");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(40)); /* let the codec's queued I2S DMA finish */
    sdr_set_freq(433920000);
    vTaskDelay(pdMS_TO_TICKS(20)); /* discard the tuner transition and queued old IQ */
    uint64_t pos = sdr_total_bytes(), dropped = 0;
    sensor_view_info_t info = {.streaming = true};
    uint64_t seen_us[SENSOR_VIEW_ROWS] = {0};
    sensor_view_open();
    s_sensor = true;
    s_sensor_exit = false;
    s_sensor_band_action = false;
    sensor_view_draw(&info);
    int64_t t_touch = 0, t_draw = 0, t_load = esp_timer_get_time(), t_yield = t_load, busy = 0;
    while (sdr_streaming() && !s_sensor_exit) {
        if (s_sensor_band_action) {
            s_sensor_band_action = false;
            int band = (info.band + 1) % SENSOR_BANDS;
            sdr_set_freq(sensor_view_band_hz(band));
            vTaskDelay(pdMS_TO_TICKS(20));
            pos = sdr_total_bytes();
            dropped = 0; /* per-band drops, like the reset packet counters */
            sensor_pipeline_reset(&s_sensor_pipeline);
            memset(&info, 0, sizeof info);
            memset(seen_us, 0, sizeof seen_us);
            info.band = band;
            info.streaming = true;
            sensor_view_draw(&info);
        }
        int64_t t0 = esp_timer_get_time();
        uint64_t dropped_before = dropped;
        size_t n = sdr_ring_read(s_iq, &pos, 32 * 1024, &dropped);
        if (dropped != dropped_before) sensor_pipeline_reset(&s_sensor_pipeline);
        if (n) {
            sensor_packet_t found[8];
            size_t count = sensor_pipeline_feed(&s_sensor_pipeline, s_iq, n, found, 8);
            for (size_t j = 0; j < count; j++) {
                char text[SENSOR_VIEW_LINE];
                if (found[j].type == SENSOR_WH2) {
                    int v = found[j].weather.temperature_tenths_c;
                    snprintf(text, sizeof text, "WH2 %02X %s%d.%d C %u%%", found[j].weather.id,
                             v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10, found[j].weather.humidity);
                    info.wh2++;
                } else {
                    const tpms_steelmate_packet_t *p = &found[j].tpms;
                    snprintf(text, sizeof text, "TPMS %04X %.1f kPa %d C %s", p->id,
                             p->pressure_kpa, p->temperature_c,
                             p->alarm == TPMS_STEELMATE_FAST_LEAK ? "FAST LEAK" :
                             p->alarm == TPMS_STEELMATE_SLOW_LEAK ? "SLOW LEAK" : "");
                    info.tpms++;
                }
                memmove(info.event[1], info.event[0],
                        (SENSOR_VIEW_ROWS - 1) * SENSOR_VIEW_LINE);
                memmove(seen_us + 1, seen_us, (SENSOR_VIEW_ROWS - 1) * sizeof seen_us[0]);
                seen_us[0] = (uint64_t)esp_timer_get_time();
                snprintf(info.event[0], SENSOR_VIEW_LINE, "%s", text);
                if (info.rows < SENSOR_VIEW_ROWS) info.rows++;
            }
            busy += esp_timer_get_time() - t0;
        }
        int64_t now = esp_timer_get_time();
        audio_settings_try_save(false);
        while (info.rows && sensor_event_expired(seen_us[info.rows - 1], (uint64_t)now)) {
            info.rows--;
            info.event[info.rows][0] = 0;
        }
        if (n < 32 * 1024 || now - t_yield > 50000) {
            vTaskDelay(1); /* IDLE1 and the USB task must get scheduled */
            t_yield = now;
        }
        if (now - t_touch > 20000) { t_touch = now; poll_touch(cal); }
        if (now - t_load > 5000000) {
            int64_t elapsed = now - t_load;
            info.load_percent = (int)(100 * busy / elapsed);
            info.dropped_bytes = dropped;
            ESP_LOGI(TAG, "sensors WH2 %u Steelmate %u, decode %d%%, dropped %" PRIu64 " B",
                     info.wh2, info.tpms, info.load_percent, dropped);
            t_load = now;
            busy = 0;
        }
        if (now - t_draw > 250000) {
            t_draw = now;
            info.dropped_bytes = dropped;
            sensor_view_draw(&info);
        }
    }
    s_sensor = false;
    ESP_LOGI(TAG, "leaving sensor mode: WH2 %u, Steelmate %u, dropped %" PRIu64 " B",
             info.wh2, info.tpms, dropped);
    sdr_set_freq(previous_freq);
    vTaskDelay(pdMS_TO_TICKS(40)); /* resumed reader starts 20 ms behind live IQ */
    audio_retuned();
    vTaskDelay(pdMS_TO_TICKS(20)); /* retune request is processed while paused */
    audio_pause(false);
    view_set_theme(view_theme());
    apply_view();
    draw_header(-1);
    view_axis(sdr_freq());
}

/* ----------------------------------------------------------------- Wi-Fi / BLE -- */

static void mac_str(char *out, const uint8_t *m)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Wi-Fi: every network heard in the sweep that just finished. BLE: every device heard in the
 * last 5 s. since_ms is the scanner clock (ms since boot) of the previous batch. */
static void log_scan(int kind, const scan_view_info_t *in, int64_t since_ms)
{
    char mac[18], a[72], b[40];
    if (kind == SV_WIFI)
        for (int i = 0; i < in->nap; i++) {
            const scan_ap_t *p = &in->ap[i];
            if (p->seen_ms <= since_ms) continue;
            mac_str(mac, p->bssid);
            sdlog_row("%.1f,%s,%s,%d,%d,%s", uptime_s(), mac, sdlog_str(a, sizeof a, p->ssid), p->ch, p->rssi, p->sec);
        }
    else
        for (int i = 0; i < in->nble; i++) {
            const scan_ble_t *p = &in->ble[i];
            if (p->seen_ms <= since_ms) continue;
            mac_str(mac, p->addr);
            sdlog_row("%.1f,%s,%s,%d,%s,%s,%d,%" PRIu32, uptime_s(), mac, p->random ? "random" : "public", p->rssi,
                      sdlog_str(a, sizeof a, p->name), sdlog_str(b, sizeof b, p->kind), p->connectable, p->count);
        }
}

/* A scanner on the module's ESP32-C6. The RTL-SDR stream is stopped first: that frees the
 * internal RAM of the USB transfers, which the Wi-Fi buffers need; the scanner frees its own
 * again on EXIT before the stream restarts. */
static void scan_mode(const touch_cal_t *cal, int kind)
{
    static scan_ap_t *ap;                      /* PSRAM snapshots for drawing */
    static scan_ble_t *ble;
    if (!ap) ap = heap_caps_calloc(SCAN_MAX_AP, sizeof *ap, MALLOC_CAP_SPIRAM);
    if (!ble) ble = heap_caps_calloc(SCAN_MAX_BLE, sizeof *ble, MALLOC_CAP_SPIRAM);
    assert(ap && ble);
    const char *name = kind == SV_WIFI ? "Wi-Fi" : "BLE";
    ESP_LOGI(TAG, "%s scanner", name);
    audio_pause(true);
    sdr_stop();
    scan_view_open(kind);
    s_scan = true;
    s_sv_action = SV_NONE;
    scan_view_info_t info = {.kind = kind, .ap = ap, .ble = ble,
                             .status = kind == SV_WIFI ? "starting Wi-Fi..." : "starting Bluetooth..."};
    scan_view_draw(&info);
    const char *why = NULL;
    bool ok = radio_scan_start(kind, &why);
    info.status = ok ? NULL : why;
    if (ok) sdlog_begin(kind == SV_WIFI ? "WIFI" : "BLE",
                        kind == SV_WIFI ? "t_s,bssid,ssid,channel,rssi_dbm,security"
                                        : "t_s,address,address_type,rssi_dbm,name,maker_or_service,connectable,adverts");
    info.rec = sdlog_state();
    int64_t t_draw = 0, t_log = esp_timer_get_time(), logged_ms = 0;
    int logged_sweeps = 0;
    while (s_sv_action != SV_EXIT) {
        int64_t now = esp_timer_get_time();
        audio_settings_try_save(false);
        if (ok) radio_scan_poll();
        s_sv_action = SV_NONE;
        poll_touch(cal);
        if (s_sv_action == SV_TOGGLE || s_sv_action == SV_MORE) t_draw = 0;
        if (now - t_draw > 1000000) {
            t_draw = now;
            info.now_ms = now / 1000;
            if (ok && kind == SV_WIFI) info.nap = radio_scan_get_ap(ap, SCAN_MAX_AP, &info.scans);
            if (ok && kind == SV_BLE) info.nble = radio_scan_get_ble(ble, SCAN_MAX_BLE);
            if (kind == SV_WIFI ? info.scans != logged_sweeps : info.now_ms - logged_ms >= 5000) {
                log_scan(kind, &info, logged_ms);
                logged_sweeps = info.scans;
                logged_ms = info.now_ms;
            }
            info.rec = sdlog_state();
            scan_view_draw(&info);
        }
        if (now - t_log > 10000000) {
            t_log = now;
            unsigned fk, mk, dk;
            ram_kb(&fk, &mk, &dk);
            if (kind == SV_WIFI) ESP_LOGI(TAG, "wifi: %d networks, %d sweeps, ram %u KB (min %u, dma %u)", info.nap,
                                          info.scans, fk, mk, dk);
            else ESP_LOGI(TAG, "ble: %d devices, ram %u KB (min %u, dma %u)", info.nble, fk, mk, dk);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    sdlog_end();
    if (ok) radio_scan_stop();
    s_scan = false;
    ESP_LOGI(TAG, "leaving the %s scanner", name);
    sdr_start();
    audio_pause(false);
    view_set_theme(view_theme());              /* clears the screen */
    apply_view();
    draw_header(-1);
    view_axis(sdr_freq());
}

/* ---------------------------------------------------------------------------- loop -- */

void ui_run(const touch_cal_t *cal)
{
    s_iq = heap_caps_malloc(IQ_PER_FRAME * 2, MALLOC_CAP_SPIRAM);
    assert(s_iq);
    spectrum_init();
    view_init(SDR_SAMPLE_RATE);
    panel_init();
    adsb_view_init();
    scan_view_init();
    sensor_view_init();
    esp_err_t nvs_err = nvs_open("sdr", NVS_READWRITE, &s_nvs);
    s_nvs_ready = nvs_err == ESP_OK;
    if (!s_nvs_ready) ESP_LOGW(TAG, "NVS open failed (%s); audio fails muted", esp_err_to_name(nvs_err));
    view_set_theme(nvs_int("theme", 0));
    s_ctl.screen_timeout = nvs_int("screen_sec", 0);
    if (!screen_timeout_valid(s_ctl.screen_timeout)) s_ctl.screen_timeout = 0;
    s_screen_idle.last_touch = esp_timer_get_time();
    s_ctl.range_auto = nvs_int("rauto", 1);
    s_ctl.ref_db = nvs_int("ref", -20);
    s_ctl.range_db = nvs_int("rspan", 60);
    s_ctl.zoom = nvs_int("zoom", 0) % PANEL_ZOOMS;
    s_ctl.bw = nvs_int("bw", 0) % PANEL_BWS;
#ifdef CONFIG_SDR_WAVESHARE_43
    s_tune_step = tune_step_index(nvs_int("tstep", TUNE_STEP_DEFAULT));
    view_set_tune_step(tune_step_label(s_tune_step));
#endif
    audio_settings_t boot_audio = {.volume = 4, .squelch = 0, .muted = true};
    if (s_nvs_ready) {
        nvs_err = audio_settings_store_load(s_nvs, &boot_audio);
        if (nvs_err != ESP_OK)
            ESP_LOGW(TAG, "audio settings load failed (%s); starting muted", esp_err_to_name(nvs_err));
    }
    s_ctl.vol = boot_audio.volume;
    s_ctl.sql = boot_audio.squelch;
    s_muted = boot_audio.muted;
    view_set_mute(s_muted);
    int saved_direct = nvs_int("direct", 0);
    if (saved_direct >= 0 && saved_direct <= 2 && saved_direct != sdr_direct_mode())
        sdr_set_direct_mode(saved_direct);     /* V4 rejects I/Q; normal tuner stays active */
    s_ctl.direct = sdr_direct_mode();
    s_ctl.direct_supported = sdr_direct_supported();
    s_ctl.bias = sdr_bias_tee();
    s_ctl.bias_forced = sdr_bias_tee_forced();
    s_ctl.bias_error = !sdr_bias_command_ok();
    load_memories();
    apply_view();
    audio_set_mute(s_muted); /* request is queued before the codec can output samples */
    if (audio_init()) apply_audio();

    draw_header(-1);
    view_axis(sdr_freq());
    sdr_start();
    peak_wait_fresh_iq();
    audio_pause(false);

    int frames = 0;
    int64_t t_log = esp_timer_get_time(), t_dsp = 0, t_lcd = 0;
    uint64_t b_log = sdr_total_bytes();
    for (;;) {
        audio_settings_try_save(false); /* also retry while the SDR is lost */
        if (!sdr_streaming()) peak_wait_fresh_iq(); /* before touch during recovery */
        poll_touch(cal); /* timeout/wake also work during stream loss */
        if (s_adsb_req) {
            s_adsb_req = false;
            adsb_mode(cal);
            peak_wait_fresh_iq();
        }
        if (s_scan_req >= 0) {
            int k = s_scan_req;
            s_scan_req = -1;
            scan_mode(cal, k);
            peak_wait_fresh_iq();
        }
        if (s_sensor_req) {
            s_sensor_req = false;
            sensor_mode(cal);
            peak_wait_fresh_iq();
        }
        if (!sdr_streaming()) {
            static int restarts;
            if (!rtlsdr_usb_take_gone() && restarts < 3) {
                /* Dongle still attached: restart the stream, don't reopen the device. */
                restarts++;
                ESP_LOGW(TAG, "stream stopped, restart %d", restarts);
                vTaskDelay(pdMS_TO_TICKS(500));
                sdr_start();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGW(TAG, "stream lost: reopening the dongle");
            s_panel = false;
            view_message("RTL-SDR not streaming", "unplug and replug it if this stays");
            if (!sdr_close()) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue; /* retry cancellation; never reopen over an active RX task */
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            while (!sdr_open(5000, NULL, 0)) {
                audio_settings_try_save(false);
                poll_touch(cal);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            restarts = 0;
            view_reset();
            draw_header(-1);
            view_axis(sdr_freq());
            sdr_start();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        int n = spectrum_size();
        uint64_t frame_end = sdr_ring_latest(s_iq, IQ_PER_FRAME * 2);
        spectrum_compute(s_iq, IQ_PER_FRAME / n, s_db);
        show_audio();
        view_render(s_db, n, sdr_freq());
        if (!peak_iq_gate_ready(&s_peak_iq, frame_end, IQ_PER_FRAME * 2,
                                SDR_SAMPLE_RATE / 25)) /* 20 ms of fresh CU8 after retune */
            view_peak_invalidate();
        int64_t t1 = esp_timer_get_time();
        poll_touch(cal);
        if (!s_panel) view_blit_spectrum();    /* the waterfall keeps filling behind the panel */
        poll_touch(cal);
        if (!s_panel) {
            view_blit_waterfall();
            view_audio_controls(s_ctl.vol, s_ctl.sql, s_ctl.bw, s_ctl.zoom, s_audio_pressed);
        }
        int64_t t2 = esp_timer_get_time();
        t_dsp += t1 - t0;
        t_lcd += t2 - t1;
        poll_touch(cal);
        while (s_panel && esp_timer_get_time() - t0 < PANEL_FRAME_US) {
            vTaskDelay(pdMS_TO_TICKS(10));
            poll_touch(cal);
        }
        if (s_mscan) mscan_step();
        settings_maybe_save(false);
        vTaskDelay(1);                          /* let IDLE1 run (task watchdog) */
        frames++;
        if (t2 - t_log > 5000000) {
            int nf = frames;
            uint64_t b = sdr_total_bytes();
            rtlsdr_usb_stats_t us;
            rtlsdr_usb_get_stats(&us);
            unsigned fk, mk, dk;
            ram_kb(&fk, &mk, &dk);
            ESP_LOGI(TAG, "%.1f fps (dsp %.1f ms, lcd %.1f ms), fft %d, floor %.1f dBFS, IQ %.3f MB/s, "
                     "usb err %" PRIu32 " backlog max %" PRIu32 ", ram %u KB (min %u, dma %u)",
                     nf * 1e6 / (t2 - t_log), t_dsp / 1e3 / nf, t_lcd / 1e3 / nf, n, view_floor(),
                     (b - b_log) / ((t2 - t_log) / 1e6) / 1e6, us.bulk_errors, us.max_backlog, fk, mk, dk);
            if (audio_ok()) {
                int load, lag;
                audio_stats(&load, &lag);
                ESP_LOGI(TAG, "audio %s vol %d sql %d: demod %d%% of real time, lag max %d ms, channel %.1f dBFS "
                         "(floor %.1f), squelch %.1f dB, %s", panel_bw_mode(s_ctl.bw), s_ctl.vol, s_ctl.sql, load, lag,
                         demod_level_db(), demod_floor_db(), demod_quality_db(), audio_squelch_open() ? "open" : "shut");
                if (s_ctl.bw == 0) {
                    rds_info_t r;
                    rds_get(&r);
                    ESP_LOGI(TAG, "rds: %s, %" PRIu32 " blocks, %" PRIu32 " bad", r.sync ? "in sync" : "searching",
                             r.blocks, r.bad_blocks);
                }
            }
            b_log = b;
            frames = 0;
            t_dsp = t_lcd = 0;
            t_log = t2;
        }
    }
}
