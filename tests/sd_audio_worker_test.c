#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include "sd_audio.h"
#include "wav_audio.h"
#include "freertos/queue.h"
#include "freertos/task.h"
struct queue { pthread_mutex_t lock; unsigned char data[8][128]; unsigned head,tail; size_t size; };
QueueHandle_t xQueueCreate(unsigned n,size_t size) {assert(n==8 && size<=128); struct queue *q=calloc(1,sizeof *q);pthread_mutex_init(&q->lock,0);q->size=size;return q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait) {(void)wait;pthread_mutex_lock(&q->lock);int ok=q->head-q->tail<8;if(ok)memcpy(q->data[q->head++%8],p,q->size);pthread_mutex_unlock(&q->lock);return ok;}
static _Atomic bool check_cancelled;
static void verify_cancelled(void);
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait) {verify_cancelled();pthread_mutex_lock(&q->lock);int ok=q->head!=q->tail;if(ok)memcpy(p,q->data[q->tail++%8],q->size);pthread_mutex_unlock(&q->lock);if(!ok && wait)usleep(wait*1000);return ok;}
void vQueueDelete(QueueHandle_t q) {free(q);}
void vTaskDelay(unsigned n) {usleep(n*1000);}
static void (*task)(void *);
static void *run(void *unused) {(void)unused;task(0);return 0;}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,int pri,void *handle,int core) {(void)name;(void)stack;(void)arg;(void)pri;(void)handle;(void)core;task=fn;return 1;}
static _Atomic int cancel_mount, cancel_open;
static _Atomic bool cancel_page_exit;
static _Atomic bool near_limit;
static _Atomic bool fail_read,fail_write,fail_flush,fail_close,arm_read;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
int __real_fflush(FILE *);
int __real_fclose(FILE *);
size_t __wrap_fread(void *p,size_t size,size_t n,FILE *f) {
    if(atomic_exchange(&fail_read,false)) return 0;
    return __real_fread(p,size,n,f);
}
size_t __wrap_fwrite(const void *p,size_t size,size_t n,FILE *f) {
    if(atomic_exchange(&fail_write,false)) return __real_fwrite(p,1,3,f);
    return __real_fwrite(p,size,n,f);
}
int __wrap_fflush(FILE *f) {
    int rc=__real_fflush(f);return atomic_exchange(&fail_flush,false) ? EOF : rc;
}
int __wrap_fclose(FILE *f) {
    int rc=__real_fclose(f);return atomic_exchange(&fail_close,false) ? EOF : rc;
}
static void verify_cancelled(void) {
    if(check_cancelled) {
        sd_audio_snapshot_t s;sd_audio_snapshot(&s);
        assert(s.state==SD_AUDIO_IDLE && !sd_audio_playing());
        check_cancelled=false;
    }
}
static void cancel_start(void) {
    if(cancel_page_exit) {
        int16_t loading[4]={1,2,3,4};
        assert(sd_audio_output_exclusive());sd_audio_exchange(loading,4);
        for(int i=0;i<4;i++)assert(!loading[i]); /* still silent inside mount/header preparation */
        sd_audio_playback_page(false);cancel_page_exit=false;
    } else assert(sd_audio_request(SD_AUDIO_STOP,0));
    check_cancelled=true;
}
bool __real_wav_begin(wav_writer_t *,FILE *);
bool __real_wav_open(wav_reader_t *,FILE *);
bool __wrap_wav_begin(wav_writer_t *w,FILE *f) {
    bool ok=__real_wav_begin(w,f);if(near_limit){w->bytes=WAV_AUDIO_MAX_BYTES-2;near_limit=false;}if(cancel_open){cancel_open=0;cancel_start();}return ok;
}
bool __wrap_wav_open(wav_reader_t *r,FILE *f) {
    bool ok=__real_wav_open(r,f);if(atomic_exchange(&arm_read,false))fail_read=true;if(cancel_open){cancel_open=0;cancel_start();}return ok;
}
static bool card=true;
static int refs;
bool sdcard_mount(void) {if(cancel_mount){cancel_mount=0;cancel_start();}if(!card)return false;__atomic_add_fetch(&refs,1,__ATOMIC_SEQ_CST);return true;}
void sdcard_unmount(void) {assert(__atomic_sub_fetch(&refs,1,__ATOMIC_SEQ_CST)>=0);}
static sd_audio_snapshot_t snap(void) {sd_audio_snapshot_t s;sd_audio_snapshot(&s);return s;}
static sd_audio_snapshot_t until(int wanted,const char *text) {for(int i=0;i<5000;i++){sd_audio_snapshot_t s=snap();if(s.state==wanted && (!text||strstr(s.status,text)))return s;usleep(1000);}fprintf(stderr,"Timeout waiting %d %s (got %s)\n",wanted,text?text:"",snap().status);abort();}
void test_audio_route_runtime(void);
int main(void) {
    assert(sd_audio_init());
    /* Page ownership must silence idle live PCM without touching codec settings. */
    sd_audio_playback_page(true);
    int16_t idle_pcm[4]={11,-22,33,-44};
    sd_audio_exchange(idle_pcm,4);
    assert(idle_pcm[0]==0 && idle_pcm[1]==0 && idle_pcm[2]==0 && idle_pcm[3]==0);
    sd_audio_playback_page(false);
    for(int i=0;i<8;i++)assert(sd_audio_request(SD_AUDIO_RECORD,0));
    assert(!sd_audio_request(SD_AUDIO_RECORD,0));
    assert(sd_audio_request(SD_AUDIO_STOP,0)); /* STOP must work even if command queue is full */
    pthread_t thread;assert(!pthread_create(&thread,0,run,0));usleep(100000);
    assert(snap().state==SD_AUDIO_IDLE); /* stale queued starts cancelled by STOP */
    /* Inject STOP inside real worker preparation, not before dequeue. */
    for(int preparation=0;preparation<2;preparation++) {
        if(preparation) cancel_open=1;else cancel_mount=1;
        assert(sd_audio_request(SD_AUDIO_RECORD,0));usleep(100000);
        assert(!check_cancelled && refs==0 && snap().state==SD_AUDIO_IDLE);
        unlink("host-card/AUDIO/RX000001.WAV");
    }
    assert(!sd_audio_request(SD_AUDIO_PLAY,"../evil.WAV"));
    assert(sd_audio_request(SD_AUDIO_REFRESH,0));until(SD_AUDIO_IDLE,"select");assert(snap().count==0);
    assert(sd_audio_request(SD_AUDIO_RECORD,0));until(SD_AUDIO_RECORDING,"REC:");
    int16_t pcm[480];for(int i=0;i<480;i++)pcm[i]=(int16_t)(i-240);
    for(int i=0;i<10;i++)sd_audio_exchange(pcm,480);
    sd_audio_playback_page(true); /* finalize live source, never record page silence */
    assert(!snap().record_requested);
    assert(!sd_audio_request(SD_AUDIO_RECORD,0));
    until(SD_AUDIO_IDLE,"saved");
    sd_audio_playback_page(false);
    for(int i=0;i<100 && __atomic_load_n(&refs,__ATOMIC_SEQ_CST);i++)usleep(1000);
    assert(refs==0);
    assert(sd_audio_request(SD_AUDIO_REFRESH,0));sd_audio_snapshot_t s=until(SD_AUDIO_IDLE,"select");
    assert(s.count==1 && !strcmp(s.files[0],"RX000001.WAV"));
    for(int page_exit=0;page_exit<2;page_exit++) for(int preparation=0;preparation<2;preparation++) {
        if(page_exit) {sd_audio_playback_page(true);cancel_page_exit=true;}
        if(preparation) cancel_open=1;else cancel_mount=1;
        assert(sd_audio_request(SD_AUDIO_PLAY,s.files[0]));usleep(100000);
        assert(!check_cancelled && !cancel_page_exit && refs==0 && snap().state==SD_AUDIO_IDLE);
        assert(!sd_audio_output_exclusive());
    }
    sd_audio_playback_page(true);
    assert(sd_audio_output_exclusive());
    assert(sd_audio_request(SD_AUDIO_PLAY,s.files[0]));until(SD_AUDIO_PLAYING,"PLAY:");
    assert(!sd_audio_request(SD_AUDIO_RECORD,0));usleep(10000);
    assert(snap().state==SD_AUDIO_PLAYING); /* never record substituted playback */
    usleep(30000);int16_t out[480];memset(out,0,sizeof out);sd_audio_exchange(out,480);assert(!memcmp(out,pcm,sizeof out));
    assert(sd_audio_request(SD_AUDIO_STOP,0));assert(!sd_audio_playing());
    until(SD_AUDIO_IDLE,"Playback stopped");
    memcpy(out,pcm,sizeof out);sd_audio_exchange(out,480);
    for(int i=0;i<480;i++)assert(out[i]==0);
    assert(sd_audio_output_exclusive()); /* STOP never releases page ownership */
    assert(sd_audio_request(SD_AUDIO_PLAY,"RX000001.WAV"));until(SD_AUDIO_PLAYING,"PLAY:");usleep(30000);
    for(int i=0;i<20;i++){sd_audio_exchange(out,480);usleep(2000);}until(SD_AUDIO_IDLE,"Playback stopped");
    memcpy(out,pcm,sizeof out);sd_audio_exchange(out,480);
    for(int i=0;i<480;i++)assert(out[i]==0); /* EOF remains live-silent */
    FILE *badfile=fopen("host-card/AUDIO/BAD.WAV","wb");assert(badfile);fputs("not WAV",badfile);fclose(badfile);
    assert(sd_audio_request(SD_AUDIO_PLAY,"BAD.WAV"));until(SD_AUDIO_IDLE,"Need PCM");
    memcpy(out,pcm,sizeof out);sd_audio_exchange(out,480);
    for(int i=0;i<480;i++)assert(out[i]==0); /* invalid WAV stays silent too */
    sd_audio_playback_page(false);
    assert(!sd_audio_output_exclusive());
    memcpy(out,pcm,sizeof out);sd_audio_exchange(out,480);assert(!memcmp(out,pcm,sizeof out));
    test_audio_route_runtime();
    usleep(5000);assert(refs==0);assert(!unlink("host-card/AUDIO/BAD.WAV"));
    /* Exclusive naming: the second recording must not overwrite the first. */
    assert(sd_audio_request(SD_AUDIO_RECORD,0));until(SD_AUDIO_RECORDING,"REC:");assert(!strcmp(snap().name,"RX000002.WAV"));
    assert(sd_audio_request(SD_AUDIO_STOP,0));until(SD_AUDIO_IDLE,"saved");
    /* Overrun fails visibly, finalizes accepted samples and releases mount. */
    assert(sd_audio_request(SD_AUDIO_RECORD,0));until(SD_AUDIO_RECORDING,"REC:");
    int16_t *large=calloc(70000,2);assert(large);sd_audio_exchange(large,70000);free(large);until(SD_AUDIO_IDLE,"buffer error");
    usleep(5000);assert(refs==0);
    /* Real-worker I/O failure paths: short odd write, flush and close errors. */
    for(int failure=0;failure<3;failure++) {
        assert(sd_audio_request(SD_AUDIO_RECORD,0));until(SD_AUDIO_RECORDING,"REC:");
        char path[100];snprintf(path,sizeof path,"host-card/AUDIO/%s",snap().name);
        if(failure==0) {fail_write=true;sd_audio_exchange(pcm,480);}
        else {
            if(failure==1) fail_flush=true;else fail_close=true;
            assert(sd_audio_request(SD_AUDIO_STOP,0));
        }
        until(SD_AUDIO_IDLE,"buffer error");usleep(5000);assert(refs==0);
        if(failure==0) {
            FILE *f=fopen(path,"rb");assert(f);wav_reader_t r;
            assert(wav_open(&r,f) && r.remaining==2);fclose(f);
        }
        assert(!unlink(path));
    }
    arm_read=true;assert(sd_audio_request(SD_AUDIO_PLAY,"RX000001.WAV"));
    until(SD_AUDIO_IDLE,"read error");assert(!fail_read);
    usleep(5000);assert(refs==0);
    /* Counter seam tests the actual worker boundary without a 2-GiB fixture. */
    near_limit=true;assert(sd_audio_request(SD_AUDIO_RECORD,0));until(SD_AUDIO_RECORDING,"REC:");
    sd_audio_exchange(pcm,480);until(SD_AUDIO_IDLE,"size limit");
    usleep(5000);assert(refs==0);
    char limit_path[100];snprintf(limit_path,sizeof limit_path,"host-card/AUDIO/%s",snap().name);
    FILE *limit=fopen(limit_path,"rb");assert(limit);assert(!fseek(limit,0,SEEK_END));
    assert(ftell(limit)==46);fclose(limit);assert(!unlink(limit_path));
    for(int i=0;i<70;i++) {
        char path[80];snprintf(path,sizeof path,"host-card/AUDIO/EX%06d.WAV",i);
        FILE *f=fopen(path,"wb");assert(f);wav_writer_t w;assert(wav_begin(&w,f));assert(wav_finish(&w));fclose(f);
    }
    assert(sd_audio_request(SD_AUDIO_REFRESH,0));s=until(SD_AUDIO_IDLE,"select");
    assert(s.count==64 && s.more && s.batch==0);
    assert(sd_audio_request(SD_AUDIO_NEXT,0));
    for(int i=0;i<5000 && snap().batch!=1;i++)usleep(1000);
    s=snap();assert(s.batch==1 && s.count==9 && !s.more);
    assert(sd_audio_request(SD_AUDIO_PREV,0));
    for(int i=0;i<5000 && snap().batch!=0;i++)usleep(1000);
    assert(snap().batch==0);
    card=false;assert(sd_audio_request(SD_AUDIO_REFRESH,0));until(SD_AUDIO_IDLE,"No SD");assert(refs==0);assert(snap().count==0);
    card=true;
    FILE *del=fopen("host-card/AUDIO/RX999999.WAV","wb");assert(del);
    wav_writer_t dw;assert(wav_begin(&dw,del));
    for(int i=0;i<10;i++) assert(wav_append(&dw,pcm,480));
    assert(wav_finish(&dw));fclose(del);
    /* DELETE rejects arbitrary basenames and traversal, handles noSD, and closes
     * playback before removing a real recording without releasing page silence. */
    assert(!sd_audio_request(SD_AUDIO_DELETE,"../RX999999.WAV"));
    assert(!sd_audio_request(SD_AUDIO_DELETE,"BAD.WAV"));
    card=false;assert(sd_audio_request(SD_AUDIO_DELETE,"RX999999.WAV"));
    usleep(100000);until(SD_AUDIO_IDLE,"No SD");assert(!access("host-card/AUDIO/RX999999.WAV",F_OK));
    card=true;
    cancel_mount=1;assert(sd_audio_request(SD_AUDIO_DELETE,"RX999999.WAV"));
    until(SD_AUDIO_IDLE,"Delete cancelled");assert(!access("host-card/AUDIO/RX999999.WAV",F_OK));
    assert(!symlink("RX999999.WAV","host-card/AUDIO/RX999998.WAV"));
    assert(sd_audio_request(SD_AUDIO_DELETE,"RX999998.WAV"));until(SD_AUDIO_IDLE,"Delete failed");
    assert(!access("host-card/AUDIO/RX999999.WAV",F_OK));assert(!unlink("host-card/AUDIO/RX999998.WAV"));
    sd_audio_playback_page(true);
    assert(sd_audio_request(SD_AUDIO_PLAY,"RX999999.WAV"));until(SD_AUDIO_PLAYING,"PLAY:");
    assert(sd_audio_request(SD_AUDIO_DELETE,"RX999999.WAV"));
    until(SD_AUDIO_IDLE,"Deleted");assert(access("host-card/AUDIO/RX999999.WAV",F_OK));
    int16_t silence[4]={1,2,3,4};sd_audio_exchange(silence,4);
    for(int i=0;i<4;i++) assert(!silence[i]);
    assert(sd_audio_output_exclusive());sd_audio_playback_page(false);
    /* Explicit request metadata stays identical while queued/preparing. */
    uint32_t hz=99100000;int bw=0;int64_t utc=1794866730;
    char metadata[64];assert(wav_record_name(metadata,sizeof metadata,hz,bw,utc,0));
    assert(sd_audio_request(SD_AUDIO_RECORD,metadata));
    hz=7100100;bw=4;utc=0; /* later retune/mode/clock must not affect queued request */
    assert(wav_record_name(metadata,sizeof metadata,hz,bw,utc,0));
    until(SD_AUDIO_RECORDING,"REC:");
    const char *meta="261116-220530_99.1M_FM.wav";
    assert(!strcmp(snap().name,meta));
    assert(sd_audio_request(SD_AUDIO_STOP,0));until(SD_AUDIO_IDLE,"saved");
    assert(sd_audio_request(SD_AUDIO_RECORD,meta));until(SD_AUDIO_RECORDING,"REC:");
    assert(!strcmp(snap().name,"261116-220530_99.1M_FM_1.wav"));
    assert(sd_audio_request(SD_AUDIO_STOP,0));until(SD_AUDIO_IDLE,"saved");
    const char *unsync="unsynced_100.101k_LSB_1.wav";
    for(int i=1;i<=2;i++) {
        assert(sd_audio_request(SD_AUDIO_RECORD,unsync));until(SD_AUDIO_RECORDING,"REC:");
        char expected[64];snprintf(expected,sizeof expected,"unsynced_100.101k_LSB_%d.wav",i);
        assert(!strcmp(snap().name,expected));assert(wav_recording_name(snap().name));
        assert(sd_audio_request(SD_AUDIO_STOP,0));until(SD_AUDIO_IDLE,"saved");
    }
    pthread_cancel(thread);pthread_join(thread,0);
    puts("SD worker: STOP during mount/open REC/PLAY and full queue, size-cap clean stop, injected read/odd-write/flush/close errors, real files, record/drain, exclusive names, browser, playback/EOF/live restore, overflow, noSD, mount balance passed");
}
