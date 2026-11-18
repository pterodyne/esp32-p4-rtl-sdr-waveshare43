/* SPDX-License-Identifier: GPL-2.0-or-later
 * HackRF One RX-only front end. Shared sdr.h contract: unsigned interleaved IQ.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "rtlsdr_usb.h"
#include "hackrf_stream.h"
#include "sdr.h"
#include "bias_state.h"

#define RING_LEN (1<<20)
static const char *TAG="hackrf";
static libusb_device_handle *dev;
static uint8_t *ring;
/* P4 is 32-bit: publish 64-bit ring counters with a spinlock, not volatile. */
static portMUX_TYPE ring_lock=portMUX_INITIALIZER_UNLOCKED;
static uint64_t written;
static atomic_bool streaming, owner;
static uint32_t freq=100000000,rate=SDR_SAMPLE_RATE;
static bias_state_t bias;
static int gain_idx;
static bool rate_ok;
static int stage_db[2]={0,40}; /* VGA, LNA; independent desired/applied values */
static bool stage_ok[2];
int sdr_stage_gain(bool lna) { return stage_db[lna]; }
bool sdr_stage_known(bool lna) { return dev && stage_ok[lna]; }
bool sdr_set_stage_gain(bool lna, int db) {
    if(db<0 || db>(lna?40:62) || db%(lna?8:2)) return false;
    if(dev && hackrf_rx_stage(dev,lna,db)) { stage_ok[lna]=false; return false; }
    stage_db[lna]=db; stage_ok[lna]=dev!=NULL; return true;
}
static hf_diag_state_t diag;
/* No AGC on HackRF. Explicit paired manual settings, amp always disabled. */
static const unsigned lna[]={0,8,16,24,32,40,40,40,40};
static const unsigned vga[]={0,0,0,0,0,0,16,32,62};
static const char *labels[]={"L0 V0","L8 V0","L16 V0","L24 V0","L32 V0","L40 V0","L40 V16","L40 V32","L40 V62"};
#define NGAINS ((int)(sizeof(lna)/sizeof(lna[0])))

