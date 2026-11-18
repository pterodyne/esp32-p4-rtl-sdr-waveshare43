#include <assert.h>
#include "../main/sdr_hackrf.c"
static int fail_gain, fail_safe, wrong_id, closes, fail_bias, tunes, fail_rate;
static uint32_t commanded_rate;
int64_t esp_timer_get_time(void) {static int64_t t;return t+=100000;}
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
int hackrf_rx_rate(libusb_device_handle*h,uint32_t r){(void)h;commanded_rate=r;return fail_rate?-1:0;}
int hackrf_rx_tune(libusb_device_handle*h,uint32_t f){(void)h;(void)f;tunes++;return 0;}
int hackrf_rx_gain(libusb_device_handle*h,unsigned l,unsigned v){(void)h;(void)l;(void)v;return fail_gain?-1:0;}
int hackrf_rx_bias(libusb_device_handle*h,int b){(void)h;(void)b;return fail_bias?-1:0;}
void hackrf_rx_unsigned(unsigned char*p,unsigned n){for(unsigned i=0;i<n;i++)p[i]^=128;}
int hackrf_stream_run(libusb_device_handle*h,hackrf_sink_t s,void*c){(void)h;(void)s;(void)c;return 0;}
void hackrf_stream_stop(void){}
void hackrf_stream_prepare(void){}
int main(void) {
 char desc[96];
 wrong_id=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev);
 wrong_id=0;fail_safe=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev && closes==1);
 fail_safe=0;fail_gain=1;assert(!sdr_open(0,desc,sizeof desc));assert(!dev);
 fail_gain=0;assert(sdr_open(0,desc,sizeof desc));
 assert(commanded_rate==2400000 && !sdr_bias_tee() && sdr_bias_command_ok());
 assert(!sdr_direct_supported() && !sdr_set_direct_mode(1));
 assert(sdr_gain_tenths(0)==0 && strcmp(sdr_gain_label(0),"AGC"));
 sdr_set_gain_index(0);assert(sdr_gain_index()==0);
 fail_gain=1;sdr_set_gain_index(4);assert(sdr_gain_index()==0);
 assert(!strcmp(sdr_gain_label(0),"GAIN ?"));fail_gain=0;
 sdr_set_sample_rate(2000000);assert(commanded_rate==2000000 && sdr_sample_rate()==2000000);
 sdr_set_sample_rate(12345);assert(sdr_sample_rate()==2000000);
 fail_rate=1;sdr_set_sample_rate(2400000);assert(sdr_sample_rate()==2000000);
 assert(commanded_rate==2000000); /* restore previous hardware rate after partial failure */
 fail_rate=0;sdr_set_sample_rate(2000000);
 atomic_store(&owner,true);sdr_set_sample_rate(2400000);assert(sdr_sample_rate()==2000000);
 assert(!sdr_close() && dev);atomic_store(&owner,false);
 unsigned char iq[]={128,255,0,127},out[4];uint64_t pos=0,drop=0;
 rx_sink(iq,4,NULL);assert(sdr_ring_read(out,&pos,4,&drop)==4);
 assert(out[0]==0 && out[1]==127 && out[2]==128 && out[3]==255 && !drop);
 fail_bias=1;assert(!sdr_set_bias_tee(true) && !sdr_bias_command_ok());
 fail_bias=0;assert(sdr_set_bias_tee(false) && sdr_bias_command_ok());
 assert(sdr_close());assert(sdr_open(0,desc,sizeof desc));assert(!sdr_bias_tee());
 assert(sdr_close());
 puts("HackRF SDR enumeration/safety/gain/rate/ring/reopen tests PASS");
}
