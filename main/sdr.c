/* RTL-SDR Blog V4 front end: open, tune, gain, and a 1 MB IQ ring in PSRAM fed by read_async. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "rtl-sdr.h"
#include "rtlsdr_usb.h"
#include "usb/usb_host.h"
#include "sdr.h"
#include "rf_policy.h"
#include "bias_state.h"

static const char *TAG = "sdr";

#define BUF_NUM   10                          /* x BUF_LEN of internal DMA RAM (384 KB region) */
#define BUF_LEN   (24 * 1024)                 /* 5.1 ms of IQ per USB transfer */
#define RING_LEN  (1 << 20)                   /* 218 ms of IQ */

static rtlsdr_dev_t *s_dev;
static bias_state_t s_bias;
static bool s_bias_forced, s_direct_supported;
static int s_direct_mode;                    /* manual I/Q survives USB reopen; bias does not */
static uint8_t *s_ring;
static volatile uint64_t s_wr;                /* total bytes written into the ring */
static volatile bool s_streaming;
static TaskHandle_t s_rx_task, s_waiter;
static uint32_t s_freq = 100000000, s_rate = SDR_SAMPLE_RATE;

/* counter-mode check state */
static bool s_check;
static uint8_t s_next;
static bool s_synced;
static uint64_t s_lost;

#define NGAIN 64
static int s_gain_tenths[NGAIN];              /* [0] unused (AGC), then every tuner gain step */
static char s_gain_lbl[NGAIN][24];
static int s_npreset = 1, s_gain_idx, s_want_tenths = -1;

static void rx_cb(unsigned char *buf, uint32_t len, void *ctx)
{
    if (s_check) {
        uint8_t n = s_synced ? s_next : buf[0];
        s_synced = true;
        for (uint32_t i = 0; i < len; i++) {
            if (buf[i] != n) { s_lost += (uint8_t)(buf[i] - n); n = buf[i]; }
            n++;
        }
        s_next = n;
        s_wr += len;
        return;
    }
    uint32_t off = s_wr & (RING_LEN - 1);
    uint32_t first = len < RING_LEN - off ? len : RING_LEN - off;
    memcpy(s_ring + off, buf, first);
    if (first < len) memcpy(s_ring, buf + first, len - first);
    s_wr += len;
}

static void rx_task(void *arg)
{
    s_streaming = true;
    int r = rtlsdr_read_async(s_dev, rx_cb, NULL, BUF_NUM, BUF_LEN);
    s_streaming = false;
    ESP_LOGI(TAG, "stream ended (%d)", r);
    s_rx_task = NULL;
    if (s_waiter) xTaskNotifyGive(s_waiter);
    vTaskDelete(NULL);
}

static void setup_gains(void)
{
    int n = rtlsdr_get_tuner_gains(s_dev, NULL);
    if (n > NGAIN - 1) n = NGAIN - 1;
    if (n > 0) rtlsdr_get_tuner_gains(s_dev, s_gain_tenths + 1);
    strcpy(s_gain_lbl[0], "AGC");
    s_npreset = 1 + (n > 0 ? n : 0);
    for (int i = 1; i < s_npreset; i++)
        snprintf(s_gain_lbl[i], sizeof s_gain_lbl[0], "%d.%d dB", s_gain_tenths[i] / 10, s_gain_tenths[i] % 10);
}

