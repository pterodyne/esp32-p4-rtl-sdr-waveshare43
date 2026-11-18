"""Real audio loop + real DSP/FFT/view in a deterministic virtual-IQ/codec scheduler.
Host load is not P4 timing. A forced stream-loss control proves flash detection.
"""
from pathlib import Path
import tempfile,subprocess,os
r=Path(__file__).resolve().parents[1]
a=(r/'main/audio.c').read_text()
loop=a[a.index('#define READ_MAX'):a.index('bool audio_init(void)')]
u=(r/'main/ui.c').read_text()
a=u.index('        if (!sdr_streaming()) {',u.index('void ui_run('))
b=u.index('        int64_t t0 =',a)
recovery=u[a:b]
code='''
#define main preview_entry_unused
#include "tools/preview/preview.c"
#undef main
#include <setjmp.h>
#include <time.h>
#include "audio.h"
#include "sdr.h"
#include "audio_pause_gate.h"
#include "audio_output_policy.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef void *esp_codec_dev_handle_t;
static jmp_buf done;
static int64_t virtual_us;
static uint64_t produced;
static int blocks,frames,restarts_seen,reopens,flashes;
static bool running=true,s_panel;
void __real_view_message(const char*,const char*);
void __wrap_view_message(const char*a,const char*b){flashes++;__real_view_message(a,b);}
static unsigned char frame_iq[8192];
static float frame_db[4096];
static void esp_codec_dev_set_out_mute(void*d,bool v){(void)d;(void)v;}
static void esp_codec_dev_set_out_vol(void*d,int v){(void)d;(void)v;}
static void esp_codec_dev_write(void*,const void*,size_t);
static int64_t esp_timer_get_time(void){return virtual_us;}
static void vTaskDelete(void*p){(void)p;abort();}
void vTaskDelay(unsigned t){virtual_us+=t*1000;produced+=t*4800;}
uint64_t sdr_total_bytes(void){return produced;}
size_t sdr_ring_read(uint8_t*p,uint64_t*pos,size_t n,uint64_t*lost){
assert(produced>=*pos);size_t avail=produced-*pos;if(n>avail)n=avail;
for(size_t j=0;j<n;j++)p[j]=128+(((*pos+j)%32)<16?8:-8);
*pos+=n;(void)lost;return n;
}
bool sd_audio_output_exclusive(void){return false;}
void sd_audio_exchange(int16_t*p,size_t n){(void)p;(void)n;}
bool sdr_streaming(void){return running;}
uint32_t sdr_freq(void){return 142000100;}
bool rtlsdr_usb_take_gone(void){return false;}
bool sdr_close(void){return true;}
void sdr_start(void){restarts_seen++;running=true;}
bool sdr_open(uint32_t t,char*d,size_t n){(void)t;(void)d;(void)n;reopens++;return true;}
static void audio_settings_try_save(bool f){(void)f;}
static void hackrf_settings_try_save(bool f){(void)f;}
static void poll_touch(const touch_cal_t*c){(void)c;}
static void draw_header(int p){view_header(142000100,"?",p);}
static void ui_recovery(void){const touch_cal_t*cal=NULL;do{
'''+recovery+'''
}while(0);}
'''+loop+'''
static void esp_codec_dev_write(void*d,const void*p,size_t n){
(void)d;(void)p;assert(n>0 && n<=192);
virtual_us+=2000;produced+=9600;blocks++;
if(blocks%10==0){
if(getenv("SSB_INJECT_LOSS") && blocks>=500 && blocks<=530)running=false;
ui_recovery();spectrum_compute(frame_iq,1,frame_db);
view_render(frame_db,4096,142000100);view_blit();frames++;
}
if(blocks>=3000)longjmp(done,1);
}
int main(void){
view_init(2400000);spectrum_init();spectrum_set_size(4096);
for(int j=0;j<8192;j++)frame_iq[j]=128+(j%32<16?8:-8);
assert(demod_init());
const demod_mode_t modes[]={DEMOD_NFM,DEMOD_USB,DEMOD_LSB,DEMOD_AM};
for(int i=0;i<4;i++){
blocks=frames=0;produced=96000;virtual_us=0;
s_mode=modes[i];s_bw=i==0?12500:i==3?8000:3000;s_req_mode=true;
audio_pause_gate_request(&s_pause_gate,false);
view_set_span(100000);view_set_sideband(i==1?1:i==2?-1:0);
int before=restarts_seen, reopened=reopens, messages=flashes;
clock_t start=clock();if(!setjmp(done))audio_task(NULL);
assert(frames==300 && restarts_seen==before && reopens==reopened && flashes==messages);
printf("mode %d actual audio/DSP/FFT/view: %d PCM blocks, %d frames, 6 virtual seconds, host CPU %.3f seconds; no stream recovery\\n",modes[i],blocks,frames,(double)(clock()-start)/CLOCKS_PER_SEC);
}
// Red-capable recovery control: force four distinct losses through production
// UI recovery. The fourth must really reopen/paint the loss message, not hide it.
int old=reopens;for(int i=0;i<4;i++){running=false;ui_recovery();}
assert(reopens==old+1 && restarts_seen>=4 && flashes==1);
puts("Forced repeated-loss control reaches actual reopen/message path; host steady SSB does not reproduce hardware flash");
return 0;
}
'''
# touch_cal_t and USB declarations already exist in the preview includes only on
# some configurations: include their production headers explicitly.
code=code.replace('#include <setjmp.h>','#include <setjmp.h>\n#include "touch_cal.h"')
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as d:
 p=Path(d);(p/'t.c').write_text(code)
 display=r/'components/rpi_display'
 cmd=['cc','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-variable','-Wno-missing-field-initializers','-DCONFIG_SDR_WAVESHARE_43','-DCONFIG_SDR_BACKEND_HACKRF','-I'+str(r),'-I'+str(r/'tests/sd_audio_host'),'-I'+str(r/'tools/preview/stub'),'-I'+str(display/'include'),'-I'+str(display),'-I'+str(r/'main'),str(p/'t.c')]
 cmd += [str(r/'main'/f) for f in ('view.c','panel.c','hf_diag.c','adsb.c','adsb_view.c','amap.c','scan_view.c','sensor_view.c','spectrum.c','demod.c','dsp.c','rds.c')]
 cmd += [str(display/'rpi_canvas.c'),str(display/'rpi_text.c'),'-Wl,--wrap=view_message','-lz','-lm','-o',str(p/'t')]
 subprocess.run(cmd,check=True)
 negative=subprocess.run([str(p/'t')],env=dict(os.environ,SSB_INJECT_LOSS='1'),capture_output=True,text=True)
 assert negative.returncode!=0 and 'Assertion' in negative.stderr,negative
 print('Injected periodic stream loss makes steady-mode no-flash assertion RED (expected)')
 subprocess.run([str(p/'t')],check=True)
