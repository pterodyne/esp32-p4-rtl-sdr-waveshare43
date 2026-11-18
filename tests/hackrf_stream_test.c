#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "hackrf_stream.h"
static struct libusb_transfer *queue[10];
static int n, delivered, canceled, fail_submit, submissions, disconnect, odd;
struct libusb_transfer *libusb_alloc_transfer(int x) { (void)x; return calloc(1,sizeof(struct libusb_transfer)); }
void libusb_free_transfer(struct libusb_transfer *t) { free(t); }
int libusb_submit_transfer(struct libusb_transfer *t) {
 assert(t->endpoint==0x81 && t->length==24576);
 if(fail_submit && ++submissions==3) return -1;
 assert(n<10); queue[n++]=t; return 0;
}
int libusb_cancel_transfer(struct libusb_transfer *t) { (void)t; canceled=1; return 0; }
int hackrf_rx_mode(libusb_device_handle *h,int mode) { (void)h; assert(mode==0||mode==1); return 0; }
int libusb_handle_events_timeout_completed(libusb_context *c,struct timeval *tv,int *done) {
 (void)c;(void)tv;(void)done;
 assert(n);
 struct libusb_transfer *t=queue[0];
 for(int i=1;i<n;i++) queue[i-1]=queue[i];
 n--;
 unsigned char bytes[]={0x80,0xff,0,0x7f};
 t->buffer=bytes;t->actual_length=4;
 t->status=canceled?LIBUSB_TRANSFER_CANCELLED:disconnect?LIBUSB_TRANSFER_NO_DEVICE:LIBUSB_TRANSFER_COMPLETED;
 if(odd) t->actual_length=3;
 t->callback(t);
 return 0;
}
static void consume(unsigned char *p,uint32_t len,void *ctx) {
 (void)ctx;assert(len==4 && p[0]==0x80); /* conversion belongs to SDR ring sink */
 if(++delivered==5) hackrf_stream_stop();
}
int main(void) {
 assert(hackrf_stream_run(NULL,consume,NULL)==0);
 assert(delivered==5 && n==0 && canceled);
 fail_submit=1; submissions=0;canceled=0;
 hackrf_stream_prepare();
 assert(hackrf_stream_run(NULL,consume,NULL)<0);
 assert(n==0 && canceled);
 fail_submit=0;disconnect=1;canceled=0;hackrf_stream_prepare();
 assert(hackrf_stream_run(NULL,consume,NULL)<0 && n==0 && canceled);
 disconnect=0;odd=1;canceled=0;hackrf_stream_prepare();
 assert(hackrf_stream_run(NULL,consume,NULL)<0 && n==0 && canceled);
 puts("HackRF stream drain/restart/partial-submit/disconnect/odd-IQ tests PASS");
}
