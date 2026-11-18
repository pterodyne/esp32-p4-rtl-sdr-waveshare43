#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "hackrf_resample.h"
static unsigned char input[160000],whole[50000],split[50000];
static double tone(unsigned rate,double hz) {
 hackrf_resample_t s; assert(hackrf_resample_init(&s,rate));
 for(unsigned k=0;k<sizeof input/2;k++) {
  input[2*k]=(uint8_t)(int8_t)lround(80*cos(2*3.141592653589793*hz*k/8000000));
  input[2*k+1]=(uint8_t)(int8_t)lround(80*sin(2*3.141592653589793*hz*k/8000000));
 }
 size_t n=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
 double power=0;for(size_t j=1000;j<n;j++) power+=((int)whole[j]-128)*((int)whole[j]-128);
 if(fabs(hz)<rate*0.4) {
  double re=0,im=0;
  for(size_t j=500;j<n/2;j++) {
   double theta=2*3.141592653589793*hz*j/rate;
   double a=(int)whole[2*j]-128,b=(int)whole[2*j+1]-128;
   re+=a*cos(theta)+b*sin(theta);im+=b*cos(theta)-a*sin(theta);
  }
  double coherent=hypot(re,im)/(n/2-500);
  assert(coherent>78 && coherent<82); /* frequency and I/Q orientation, not RMS alone */
 }
 return sqrt(power/(n-1000));
}
int main(void) {
 assert(hackrf_resample_selftest());
 hackrf_resample_t s; unsigned char in[2000]={0},out[2000];
 assert(hackrf_resample_init(&s,2400000));
 assert(hackrf_resample_process(&s,in,sizeof in,out,sizeof out)==600);
 for(unsigned i=0;i<600;i++) assert(out[i]==128);
 assert(hackrf_resample_init(&s,2000000));
 assert(hackrf_resample_process(&s,in,sizeof in,out,sizeof out)==500);
 assert(!hackrf_resample_init(&s,123));
 for(unsigned r=2000000;r<=2400000;r+=400000) {
  for(unsigned i=0;i<sizeof input;i++) input[i]=(i*79+13)%256;
  assert(hackrf_resample_init(&s,r));
  hackrf_resample_t before=s;
  assert(hackrf_resample_process(&s,input,sizeof input,whole,1)==SIZE_MAX);
  assert(!memcmp(&s,&before,sizeof s));
  size_t n=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
  assert(n==sizeof input*(size_t)r/8000000);
  assert(hackrf_resample_init(&s,r));size_t pos=0,got=0;
  while(pos<sizeof input) {size_t k=(pos*17+1)%997+1;if(k>sizeof input-pos)k=sizeof input-pos;
   size_t v=hackrf_resample_process(&s,input+pos,k,split+got,sizeof split-got);assert(v!=SIZE_MAX);pos+=k;got+=v;
  }
  assert(got==n && !memcmp(whole,split,n));
  assert(hackrf_resample_init(&s,r));
  assert(hackrf_resample_process(&s,in,sizeof in,out,sizeof out)==sizeof in*(size_t)r/8000000);
  for(unsigned i=0;i<sizeof in*(size_t)r/8000000;i++) assert(out[i]==128);
  double pass=tone(r,r==2400000?850000:650000);
  assert(pass>55 && pass<58);
  double worst=0;
  for(double f=r==2400000?1400000:1200000;f<4000000;f+=200000) {
   double reject=tone(r,f);assert(reject<0.8);if(reject>worst) worst=reject;
  }
  printf("HOST signal: %u pass RMS %.4f, worst stop RMS %.4f (<= %.2f dBc, 0.5-LSB bound)\n",r,pass,worst,20*log10(fmax(worst,0.5)/pass));
  assert(tone(r,-650000)>55);
  /* Signed extrema and DC normalization. */
  memset(input,127,sizeof input);assert(hackrf_resample_init(&s,r));
  n=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
  for(size_t i=1000;i<n;i++) assert(whole[i]==255);
  memset(input,128,sizeof input);assert(hackrf_resample_init(&s,r));
  n=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
  for(size_t i=1000;i<n;i++) assert(whole[i]==0);
  /* Independent direct-convolution oracle, including real step overshoot. */
  for(unsigned j=0;j<sizeof input/2;j++) {
   input[2*j]=(j/100)%2?127:128;
   input[2*j+1]=(j/151)%2?128:127;
  }
  assert(hackrf_resample_init(&s,r));
  n=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
  unsigned clipped=0;
  for(size_t o=1;o<=n/2;o++) {
   size_t idx=(o*s.down+s.up-1)/s.up-1;
   unsigned phase=(idx+1)*s.up-o*s.down;
   for(unsigned ch=0;ch<2;ch++) {
    int32_t sum=0;
    for(unsigned k=0;k<HACKRF_FIR_TAPS && k<=idx;k++)
     sum+=s.coeff[phase][k]*(int8_t)input[2*(idx-k)+ch];
    int v=sum<0?-((-sum+16384)/32768):(sum+16384)/32768;
    if(v>127) {v=127;clipped++;}
    if(v< -128) {v= -128;clipped++;}
    assert(whole[2*(o-1)+ch]==v+128);
   }
  }
  assert(clipped>0);
  clock_t start=clock();size_t bytes=0;
  for(unsigned i=0;i<100;i++) bytes+=hackrf_resample_process(&s,input,sizeof input,whole,sizeof whole);
  printf("HOST ONLY: %u output, native %.3f MS/s, %.3f MB/s output; CPU %.3fs, output bytes %zu\n",r,8.0/((double)(clock()-start)/CLOCKS_PER_SEC),bytes/1e6/((double)(clock()-start)/CLOCKS_PER_SEC),(double)(clock()-start)/CLOCKS_PER_SEC,bytes);
 }
 puts("resampler counts/center/chunks/reset/capacity/tones/alias/extrema PASS");
}