static void rx_sink(unsigned char *p,uint32_t n,void *ctx) {
    (void)ctx;
    hackrf_rx_unsigned(p,n);
    uint64_t w=sdr_total_bytes();
    unsigned off=w&(RING_LEN-1),first=n<RING_LEN-off?n:RING_LEN-off;
    memcpy(ring+off,p,first);
    if(first<n) memcpy(ring,p+first,n-first);
    portENTER_CRITICAL(&ring_lock); written=w+n; portEXIT_CRITICAL(&ring_lock);
}
static void rx_task(void *arg) {
    (void)arg;
    int rc=hackrf_stream_run(dev,rx_sink,NULL);
    atomic_store(&streaming,false);
    ESP_LOGI(TAG,"RX ended (%d)",rc);
    atomic_store(&owner,false);
    vTaskDelete(NULL);
}
bool sdr_open(uint32_t timeout_ms,char *desc,size_t desc_len) {
    if(dev) return false;
    /* Independent desired stage values survive reopen. */
    if(!ring) ring=heap_caps_calloc(1,RING_LEN,MALLOC_CAP_SPIRAM);
    if(!ring || rtlsdr_usb_start()!=ESP_OK) return false;
    int64_t until=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    do {
        libusb_device **list=NULL;
        ssize_t n=libusb_get_device_list(NULL,&list);
        for(ssize_t i=0;i<n;i++) {
            struct libusb_device_descriptor d;
            libusb_get_device_descriptor(list[i],&d);
            if(d.idVendor!=0x1d50 || d.idProduct!=0x6089) continue;
            if(libusb_open(list[i],&dev)) continue;
            /* Standard HackRF One: interface 0 alt 0, HS bulk IN 0x81/512.
             * Reject unexpected firmware/configuration rather than submit to RTL's 0x81 blindly. */
            if(libusb_validate_bulk_in(dev,0x81) || libusb_claim_interface(dev,0)) {
                libusb_close(dev); dev=NULL; continue;
            }
            if(hackrf_rx_clear_bias_options(dev,d.bcdDevice) || hackrf_rx_safe(dev) || hackrf_rx_rate(dev,rate) || hackrf_rx_tune(dev,freq) ||
               hackrf_rx_gain(dev,stage_db[1],stage_db[0])) {
                hackrf_rx_safe(dev);
                libusb_release_interface(dev,0);libusb_close(dev);dev=NULL;continue;
            }
            rate_ok=true; stage_ok[0]=stage_ok[1]=true;
            memset(&diag,0,sizeof diag);
            hf_diag_identity(&diag,"Great Scott Gadgets","HackRF One (VID/PID)",false);
            bias=(bias_state_t){.on=false,.command_ok=true};
            if(desc) snprintf(desc,desc_len,"HackRF One RX, %.1f MS/s",rate/1e6);
            ESP_LOGW(TAG,"native low-rate RX %.1f MS/s: experimental below recommended 8 MS/s; amp OFF",rate/1e6);
            break;
        }
        libusb_free_device_list(list,1);
        if(dev) return true;
        if(esp_timer_get_time()>=until) break;
        rtlsdr_usb_wait_new_device(100);
    } while(esp_timer_get_time()<until);
    return false;
}
bool sdr_close(void) {
    sdr_stop();
    if(atomic_load(&owner)) return false;
    if(dev) {
        hackrf_rx_safe(dev);
        libusb_release_interface(dev,0);libusb_close(dev);dev=NULL;
    }
    memset(&diag,0,sizeof diag);bias.command_ok=false;
    return true;
}
void sdr_start(void) {
    if(!dev || !rate_ok || atomic_load(&owner)) return;
    hackrf_stream_prepare();
    atomic_store(&owner,true);atomic_store(&streaming,true);
    if(xTaskCreatePinnedToCore(rx_task,"hackrf_rx",6144,NULL,20,NULL,0)!=pdPASS) {
        atomic_store(&owner,false);atomic_store(&streaming,false);
    }
}
void sdr_stop(void) {
    hackrf_stream_stop();
    int64_t until=esp_timer_get_time()+3000000;
    while(atomic_load(&owner) && esp_timer_get_time()<until) vTaskDelay(pdMS_TO_TICKS(10));
    if(atomic_load(&owner)) ESP_LOGE(TAG,"RX still owns USB: close/rate change refused");
}
bool sdr_streaming(void) { return atomic_load(&streaming); }
bool sdr_link_check(int ms,uint64_t *lost,double *bps) {
    /* HackRF has no RTL counter mode. Do NOT claim measured loss-free reception. */
    (void)ms;
    if(lost) *lost=0;
    if(bps) *bps=0;
    return false;
}
void sdr_set_freq(uint32_t hz) {
    /* App's uint32 frequency/UI cannot expose HackRF's full 6 GHz range yet. */
    if(hz<1000000) hz=1000000;
    if(hz>1766000000) hz=1766000000;
    if(!dev || hackrf_rx_tune(dev,hz)==0) freq=hz;
    else ESP_LOGE(TAG,"tune command failed");
}
uint32_t sdr_freq(void) {return freq;}
hf_diag_state_t sdr_hf_diag_snapshot(void) {return diag;}
int sdr_gain_presets(void) {return NGAINS;}
int sdr_gain_index(void) {return gain_idx;}
const char *sdr_gain_label(int i) {
    /* Current label reflects each independently acknowledged stage. */
    if(i==gain_idx) {
        static char label[24]; char a[8],b[8];
        if(sdr_stage_known(true)) snprintf(a,sizeof a,"%d",stage_db[1]); else strcpy(a,"?");
        if(sdr_stage_known(false)) snprintf(b,sizeof b,"%d",stage_db[0]); else strcpy(b,"?");
        snprintf(label,sizeof label,"L%s V%s",a,b); return label;
    }
    return i>=0 && i<NGAINS?labels[i]:"?";
}
int sdr_gain_tenths(int i) {return i>=0 && i<NGAINS?(int)(lna[i]+vga[i])*10:-1;}
void sdr_set_gain_index(int i) {
    if(i<0 || i>=NGAINS) return;
    if(dev && hackrf_rx_gain(dev,lna[i],vga[i])) {stage_ok[0]=stage_ok[1]=false;ESP_LOGE(TAG,"gain command failed; LNA/VGA state unknown");return;}
    gain_idx=i;
    stage_db[1]=lna[i]; stage_db[0]=vga[i];
    stage_ok[0]=stage_ok[1]=dev!=NULL;
}
void sdr_set_gain_tenths(int tenths) {
    /* Legacy saved AGC maps to manual L40/V0, never to fictitious HackRF AGC. */
    if(tenths<0) tenths=400;
    int best=0;
    for(int i=1;i<NGAINS;i++) if(abs(sdr_gain_tenths(i)-tenths)<abs(sdr_gain_tenths(best)-tenths)) best=i;
    sdr_set_gain_index(best);
}
void sdr_set_sample_rate(uint32_t hz) {
    if(hz!=2400000 && hz!=2000000) {ESP_LOGE(TAG,"unsupported application IQ rate");return;}
    if(atomic_load(&owner)) {ESP_LOGE(TAG,"stop RX before rate change");return;}
    if(!dev || hackrf_rx_rate(dev,hz)==0) {rate=hz;rate_ok=true;}
    else {
        /* The clock write may have succeeded before the filter write failed.
         * Restore both, or refuse RX until reopen rather than feed mislabeled IQ. */
        rate_ok=hackrf_rx_rate(dev,rate)==0;
        ESP_LOGE(TAG,"sample rate command failed; previous rate %s",rate_ok?"restored":"UNKNOWN: RX disabled");
    }
}
uint32_t sdr_sample_rate(void) {return rate;}
bool sdr_bias_tee(void) {return bias.on;}
bool sdr_bias_command_ok(void) {return bias.command_ok;}
bool sdr_bias_tee_forced(void) {return false;}
bool sdr_set_bias_tee(bool on) {
    if(!dev) return false;
    bool ok=hackrf_rx_bias(dev,on)==0;
    bias_state_note_result(&bias,on,ok);return ok;
}
bool sdr_direct_supported(void) {return false;}
int sdr_direct_mode(void) {return 0;}
bool sdr_set_direct_mode(int mode) {return mode==0;}
uint64_t sdr_total_bytes(void) {
    portENTER_CRITICAL(&ring_lock); uint64_t w=written; portEXIT_CRITICAL(&ring_lock);return w;
}
uint64_t sdr_ring_latest(uint8_t *dst,size_t n) {
    uint64_t w=sdr_total_bytes();
    if(!ring || n>RING_LEN) return w;
    unsigned off=(w-n)&(RING_LEN-1),first=n<RING_LEN-off?n:RING_LEN-off;
    memcpy(dst,ring+off,first);if(first<n) memcpy(dst+first,ring,n-first);return w;
}
size_t sdr_ring_read(uint8_t *dst,uint64_t *pos,size_t max,uint64_t *dropped) {
    uint64_t w=sdr_total_bytes();
    if(*pos>w) *pos=w;
    if(w-*pos>RING_LEN-128*1024) {
        uint64_t to=w-RING_LEN/4;if(dropped) *dropped+=to-*pos;*pos=to;
    }
    size_t n=w-*pos<max?w-*pos:max;n&=~(size_t)1;
    unsigned off=*pos&(RING_LEN-1),first=n<RING_LEN-off?n:RING_LEN-off;
    if(n && ring) {memcpy(dst,ring+off,first);if(first<n) memcpy(dst+first,ring,n-first);}
    *pos+=n;return n;
}
