/* One low-priority worker owns all SD operations. No filesystem work on RX/audio/UI tasks.
 * A bounded 128 KiB SPSC PCM ring absorbs ~1.36 s card latency. Overflow fails recording
 * visibly instead of silently splicing missing audio. Mount reference held for each operation. */
#include "sd_audio.h"
#include "wav_audio.h"
#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#define CAP 65536u
#define BLOCK 512u
#define DIR_AUDIO SDCARD_ROOT "/AUDIO"
/* ESP-IDF FAT VFS has no symlinks/lstat; host tests must reject symlinks. */
#ifdef ESP_PLATFORM
#define recording_stat stat
#else
#define recording_stat lstat
#endif
typedef struct { int command; unsigned epoch; char name[SD_AUDIO_NAME]; } request_t;
static QueueHandle_t requests;
static int16_t *ring;
static size_t head, tail;
static int mode;
static bool overflow, stop_requested;
static bool playback_page;
static unsigned command_epoch;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static sd_audio_snapshot_t state;
static void status(const char *text) {
    taskENTER_CRITICAL(&mux);
    snprintf(state.status,sizeof state.status,"%s",text); state.generation++;
    taskEXIT_CRITICAL(&mux);
}
static void setmode(int m) {
    taskENTER_CRITICAL(&mux);
    mode=m; state.state=m; if(m==SD_AUDIO_IDLE) state.record_requested=false; state.generation++;
    taskEXIT_CRITICAL(&mux);
}
void sd_audio_snapshot(sd_audio_snapshot_t *out) {
    taskENTER_CRITICAL(&mux); *out=state; taskEXIT_CRITICAL(&mux);
}
bool sd_audio_playing(void) {
    taskENTER_CRITICAL(&mux); bool on=mode==SD_AUDIO_PLAYING; taskEXIT_CRITICAL(&mux); return on;
}
void sd_audio_playback_page(bool on) {
    taskENTER_CRITICAL(&mux);
    if(playback_page!=on) {
        playback_page=on;
        command_epoch++;stop_requested=true;mode=SD_AUDIO_IDLE;
        state.state=SD_AUDIO_IDLE;state.record_requested=false;state.generation++;
    }
    taskEXIT_CRITICAL(&mux);
}
bool sd_audio_output_exclusive(void) {
    taskENTER_CRITICAL(&mux);
    bool on=playback_page || mode==SD_AUDIO_PLAYING;
    taskEXIT_CRITICAL(&mux);return on;
}
void sd_audio_exchange(int16_t *pcm,size_t n) {
    taskENTER_CRITICAL(&mux);
    if(mode==SD_AUDIO_RECORDING) {
        if(n>CAP-(head-tail)) overflow=true;
        else for(size_t i=0;i<n;i++) ring[head++%CAP]=pcm[i];
    } else if(mode==SD_AUDIO_PLAYING) {
        for(size_t i=0;i<n;i++) pcm[i]=tail<head ? ring[tail++%CAP] : 0;
    } else if(playback_page) {
        memset(pcm,0,n*sizeof *pcm);
    }
    taskEXIT_CRITICAL(&mux);
}
static size_t transfer(int16_t *buf,size_t n,bool push) {
    taskENTER_CRITICAL(&mux);
    size_t avail=push ? CAP-(head-tail) : head-tail;
    if(n>avail) n=avail;
    for(size_t i=0;i<n;i++) {
        if(push) ring[head++%CAP]=buf[i]; else buf[i]=ring[tail++%CAP];
    }
    taskEXIT_CRITICAL(&mux); return n;
}
static int cmp(const void *a,const void *b) { return strcmp(a,b); }
static void list(int command) {
    int batch=command==SD_AUDIO_REFRESH ? 0 : state.batch+(command==SD_AUDIO_NEXT ? 1:-1);
    if(batch<0) batch=0;
    /* Local temporary snapshot prevents a slow directory walk blocking UI polling. */
    char (*names)[SD_AUDIO_NAME]=calloc(SD_AUDIO_FILES,SD_AUDIO_NAME);
    if(!names) {status("No memory for file list");return;}
    if(!sdcard_mount()) {
        taskENTER_CRITICAL(&mux);state.count=0;state.more=false;state.generation++;taskEXIT_CRITICAL(&mux);
        free(names);status("No SD card / mount failed");return;
    }
    mkdir(DIR_AUDIO,0777);
    DIR *d=opendir(DIR_AUDIO); int count=0,skip=batch*SD_AUDIO_FILES; bool more=false;
    if(d) {
        struct dirent *e;
        while((e=readdir(d))) if(wav_safe_name(e->d_name)) {
            char path[sizeof DIR_AUDIO+SD_AUDIO_NAME+4];struct stat st;
            snprintf(path,sizeof path,DIR_AUDIO "/%.63s",e->d_name);
            if(stat(path,&st)!=0 || !S_ISREG(st.st_mode)) continue;
            if(skip) {skip--;continue;}
            if(count==SD_AUDIO_FILES) {more=true;break;}
            snprintf(names[count++],SD_AUDIO_NAME,"%.63s",e->d_name);
        }
        closedir(d);
    }
    qsort(names,count,SD_AUDIO_NAME,cmp);
    taskENTER_CRITICAL(&mux);
    memcpy(state.files,names,SD_AUDIO_FILES*SD_AUDIO_NAME);
    state.count=count;state.more=more;state.batch=batch;state.generation++;
    taskEXIT_CRITICAL(&mux);
    status(!d ? "Cannot list /AUDIO" : more ? "SD /AUDIO - select / NEXT for more" : "SD /AUDIO - select a WAV");
    sdcard_unmount(); free(names);
}
static void worker(void *arg) {
    (void)arg;
    FILE *file=NULL; bool mounted=false,ending=false,bad=false,limit=false;
    wav_writer_t writer={0}; wav_reader_t reader={0};
    int active=SD_AUDIO_IDLE;
    int16_t buf[BLOCK];
    for(;;) {
        request_t req;
        bool got=xQueueReceive(requests,&req,active ? 0 : pdMS_TO_TICKS(20))==pdTRUE;
        taskENTER_CRITICAL(&mux);
        bool stop=stop_requested;stop_requested=false;unsigned epoch=command_epoch;
        taskEXIT_CRITICAL(&mux);
        if(stop) ending=true;
        if(got && req.epoch==epoch) {
            if(req.command==SD_AUDIO_STOP) ending=true;
            else if(req.command==SD_AUDIO_DELETE) {
                /* Request already silenced the PCM consumer. Close its stream on
                 * this worker before unlink; never delete an active recording. */
                if(active==SD_AUDIO_RECORDING) {status("STOP recording before DELETE");continue;}
                if(file) {int rc=fclose(file);file=NULL;active=SD_AUDIO_IDLE;setmode(active);
                    if(mounted) {sdcard_unmount();mounted=false;}
                    if(rc) {status("Playback close failed - not deleted");continue;}}
                ending=false;
                if(!sdcard_mount()) {status("No SD card / mount failed");continue;}
                char path[sizeof DIR_AUDIO+SD_AUDIO_NAME+4];struct stat st;
                snprintf(path,sizeof path,DIR_AUDIO "/%s",req.name);
                taskENTER_CRITICAL(&mux);bool cancelled=req.epoch!=command_epoch;taskEXIT_CRITICAL(&mux);
                bool ok=false;
                if(!cancelled && wav_recording_name(req.name) && recording_stat(path,&st)==0 && S_ISREG(st.st_mode)) ok=unlink(path)==0;
                sdcard_unmount();
                if(ok) list(SD_AUDIO_REFRESH);
                status(ok ? "Deleted recording" : cancelled ? "Delete cancelled by STOP" : "Delete failed / not a regular recording");
            }
            else if(active) {
                taskENTER_CRITICAL(&mux);
                if(mode!=SD_AUDIO_RECORDING) {state.record_requested=false;state.generation++;}
                taskEXIT_CRITICAL(&mux);
                status("STOP before another operation");
            }
            else if(req.command==SD_AUDIO_REFRESH || req.command==SD_AUDIO_NEXT || req.command==SD_AUDIO_PREV) list(req.command);
            else if(req.command==SD_AUDIO_RECORD || req.command==SD_AUDIO_PLAY) {
                ending=bad=limit=false;
                if(!sdcard_mount()) {setmode(SD_AUDIO_IDLE);status("No SD card / mount failed");continue;}
                mounted=true;
                taskENTER_CRITICAL(&mux);head=tail=0;overflow=false;taskEXIT_CRITICAL(&mux);
                char path[sizeof DIR_AUDIO+SD_AUDIO_NAME+4], name[SD_AUDIO_NAME]={0};
                if(req.command==SD_AUDIO_RECORD) {
                    if(mkdir(DIR_AUDIO,0777)!=0 && errno!=EEXIST) {status("Cannot create /AUDIO");goto failed;}
                    /* O_EXCL never overwrites an existing recording, even after reboot. */
                    int fd=-1;
                    for(unsigned i=1;i<=999999;i++) {
                        if(req.name[0]) {
                            size_t len=strlen(req.name)-4;
                            bool unsynced=!strncmp(req.name,"unsynced_",9);
                            if(unsynced) len=(size_t)(strrchr(req.name,'_')-req.name);
                            int n=i==1 ? snprintf(name,sizeof name,"%s",req.name) :
                                snprintf(name,sizeof name,"%.*s_%u.wav",(int)len,req.name,unsynced ? i : i-1);
                            if(n<0 || (size_t)n>=sizeof name) break;
                        } else snprintf(name,sizeof name,"RX%06u.WAV",i);
                        snprintf(path,sizeof path,DIR_AUDIO "/%s",name);
                        fd=open(path,O_RDWR|O_CREAT|O_EXCL,0666);
                        if(fd>=0 || errno!=EEXIST) break;
                    }
                    if(fd<0) {status("Cannot create WAV (full/card error)");goto failed;}
                    file=fdopen(fd,"w+b");
                    if(!file) {close(fd);status("Cannot open WAV stream");goto failed;}
                    if(!wav_begin(&writer,file)) {status("WAV header write failed");goto failed;}
                    /* Publish only after all blocking preparation has completed. */
                } else {
                    if(!wav_safe_name(req.name)) {status("Invalid WAV filename");goto failed;}
                    snprintf(name,sizeof name,"%s",req.name);
                    snprintf(path,sizeof path,DIR_AUDIO "/%s",name);
                    file=fopen(path,"rb");
                    if(!file || !wav_open(&reader,file)) {status("Need PCM WAV: 48kHz mono 16-bit");goto failed;}
                    /* Publication below is atomic with STOP cancellation. */
                }
                taskENTER_CRITICAL(&mux);
                bool cancelled=req.epoch!=command_epoch || stop_requested;
                if(!cancelled) {
                    active=req.command==SD_AUDIO_RECORD ? SD_AUDIO_RECORDING : SD_AUDIO_PLAYING;
                    mode=active;state.state=active;
                    snprintf(state.name,sizeof state.name,"%s",name);state.generation++;
                }
                taskEXIT_CRITICAL(&mux);
                if(cancelled) {status("Start cancelled by STOP");goto failed;}
                status(active==SD_AUDIO_RECORDING ? "REC: demod audio 48k mono WAV" : "PLAY: live RX replaced, not recorded");
                continue;
failed:
                setmode(SD_AUDIO_IDLE);
                if(file) fclose(file);
                file=NULL;sdcard_unmount();mounted=false;
            }
        }
        if(active==SD_AUDIO_RECORDING) {
            if(ending) {taskENTER_CRITICAL(&mux);mode=SD_AUDIO_IDLE;taskEXIT_CRITICAL(&mux);} /* stop producer before draining */
            taskENTER_CRITICAL(&mux);bool lost=overflow;taskEXIT_CRITICAL(&mux);
            if(lost) {bad=true;ending=true;setmode(SD_AUDIO_IDLE);}
            size_t n=transfer(buf,BLOCK,false);
            size_t room=(WAV_AUDIO_MAX_BYTES-writer.bytes)/2;
            if(n>room) n=room;
            if(n && !wav_append(&writer,buf,n)) {bad=true;ending=true;setmode(SD_AUDIO_IDLE);}
            if(writer.bytes==WAV_AUDIO_MAX_BYTES) {
                limit=true;ending=true;setmode(SD_AUDIO_IDLE);
            }
            if(ending && (!n || bad || limit)) {
                if(!wav_finish(&writer)) bad=true;
                if(fsync(fileno(file))) bad=true;
                if(fclose(file)) bad=true;
                file=NULL;
                status(bad ? "REC stopped: card/full/buffer error" : limit ? "WAV saved (size limit reached)" : "WAV saved (demod listening audio)");
                active=SD_AUDIO_IDLE;setmode(active);
            }
        } else if(active==SD_AUDIO_PLAYING) {
            taskENTER_CRITICAL(&mux);size_t queued=head-tail;taskEXIT_CRITICAL(&mux);
            if(!ending && reader.remaining && CAP-queued>=BLOCK) {
                size_t n=wav_read(&reader,buf,BLOCK); transfer(buf,n,true);
                if(reader.failed) {bad=true;ending=true;}
            } else if(ending || (!reader.remaining && !queued)) {
                setmode(SD_AUDIO_IDLE); /* restore same live demod/volume/mute, no setting changed */
                if(fclose(file)) bad=true;
                file=NULL;active=SD_AUDIO_IDLE;
                status(bad ? "Playback card/read error" : sd_audio_output_exclusive() ? "Playback stopped - live RX gated on PLAY page" : "Playback stopped - live RX restored");
            }
        }
        if(!active && mounted) {sdcard_unmount();mounted=false;}
        if(active) {
            taskENTER_CRITICAL(&mux);size_t pending=head-tail;taskEXIT_CRITICAL(&mux);
            /* Batch card I/O instead of limiting throughput to one block per RTOS tick. */
            if(active==SD_AUDIO_RECORDING && pending>=BLOCK) continue;
            if(active==SD_AUDIO_PLAYING && !ending && reader.remaining && CAP-pending>=BLOCK) continue;
            vTaskDelay(1);
        }
    }
}
bool sd_audio_init(void) {
    if(requests) return true;
    ring=heap_caps_malloc(CAP*sizeof *ring,MALLOC_CAP_SPIRAM);
    requests=xQueueCreate(8,sizeof(request_t));
    if(!ring || !requests) {
        if(requests) vQueueDelete(requests);
        requests=NULL;free(ring);ring=NULL;
        status("SD audio unavailable: no memory");return false;
    }
    if(xTaskCreatePinnedToCore(worker,"sd-audio",6144,NULL,2,NULL,0)!=pdPASS) {
        vQueueDelete(requests);requests=NULL;free(ring);ring=NULL;
        status("SD audio worker unavailable");return false;
    }
    status("SD /AUDIO - tap REFRESH"); return true;
}
bool sd_audio_request(int command,const char *name) {
    if(!requests) return false;
    if((command==SD_AUDIO_PLAY || command==SD_AUDIO_DELETE) && !name) return false;
    if(name && (!wav_safe_name(name) || ((command==SD_AUDIO_DELETE || command==SD_AUDIO_RECORD) && !wav_recording_name(name)))) return false;
    /* STOP cannot be lost behind a full queue or a slow mount. Epoch invalidates
     * older queued starts so leaving the screen cannot start a hidden recording. */
    taskENTER_CRITICAL(&mux);
    if(command==SD_AUDIO_STOP) {command_epoch++;stop_requested=true;mode=SD_AUDIO_IDLE;state.state=SD_AUDIO_IDLE;state.record_requested=false;state.generation++;taskEXIT_CRITICAL(&mux);return true;}
    if(command==SD_AUDIO_RECORD && playback_page) {taskEXIT_CRITICAL(&mux);return false;}
    if(command==SD_AUDIO_DELETE) {
        if(mode==SD_AUDIO_RECORDING || state.record_requested) {taskEXIT_CRITICAL(&mux);return false;}
        command_epoch++;stop_requested=true;mode=SD_AUDIO_IDLE;state.state=SD_AUDIO_IDLE;state.generation++;
    }
    unsigned epoch=command_epoch;
    if(command==SD_AUDIO_RECORD && mode==SD_AUDIO_IDLE) {state.record_requested=true;state.generation++;}
    taskEXIT_CRITICAL(&mux);
    request_t req={.command=command,.epoch=epoch};
    if(name) {
        if(!wav_safe_name(name)) return false;
        snprintf(req.name,sizeof req.name,"%s",name);
    }
    bool sent=xQueueSend(requests,&req,0)==pdTRUE;
    if(!sent && command==SD_AUDIO_RECORD) {
        taskENTER_CRITICAL(&mux);
        if(epoch==command_epoch && mode!=SD_AUDIO_RECORDING) {state.record_requested=false;state.generation++;}
        taskEXIT_CRITICAL(&mux);
    }
    return sent;
}
