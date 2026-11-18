/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hackrf_resample.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include <math.h>
#include <string.h>
#ifdef CONFIG_IDF_TARGET_ESP32P4
extern void hackrf_dot96_p4(const int16_t *,const int16_t *,const int16_t *,int32_t *);
#endif
static void dot96(const int16_t *c,const int16_t *i,const int16_t *q,int32_t sums[2]) {
#ifdef CONFIG_IDF_TARGET_ESP32P4
 hackrf_dot96_p4(c,i,q,sums);
#else
 sums[0]=sums[1]=0;
 for(unsigned k=0;k<HACKRF_FIR_TAPS;k++) {sums[0]+=c[k]*i[k];sums[1]+=c[k]*q[k];}
#endif
}
bool hackrf_resample_selftest(void) {
 int16_t c[96] __attribute__((aligned(16)));
 int16_t i[104] __attribute__((aligned(16))),q[104] __attribute__((aligned(16)));
 for(unsigned k=0;k<96;k++) c[k]=(int16_t)((k*1171u)%30001)-15000;
 for(unsigned k=0;k<104;k++) {i[k]=(int)(k*79u%256)-128;q[k]=(int)(k*53u%256)-128;}
 for(unsigned off=0;off<8;off++) {
  int32_t got[2],a=0,b=0;dot96(c,i+off,q+off,got);
  for(unsigned k=0;k<96;k++) {a+=c[k]*i[k+off];b+=c[k]*q[k+off];}
  if(a!=got[0] || b!=got[1]) return false;
 }
 return true;
}
bool hackrf_resample_init(hackrf_resample_t *s,uint32_t rate) {
 if(rate!=2400000 && rate!=2000000) return false;
 memset(s,0,sizeof *s); s->up=rate==2400000?3:1; s->down=rate==2400000?10:4;
 double fc=rate==2400000?0.13125:0.10625;
 for(unsigned p=0;p<s->up;p++) {
  double h[HACKRF_FIR_TAPS],sum=0;
  for(unsigned k=0;k<HACKRF_FIR_TAPS;k++) {
   double x=(double)k-(HACKRF_FIR_TAPS-1)/2.0-(double)p/s->up;
   double w=0.42-0.5*cos(2*3.141592653589793*k/(HACKRF_FIR_TAPS-1))+0.08*cos(4*3.141592653589793*k/(HACKRF_FIR_TAPS-1));
   h[k]=(fabs(x)<1e-12?2*fc:sin(2*3.141592653589793*fc*x)/(3.141592653589793*x))*w; sum+=h[k];
  }
  int total=0;
  for(unsigned k=0;k<HACKRF_FIR_TAPS;k++) {s->coeff[p][k]=(int16_t)lround(h[k]/sum*32768);total+=s->coeff[p][k];}
  s->coeff[p][HACKRF_FIR_TAPS/2]+=32768-total;
 }
 return true;
}
static uint8_t quantize(int32_t x) {
 int v=x>=0?(x+16384)/32768:-((-x+16384)/32768);
 if(v>127) v=127;
 if(v< -128) v= -128;
 return (uint8_t)(v+128);
}
size_t hackrf_resample_process(hackrf_resample_t *s,const uint8_t *in,size_t n,uint8_t *out,size_t cap) {
 if(!s->up || ((n+s->pending)/2*s->up+s->phase)/s->down>cap/2) return SIZE_MAX;
 size_t count=0;
 for(size_t j=0;j<n;j++) {
  if(!s->pending) {s->i=(int8_t)in[j];s->pending=true;continue;}
  s->pending=false;
  if(!s->head) s->head=HACKRF_FIR_TAPS;
  --s->head;
  s->history[0][s->head]=s->history[0][s->head+HACKRF_FIR_TAPS]=s->i;
  s->history[1][s->head]=s->history[1][s->head+HACKRF_FIR_TAPS]=(int8_t)in[j];
  s->phase+=s->up;
  if(s->phase<s->down) continue;
  s->phase-=s->down;
  int32_t sums[2]={0,0};
  dot96(s->coeff[s->phase],s->history[0]+s->head,s->history[1]+s->head,sums);
  out[count++]=quantize(sums[0]);out[count++]=quantize(sums[1]);
 }
 return count;
}
