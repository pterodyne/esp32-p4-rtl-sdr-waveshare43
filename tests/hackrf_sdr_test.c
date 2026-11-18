#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include "../main/sdr_hackrf.c"
static int fail_gain, fail_safe, wrong_id, closes, fail_bias, tunes, fail_rate;
static uint32_t commanded_rate;
static unsigned stop_calls;
static unsigned last_stage_db; static bool last_stage_lna; static int stage_calls;
int64_t esp_timer_get_time(void) {static atomic_llong t;return atomic_fetch_add(&t,100000)+100000;}
int rtlsdr_usb_start(void){return 0;}
bool rtlsdr_usb_wait_new_device(uint32_t ms){(void)ms;return false;}
ssize_t libusb_get_device_list(libusb_context *c,libusb_device ***list) {
 (void)c; static libusb_device *items[2]={(void*)1,NULL};*list=items;return 1;
}
void libusb_free_device_list(libusb_device **l,int u){(void)l;(void)u;}
int libusb_get_device_descriptor(libusb_device *d,struct libusb_device_descriptor *p) {
 (void)d;*p=(struct libusb_device_descriptor){.idVendor=wrong_id?0x0bda:0x1d50,.idProduct=0x6089};return 0;
}
int libusb_open(libusb_device *d,libusb_device_handle **h){(void)d;*h=(void*)2;return 0;}
void libusb_close(libusb_device_handle *h){(void)h;closes++;}
int libusb_validate_bulk_in(libusb_device_handle*h,unsigned char ep){(void)h;assert(ep==0x81);return 0;}
int libusb_claim_interface(libusb_device_handle*h,int i){(void)h;assert(i==0);return 0;}
int libusb_release_interface(libusb_device_handle*h,int i){(void)h;assert(i==0);return 0;}
int hackrf_rx_clear_bias_options(libusb_device_handle*h,uint16_t api){(void)h;(void)api;return 0;}
int hackrf_rx_safe(libusb_device_handle*h){(void)h;return fail_safe?-1:0;}
int hackrf_rx_rate(libusb_device_handle*h,uint32_t r){(void)h;commanded_rate=r;if(fail_rate==2) {fail_rate=0;return -1;}return fail_rate?-1:0;}
int hackrf_rx_tune(libusb_device_handle*h,uint32_t f){(void)h;(void)f;tunes++;return 0;}
int hackrf_rx_gain(libusb_device_handle*h,unsigned l,unsigned v){(void)h;(void)l;(void)v;return fail_gain?-1:0;}
int hackrf_rx_bias(libusb_device_handle*h,int b){(void)h;(void)b;return fail_bias?-1:0;}
void hackrf_rx_unsigned(unsigned char*p,unsigned n){for(unsigned i=0;i<n;i++)p[i]^=128;}
int hackrf_stream_run(libusb_device_handle*h,hackrf_sink_t s,void*c){(void)h;(void)s;(void)c;return 0;}
void hackrf_stream_stop(void){stop_calls++;}
void hackrf_stream_prepare(void){}
int hackrf_rx_stage(libusb_device_handle*h,bool l,unsigned db){(void)h;last_stage_lna=l;last_stage_db=db;stage_calls++;return fail_gain?-1:0;}
int sdr_stage_gain(bool);
bool sdr_stage_known(bool);
bool sdr_set_stage_gain(bool,int);
static atomic_bool test_done;
static void *consumer(void *arg) {
 (void)arg;
 while(!atomic_load(&test_done) || atomic_load(&raw_head)!=atomic_load(&raw_tail))
  if(!drain_once()) sched_yield();
 return NULL;
}
static void queue_stress(void) {
 enum { N=2000000 };
 unsigned char *src=malloc(N),*expected=malloc(N),*actual=malloc(N);
 assert(src && expected && actual);
 for(unsigned j=0;j<N;j++) src[j]=(j*73u+(j>>5))%256;
 hackrf_resample_t oracle;assert(hackrf_resample_init(&oracle,rate));
 size_t want=hackrf_resample_process(&oracle,src,N,expected,N);
 assert(pipeline_reset());
 /* Both the PSRAM index and the 32-bit published counters wrap. */
 atomic_store(&raw_head,UINT32_MAX-1000);atomic_store(&raw_tail,UINT32_MAX-1000);
 uint64_t pos=sdr_total_bytes(),drop=0;
 atomic_store(&test_done,false);pthread_t thread;assert(!pthread_create(&thread,NULL,consumer,NULL));
 for(unsigned j=0;j<N;) {
  unsigned k=(j*13u)%23000+1;if(k>N-j) k=N-j;
  while((unsigned)(atomic_load(&raw_head)-atomic_load(&raw_tail))>RAW_LEN-k) sched_yield();
  rx_sink(src+j,k,NULL);j+=k;
 }
 atomic_store(&test_done,true);assert(!pthread_join(thread,NULL));
 assert(!atomic_load(&raw_overflow));
 assert(sdr_ring_read(actual,&pos,N,&drop)==want && !drop && !memcmp(actual,expected,want));
 /* Measure the actual host callback+worker pipeline separately from USB mocks. */
 assert(pipeline_reset());clock_t begin=clock();
 for(unsigned j=0;j<1000;j++) {rx_sink(src,24576,NULL);while(drain_once()) {}}
 double seconds=(double)(clock()-begin)/CLOCKS_PER_SEC;
 printf("HOST ONLY pipeline %u output: %.3f native MS/s, %.6f CPU s for 24576000 raw B\n",rate,24576000.0/2e6/seconds,seconds);
 free(src);free(expected);free(actual);
}
int main(void) {
 char desc[96];
 wrong_id=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev);
 wrong_id=0;fail_safe=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev && closes==1);
 fail_safe=0;fail_gain=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev);
 fail_gain=0;assert(sdr_open(0,desc,sizeof desc));
 assert(sdr_set_stage_gain(true,24) && sdr_set_stage_gain(false,30));
 assert(sdr_stage_gain(true)==24 && sdr_stage_gain(false)==30);
 assert(!strcmp(sdr_gain_label(sdr_gain_index()),"L24 V30"));
 fail_gain=1;assert(!sdr_set_stage_gain(true,32));
 assert(sdr_stage_gain(true)==24 && !sdr_stage_known(true));
 assert(sdr_stage_gain(false)==30 && sdr_stage_known(false));
 assert(!strcmp(sdr_gain_label(sdr_gain_index()),"L? V30"));
 fail_gain=0;assert(sdr_set_stage_gain(true,24) && sdr_stage_known(true));
 assert(!sdr_set_stage_gain(true,7) && !sdr_set_stage_gain(false,64));
 for(int l=0;l<=40;l+=8) {
   int before=stage_calls; assert(sdr_set_stage_gain(true,l));
   assert(stage_calls==before+1 && last_stage_lna && last_stage_db==(unsigned)l && sdr_stage_gain(false)==30);
 }
 for(int v=0;v<=62;v+=2) {
   assert(sdr_set_stage_gain(false,v));
   assert(!last_stage_lna && last_stage_db==(unsigned)v && sdr_stage_gain(true)==40);
 }
 assert(sdr_close() && sdr_open(0,desc,sizeof desc));
 assert(sdr_stage_gain(true)==40 && sdr_stage_gain(false)==62);
 assert(commanded_rate==8000000 && !sdr_bias_tee() && sdr_bias_command_ok());
 assert(!sdr_direct_supported() && !sdr_set_direct_mode(1));
 assert(sdr_gain_tenths(0)==0 && strcmp(sdr_gain_label(0),"AGC"));
 sdr_set_gain_index(0);assert(sdr_gain_index()==0);
 fail_gain=1;sdr_set_gain_index(4);assert(sdr_gain_index()==0);
 assert(!strcmp(sdr_gain_label(0),"L? V?"));fail_gain=0;
 assert(sdr_set_stage_gain(true,8));
 assert(!strcmp(sdr_gain_label(0),"L8 V?"));
 assert(sdr_set_stage_gain(false,2));
 assert(!strcmp(sdr_gain_label(0),"L8 V2"));
 fail_rate=2;assert(!sdr_request_sample_rate(2000000) && sdr_sample_rate()==2400000);
 assert(sdr_request_sample_rate(2000000));assert(commanded_rate==8000000 && sdr_sample_rate()==2000000);
 sdr_set_sample_rate(12345);assert(sdr_sample_rate()==2000000);
 fail_rate=1;assert(!sdr_request_sample_rate(2400000));assert(sdr_sample_rate()==0);
 assert(commanded_rate==8000000); /* restore previous hardware rate after partial failure */
 fail_rate=0;assert(sdr_request_sample_rate(2000000));
 atomic_store(&owner,true);sdr_set_sample_rate(2400000);assert(sdr_sample_rate()==2000000);
 int before_tune=tunes;sdr_set_freq(12345678);assert(tunes==before_tune);
 assert(!sdr_close() && dev);atomic_store(&owner,false);
 resampler.pending=true;sdr_start(); /* host seam injects task-creation failure */
 assert(!atomic_load(&owner) && !sdr_streaming() && !resampler.pending);
 /* Callback is copy-only; consumer owns filtering. Odd chunk edges survive. */
 unsigned char iq[2000]={0},out[600];uint64_t pos=sdr_total_bytes(),drop=0;
 assert(pipeline_reset());
 rx_sink(iq,1,NULL);rx_sink(iq+1,1999,NULL);
 assert(sdr_total_bytes()==pos);
 assert(raw_bytes==2000 && queue_peak==2000 && callback_max_us>0);
 assert(drain_once());
 assert(sdr_ring_read(out,&pos,sizeof out,&drop)==500);
 for(unsigned i=0;i<500;i++) assert(out[i]==128);
 assert(!drop && !drain_once());
 assert(pipeline_reset());
 atomic_store(&raw_head,RAW_LEN);atomic_store(&raw_tail,0);
 unsigned before_stop=stop_calls;
 rx_sink(iq,2,NULL);assert(atomic_load(&raw_overflow) && stop_calls==before_stop+1);
 assert(pipeline_reset() && !atomic_load(&raw_overflow));
 queue_stress();
 assert(sdr_request_sample_rate(2400000));queue_stress();
 assert(sdr_request_sample_rate(2000000));
 fail_bias=1;assert(!sdr_set_bias_tee(true) && !sdr_bias_command_ok());
 fail_bias=0;assert(sdr_set_bias_tee(false) && sdr_bias_command_ok());
 assert(sdr_close());assert(sdr_open(0,desc,sizeof desc));assert(!sdr_bias_tee());
 assert(sdr_close());
 puts("HackRF SDR enumeration/safety/gain/rate/ring/reopen tests PASS");
}
