"""Compile target allocation paths against a capability-checking heap; no allocator model of DSP."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
a=(r/'main/audio.c').read_text();start=a.index('    uint8_t *iq =',a.index('static void audio_task'));end=a.index('    uint64_t pos =',start)
code='''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "demod.h"
#include "dsp.h"
static int calls,fail_at,live;
static bool s_ready;
static size_t bytes;
void *heap_caps_calloc(size_t n,size_t z,unsigned caps){
assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));calls++;
if(calls==fail_at)return NULL;
void*p=calloc(n,z);assert(p);live++;bytes+=n*z;return p;
}
void heap_caps_free(void*p){if(p){live--;free(p);}}
static int acalls,audio_fail;
void *heap_caps_malloc(size_t n,unsigned caps){
assert(caps==(acalls++==0?MALLOC_CAP_SPIRAM:(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
if(acalls==audio_fail)return NULL;
void*p=malloc(n);assert(p);live++;return p;
}
#define ESP_LOGE(...) ((void)0)
static void vTaskDelete(void*p){(void)p;assert(audio_fail && live==0);exit(0);}
#define READ_MAX (2*4800)
static void audio_allocations(void){
'''+a[start:end]+'''
heap_caps_free(iq);heap_caps_free(pcm);
}
int main(int argc,char**argv){
if(argc>1 && argv[1][0]=='a'){audio_fail=atoi(argv[1]+1);audio_allocations();abort();}
if(argc>1){fail_at=atoi(argv[1]);assert(!demod_init());assert(live==0);return 0;}
assert(demod_init());
printf("DSP internal heap: %d allocations, %zu bytes; audio IQ stays PSRAM, PCM internal\\n",calls,bytes);
assert(calls==11);assert(bytes==27612);
audio_allocations();
// Execute actual DSP after capability-checked allocations, all listening modes.
unsigned char iq[9600];short pcm[100];memset(iq,128,sizeof iq);
for(int m=0;m<=4;m++){demod_set((demod_mode_t)m,m==0?200000:m==1?12500:m==2?8000:3000);assert(demod_process(iq,4800,pcm)==96);}
return 0;
}
'''
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as d:
 p=Path(d);(p/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
void *heap_caps_calloc(size_t,size_t,unsigned);
void *heap_caps_malloc(size_t,unsigned);
void heap_caps_free(void*);
''')
 (p/'ready.c').write_text('#include <assert.h>\n#include <stdbool.h>\n#include <stddef.h>\nstatic bool s_ready; static void*s_dev=(void*)1;\n'+a[a.index('bool audio_ok(void)'):a.index('\n',a.index('bool audio_ok(void)'))]+'\nint main(void){assert(!audio_ok());s_ready=true;assert(audio_ok());return 0;}\n')
 subprocess.run(['cc','-Wno-unused-variable',str(p/'ready.c'),'-o',str(p/'ready')],check=True)
 subprocess.run([str(p/'ready')],check=True)
 (p/'t.c').write_text(code)
 subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-DESP_PLATFORM','-I'+str(p),'-I'+str(r/'main'),str(p/'t.c')]+[str(r/'main'/f) for f in ('demod.c','dsp.c','rds.c')]+['-lm','-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
 for allocation in range(1,12):subprocess.run([str(p/'t'),str(allocation)],check=True)
 for allocation in ('a1','a2'):subprocess.run([str(p/'t'),allocation],check=True)
 print('Every DSP allocation failure returns false without leaked partial allocation PASS')
