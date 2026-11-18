"""Execute the actual UI restore/recovery control seam with injected backend faults."""
from pathlib import Path
import os, subprocess
root = Path(__file__).resolve().parents[1]
u = (root/'main/ui.c').read_text()
assert 'if (s_normal_restore_pending) goto normal_recovery;' in u.split('if (s_adsb_req) {',1)[0]
assert 'if (s_normal_restore_pending) goto normal_recovery;' in u.split('adsb_mode(cal);',1)[1].split('if (s_scan_req >= 0)',1)[0]
restore = u.split('    bool rate_restored =',1)[1].split('    view_set_theme(view_theme());',1)[0]
restore = '    bool rate_restored =' + restore
recovery = u.split('        if (!sdr_streaming()) {\n            static int restarts;',1)[1].split('        int64_t t0 =',1)[0]
recovery = 'if (!sdr_streaming()) {\n static int restarts;' + recovery
helper = ''
if 'static bool normal_rate_restore(void)' in u:
    helper = u.split('static bool normal_rate_restore(void)',1)[1].split('\n}\n',1)[0]
    helper = 'static bool normal_rate_restore(void)' + helper + '\n}\n'
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define SDR_SAMPLE_RATE 2400000
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static bool s_normal_restore_pending, streaming, paused=true, gone, s_panel;
static int failures, normal_frames, resumes, starts, opens;
static uint32_t rate=2000000;
static bool sdr_streaming(void){return streaming;}
static bool sdr_request_sample_rate(uint32_t hz){if(failures){failures--;return false;}rate=hz;return true;}
static uint32_t sdr_sample_rate(void){return rate;}
static void sdr_stop(void){streaming=false;}
static void sdr_start(void){starts++; streaming=rate!=0; assert(rate==SDR_SAMPLE_RATE);}
static void audio_pause(bool p){paused=p;if(!p){assert(rate==SDR_SAMPLE_RATE);resumes++;}}
static bool rtlsdr_usb_take_gone(void){bool g=gone;gone=false;return g;}
static bool sdr_close(void){streaming=false;return true;}
static bool sdr_open(int t,void*d,int n){(void)t;(void)d;(void)n;opens++;return true;}
static void vTaskDelay(int n){(void)n;}
static void view_message(const char*a,const char*b){(void)a;(void)b;}
static void audio_settings_try_save(bool b){(void)b;}
static void poll_touch(void*c){(void)c;}
static void view_reset(void){}
static void draw_header(int n){(void)n;}
static void view_axis(int n){(void)n;}
static int sdr_freq(void){return 100000000;}
static void sdr_set_freq(int n){(void)n;}
static void sdr_set_gain_index(int n){(void)n;}
'''
# Include any new pending-state gate immediately before the real recovery branch.
prefix = u.split('        if (!sdr_streaming()) {\n            static int restarts;',1)[0]
gate = ''
if '        if (s_normal_restore_pending)' in prefix:
    gate = '        if (s_normal_restore_pending)' + prefix.rsplit('        if (s_normal_restore_pending)',1)[1]
code = preamble + helper + '\nstatic void restore(void){int freq=0,gain=0;\n' + restore + '\n}\n'
code += 'static void tick(void){void *cal=NULL; do {\n' + gate + recovery + '\nassert(!s_normal_restore_pending);assert(rate==SDR_SAMPLE_RATE);assert(!paused);normal_frames++;\n}while(0);}\n'
code += r'''
int main(void){
 failures=2;restore();assert(s_normal_restore_pending);assert(paused);assert(!streaming);
 tick();assert(normal_frames==0);assert(paused);assert(starts==0);
 tick();assert(!s_normal_restore_pending);assert(!paused);assert(streaming);tick();assert(normal_frames>0);
 streaming=false;paused=true;rate=0;failures=2;gone=true;restore();assert(s_normal_restore_pending);
 tick();assert(opens>0);assert(paused);assert(!streaming);
 tick();assert(!s_normal_restore_pending);assert(!paused);assert(streaming);tick();
 assert(resumes==2);return 0;
}
'''
p=Path(os.environ['TMPDIR'])/'normal_rate_recovery_test.c';p.write_text(code)
exe=p.with_suffix('');subprocess.run(['cc','-Wall','-Wextra','-Wno-unused-function',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('Actual UI normal-rate restore/recovery seam PASS (rollback and unknown clock/reopen)')
