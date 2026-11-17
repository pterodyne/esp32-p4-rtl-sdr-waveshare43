/* Executes the actual audio.c task body (extracted at build time, not a model),
 * with deterministic IQ/codec seams and the real concurrently running SD worker. */
#include <stdio.h>
#define ESP_LOGE(...) ((void)0)
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "audio.h"
#include "sdr.h"
#include "sd_audio.h"
#include "audio_pause_gate.h"
#include "audio_output_policy.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
typedef void *esp_codec_dev_handle_t;
static jmp_buf written;
static bool codec_muted;
static bool live_forbidden;
static int codec_volume, writes;
static int16_t speaker[128];
static void esp_codec_dev_set_out_mute(void *d, bool on) {(void)d;codec_muted=on;}
static void esp_codec_dev_set_out_vol(void *d, int v) {(void)d;codec_volume=v;}
static void esp_codec_dev_write(void *d, const void *p, size_t n) {
    (void)d;assert(n<=sizeof speaker);memcpy(speaker,p,n);writes++;longjmp(written,1);
}
static int64_t esp_timer_get_time(void) {static int64_t t;return ++t;}
static void vTaskDelete(void *p) {(void)p;abort();}
uint64_t sdr_total_bytes(void) {assert(!live_forbidden);return 96000;}
size_t sdr_ring_read(uint8_t *p,uint64_t *pos,size_t n,uint64_t *lost) {
    (void)lost;memset(p,128,n);*pos+=n;return n;
}
void demod_set(demod_mode_t m,uint32_t b) {(void)m;(void)b;}
void demod_set_squelch(int s) {(void)s;}
void demod_retuned(void) {}
bool demod_open(void) {return true;}
int demod_process(const uint8_t *iq,int n,int16_t *pcm) {
    (void)iq;(void)n;for(int i=0;i<96;i++)pcm[i]=1234;return 96;
}
#include "audio_task_under_test.h"
static void block(void) {if(!setjmp(written))audio_task(NULL);}
static void silent(void) {block();for(int i=0;i<96;i++)assert(speaker[i]==0);}
void test_audio_route_runtime(void) {
    for(int muted=0;muted<=1;muted++) for(int volume=0;volume<=7;volume+=7) {
        audio_output_set_volume(&s_output,volume);
        audio_output_set_mute(&s_output,muted);
        audio_pause_gate_request(&s_pause_gate,false);
        block();assert(speaker[0]==1234);
        bool expected=muted || !volume;
        int old_volume=codec_volume;
        assert(codec_muted==expected);
        sd_audio_playback_page(true);
        live_forbidden=true; /* a missing RF device must not block WAV/page silence */
        silent(); /* idle page, independent of IQ availability */
        assert(sd_audio_output_exclusive());
        assert(codec_muted==expected && codec_volume==old_volume);
        assert(sd_audio_request(SD_AUDIO_STOP,NULL));silent();
        assert(sd_audio_output_exclusive());
        sd_audio_playback_page(false);
        live_forbidden=false;
        block();assert(speaker[0]==1234);
        assert(codec_muted==expected && codec_volume==old_volume);
        assert(audio_output_is_muted(&s_output)==(bool)muted);
        assert(s_output.desired_volume==volume);
    }
    audio_output_set_volume(&s_output,7);audio_output_set_mute(&s_output,false);
    sd_audio_playback_page(true);silent();
    assert(sd_audio_request(SD_AUDIO_PLAY,"RX000001.WAV"));
    for(int i=0;i<5000 && !sd_audio_playing();i++)usleep(1000);
    assert(sd_audio_playing());usleep(30000);block();
    assert(speaker[0]==-240 && !codec_muted); /* WAV, not mock live 1234 or silence */
    for(int i=0;i<100 && sd_audio_playing();i++) {block();usleep(2000);}
    assert(!sd_audio_playing());silent(); /* actual task EOF stays gated */
    assert(sd_audio_request(SD_AUDIO_STOP,NULL));silent();
    for(int i=0;i<5000;i++) {sd_audio_snapshot_t s;sd_audio_snapshot(&s);if(strstr(s.status,"Playback stopped"))break;usleep(1000);}
    silent();sd_audio_playback_page(false);block();assert(speaker[0]==1234);
    assert(writes>20);
    puts("Actual audio task + SD worker: idle/STOP page silence, WAV audible, live restoration, prior mute and volume-zero/7 unchanged passed");
}