bool sdr_open(uint32_t timeout_ms, char *desc, size_t desc_len)
{
    if (!s_ring) s_ring = heap_caps_malloc(RING_LEN, MALLOC_CAP_SPIRAM);
    if (rtlsdr_usb_start() != ESP_OK) return false;
    int64_t until = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (rtlsdr_get_device_count() == 0) {
        static int64_t last_log;
        usb_host_lib_info_t li;
        if (esp_timer_get_time() - last_log > 5000000 && usb_host_lib_info(&li) == ESP_OK) {
            ESP_LOGI(TAG, "waiting for the dongle: %d USB device(s) up (the kit's hub is one)", li.num_devices);
            last_log = esp_timer_get_time();
        }
        if (esp_timer_get_time() > until) return false;
        rtlsdr_usb_wait_new_device(500);
    }
    esp_log_level_set("USBH", ESP_LOG_NONE);     /* tuner probing STALLs EP0 on purpose */
    int r = rtlsdr_open(&s_dev, 0);
    esp_log_level_set("USBH", ESP_LOG_WARN);
    if (r < 0) { ESP_LOGE(TAG, "rtlsdr_open %d", r); s_dev = NULL; return false; }
    rtlsdr_set_sample_rate(s_dev, s_rate);
    rtlsdr_set_center_freq(s_dev, s_freq);
    char manufacturer[256] = {0}, product[256] = {0};
    if (rtlsdr_get_usb_strings(s_dev, manufacturer, product, NULL) < 0) {
        manufacturer[0] = product[0] = 0;
    }
    s_direct_supported = rf_direct_sampling_supported(rtlsdr_get_tuner_type(s_dev), manufacturer, product);
    if (!s_direct_supported) s_direct_mode = 0;
    else if (s_direct_mode && rtlsdr_set_direct_sampling(s_dev, s_direct_mode)) {
        ESP_LOGE(TAG, "could not restore direct ADC mode %d", s_direct_mode);
        s_direct_mode = 0;
        rtlsdr_set_direct_sampling(s_dev, 0);
    }
    s_bias_forced = rtlsdr_get_bias_tee_forced(s_dev) > 0;
    /* Never automatically re-energize the antenna on a USB reconnect. */
    s_bias = (bias_state_t){.on = s_bias_forced};
    bool bias_ok = rtlsdr_set_bias_tee(s_dev, s_bias_forced) == 0;
    bias_state_note_result(&s_bias, s_bias_forced, bias_ok);
    if (!bias_ok) ESP_LOGW(TAG, "bias-tee GPIO command failed: antenna power state unknown");
    if (s_bias_forced) ESP_LOGW(TAG, "bias tee forced ON by dongle EEPROM; UI cannot turn it off");
    setup_gains();
    sdr_set_gain_tenths(s_want_tenths);
    if (desc) {
        char m[256], p[256], sn[256];
        rtlsdr_get_usb_strings(s_dev, m, p, sn);
        static const char *tn[] = {"?", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D"};
        int t = rtlsdr_get_tuner_type(s_dev);
        snprintf(desc, desc_len, "%s %s, %s", m, p, tn[t >= 0 && t < 7 ? t : 0]);
    }
    return true;
}

bool sdr_close(void)
{
    sdr_stop();
    if (s_rx_task) {
        ESP_LOGE(TAG, "RX task did not stop: refusing to close an active USB device");
        return false;
    }
    if (s_dev && !s_bias_forced) {
        bool off_ok = rtlsdr_set_bias_tee(s_dev, 0) == 0;
        bias_state_note_result(&s_bias, false, off_ok);
        if (!off_ok) ESP_LOGW(TAG, "bias-tee OFF failed on close: antenna power state unknown");
    }
    if (s_dev) rtlsdr_close(s_dev);
    s_dev = NULL;
    s_bias.command_ok = false;
    s_bias_forced = s_direct_supported = false;
    return true;
}

void sdr_start(void)
{
    if (!s_dev || s_rx_task) return;
    rtlsdr_reset_buffer(s_dev);
    s_streaming = true;                       /* before the task runs, so the UI never sees a gap */
    xTaskCreatePinnedToCore(rx_task, "rtl_rx", 6144, NULL, 20, &s_rx_task, 0);
}

void sdr_stop(void)
{
    if (!s_rx_task) return;
    s_waiter = xTaskGetCurrentTaskHandle();
    rtlsdr_cancel_async(s_dev);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
    s_waiter = NULL;
}

bool sdr_streaming(void) { return s_streaming; }

bool sdr_link_check(int ms, uint64_t *lost, double *bytes_per_s)
{
    if (!s_dev) return false;
    rtlsdr_set_testmode(s_dev, 1);
    s_check = true; s_synced = false; s_lost = 0;
    uint64_t w0 = s_wr;
    int64_t t0 = esp_timer_get_time();
    sdr_start();
    vTaskDelay(pdMS_TO_TICKS(ms));
    uint64_t got = s_wr - w0;
    double dt = (esp_timer_get_time() - t0) / 1e6;
    sdr_stop();
    s_check = false;
    rtlsdr_set_testmode(s_dev, 0);
    *lost = s_lost;
    *bytes_per_s = got / dt;
    return s_lost == 0 && got / dt > s_rate * 2 * 0.97;
}

void sdr_set_freq(uint32_t hz)
{
    if (hz < 500000) hz = 500000;           /* V4: HF via upconversion from 500 kHz */
    if (hz > 1766000000) hz = 1766000000;
    s_freq = hz;
    if (s_dev) rtlsdr_set_center_freq(s_dev, hz);
}

uint32_t sdr_freq(void) { return s_freq; }
int sdr_gain_presets(void) { return s_npreset; }
int sdr_gain_index(void) { return s_gain_idx; }
const char *sdr_gain_label(int i) { return (i >= 0 && i < s_npreset) ? s_gain_lbl[i] : "?"; }

int sdr_gain_tenths(int i) { return (i > 0 && i < s_npreset) ? s_gain_tenths[i] : -1; }

void sdr_set_gain_tenths(int tenths)
{
    s_want_tenths = tenths;
    if (!s_dev) return;                          /* applied in sdr_open */
    int best = 0;
    if (tenths >= 0)
        for (int i = 1; i < s_npreset; i++)
            if (!best || abs(s_gain_tenths[i] - tenths) < abs(s_gain_tenths[best] - tenths)) best = i;
    sdr_set_gain_index(best);
}

void sdr_set_gain_index(int i)
{
    if (i < 0 || i >= s_npreset) i = 0;
    if (!s_dev) return;
    s_gain_idx = i;
    s_want_tenths = sdr_gain_tenths(i);
    if (i == 0) {
        rtlsdr_set_tuner_gain_mode(s_dev, 0);
    } else {
        rtlsdr_set_tuner_gain_mode(s_dev, 1);
        rtlsdr_set_tuner_gain(s_dev, s_gain_tenths[i]);
    }
}

uint64_t sdr_ring_latest(uint8_t *dst, size_t n)
{
    uint64_t w = s_wr;
    uint64_t start = w - n;
    uint32_t off = start & (RING_LEN - 1);
    uint32_t first = n < RING_LEN - off ? n : RING_LEN - off;
    memcpy(dst, s_ring + off, first);
    if (first < n) memcpy(dst + first, s_ring, n - first);
    return w;
}

uint64_t sdr_total_bytes(void) { return s_wr; }

void sdr_set_sample_rate(uint32_t hz)
{
    s_rate = hz;
    if (s_dev) rtlsdr_set_sample_rate(s_dev, hz);
}

uint32_t sdr_sample_rate(void) { return s_rate; }

bool sdr_bias_tee(void) { return s_bias.on; }
bool sdr_bias_command_ok(void) { return s_bias.command_ok; }
bool sdr_bias_tee_forced(void) { return s_bias_forced; }
bool sdr_direct_supported(void) { return s_direct_supported; }
int sdr_direct_mode(void) { return s_direct_mode; }

bool sdr_set_bias_tee(bool on)
{
    if (!s_dev || (s_bias_forced && !on)) return false;
    if (!bias_state_needs_command(&s_bias, on)) return true;
    bool ok = rtlsdr_set_bias_tee(s_dev, on) == 0;
    bias_state_note_result(&s_bias, on, ok);
    if (!ok) return false;
    ESP_LOGI(TAG, "bias tee %s", on ? "ON" : "OFF");
    return true;
}

bool sdr_set_direct_mode(int mode)
{
    if (!s_dev || mode < 0 || mode > 2 || (mode && !s_direct_supported)) return false;
    if (mode == s_direct_mode) return true;
    bool was_streaming = s_rx_task != NULL;
    if (was_streaming) {
        sdr_stop();                       /* tuner init/exit and demod registers: no active USB IQ */
        if (s_rx_task) return false;     /* failed to quiesce; leave hardware untouched */
    }
    int old = s_direct_mode;
    int r = rtlsdr_set_direct_sampling(s_dev, mode);
    if (r != 0) {
        ESP_LOGE(TAG, "direct ADC mode %d failed (%d), reverting", mode, r);
        rtlsdr_set_direct_sampling(s_dev, old);
    } else {
        s_direct_mode = mode;
        ESP_LOGI(TAG, "ADC input: %s", mode == 1 ? "direct I" : mode == 2 ? "direct Q" : "tuner/auto");
    }
    if (was_streaming) sdr_start();
    return r == 0;
}

size_t sdr_ring_read(uint8_t *dst, uint64_t *pos, size_t max, uint64_t *dropped)
{
    uint64_t w = s_wr;
    if (*pos > w) *pos = w;
    if (w - *pos > RING_LEN - 128 * 1024) {      /* reader fell behind: skip to recent data */
        uint64_t to = w - RING_LEN / 4;
        if (dropped) *dropped += to - *pos;
        *pos = to;
    }
    size_t n = w - *pos < max ? (size_t)(w - *pos) : max;
    n &= ~(size_t)1;                             /* whole IQ pairs */
    uint32_t off = *pos & (RING_LEN - 1);
    uint32_t first = n < RING_LEN - off ? n : RING_LEN - off;
    memcpy(dst, s_ring + off, first);
    if (first < n) memcpy(dst + first, s_ring, n - first);
    *pos += n;
    return n;
}
