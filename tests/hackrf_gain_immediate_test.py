"""Execute actual UI persistence seams against transactional NVS power-loss stub."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
src=(root/'main/ui.c').read_text()
def function(name):
    start=src.index('static void '+name+'(')
    brace=src.index('{',start); depth=1; end=brace+1
    while depth:
        depth+=(src[end]=='{')-(src[end]=='}'); end+=1
    return src[start:end]
# Compile production UI helpers and the actual stage/Controls branches.
seams='\n'.join(function(n) for n in ('hackrf_settings_try_save','hackrf_settings_changed'))
stage=src.split('    if (act >= VIEW_LNA_DN',1)[1].split('#endif',1)[0]
stage='    if (act >= VIEW_LNA_DN'+stage
controls=src.split('    if (ch & CTL_GAIN) {',1)[1].split('    if (ch & CTL_BIAS)',1)[0].rsplit('}',1)[0]
for name in ('adsb_mode','sensor_mode','scan_mode'):
    assert 'hackrf_settings_try_save(false)' in function(name)
outer=src.split('void ui_run(',1)[1]
assert outer.index('hackrf_settings_try_save(false)') < outer.index('if (!sdr_streaming())')
assert 'hackrf_settings_try_save(false)' in outer.split('while (!sdr_open(5000, NULL, 0)) {',1)[1].split('poll_touch',1)[0]
code='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hackrf_gain_store.h"
#include "audio_save_pending.h"
static bool s_nvs_ready,s_remote_active; static nvs_handle_t s_nvs;
static audio_save_pending_t s_hackrf_pending;
static int s_hackrf_lna,s_hackrf_vga;
static int lna=24,vga=30, failure; static int64_t now=1;
static int32_t stored=0x0100181e, pending;
#define ESP_LOGW(...) ((void)0)
#define NVS_READWRITE 1
static int64_t esp_timer_get_time(void){return now;}
static int sdr_stage_gain(bool l){return l?lna:vga;}
esp_err_t nvs_open(const char *k,int m,nvs_handle_t*h){(void)m;assert(!strcmp(k,"sdr"));if(failure==1)return ESP_FAIL;*h=7;return ESP_OK;}
esp_err_t nvs_set_i32(nvs_handle_t h,const char*k,int32_t v){assert(h==7);assert(!strcmp(k,"hrgain1"));if(failure==2)return ESP_FAIL;pending=v;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==7);if(failure==3)return ESP_FAIL;stored=pending;return ESP_OK;}
esp_err_t nvs_get_i32(nvs_handle_t h,const char*k,int32_t*v){(void)h;assert(!strcmp(k,"hrgain1"));*v=stored;return ESP_OK;}
'''+seams+'''
enum {VIEW_LNA_DN,VIEW_LNA_UP,VIEW_VGA_DN,VIEW_VGA_UP};
static int reject;
static void draw_header(int a){(void)a;}
static bool sdr_set_stage_gain(bool l,int db){if(reject)return false;if(l)lna=db;else vga=db;return true;}
static bool sdr_stage_known(bool l){(void)l;return !reject;}
static struct {int gain_idx;} s_ctl;
static void sdr_set_gain_index(int i){if(!reject){lna=i*8;vga=i*2;}}
static void action(int act){
'''+stage+'''
}
static void controls(void){
'''+controls+'''
}
static void reload(int a,int b){int l=-1,v=-1;pending=0;assert(hackrf_gain_load(7,&l,&v));assert(l==a && v==b);}
int main(void){
 action(VIEW_LNA_UP);reload(32,30);
 reject=1;action(VIEW_VGA_UP);reload(32,30);reject=0;
 action(VIEW_VGA_UP);reload(32,32);
 for(failure=1;failure<=3;failure++){
  s_nvs_ready=false;lna-=8;hackrf_settings_changed();assert(s_hackrf_pending.dirty);
  int f=failure;failure=0;now+=5000001;
  int desired=lna;lna=40;vga=62; /* temporary foreground gains must not replace pending pair */
  hackrf_settings_try_save(false);reload(desired,32);assert(!s_hackrf_pending.dirty);
  lna=desired;vga=32;failure=f;
 }
 failure=2;vga=34;hackrf_settings_changed();failure=0;vga=36;hackrf_settings_changed();reload(lna,36);
 s_ctl.gain_idx=3;controls();reload(24,6);
 reject=1;s_ctl.gain_idx=4;controls();reload(24,6);reject=0;
 s_remote_active=true;lna=40;hackrf_settings_changed();reload(24,6);
 puts("Immediate gain retention + open/set/commit retry + snapshot isolation PASS");
}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as d:
    p=Path(d);(p/'test.c').write_text(code)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-DCONFIG_SDR_BACKEND_HACKRF','-I'+str(root/'tests/stubs'),'-I'+str(root/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
