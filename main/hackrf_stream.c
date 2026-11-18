/* SPDX-License-Identifier: GPL-2.0-or-later
 * RX-only async transport on the existing ESP-IDF libusb subset.
 */
#include <stdatomic.h>
#include <stdbool.h>
#include "hackrf_stream.h"
#define NTRANS 10
#define TRANS_LEN (24*1024)
static atomic_bool stop;
void hackrf_stream_prepare(void) { atomic_store(&stop,false); }
void hackrf_stream_stop(void) { atomic_store(&stop,true); }
typedef struct {
    hackrf_sink_t sink;
    void *ctx;
    int active, error;
} stream_t;
static void complete(struct libusb_transfer *t) {
    stream_t *s=t->user_data;
    s->active--;
    if(t->status!=LIBUSB_TRANSFER_COMPLETED) {
        if(t->status!=LIBUSB_TRANSFER_CANCELLED) s->error=-1;
        atomic_store(&stop,true);
        return;
    }
    if(atomic_load(&stop)) return;
    if(t->actual_length<0 || t->actual_length>t->length || t->actual_length%2) {
        s->error=-1; atomic_store(&stop,true); return;
    }
    if(t->actual_length) s->sink(t->buffer,t->actual_length,s->ctx);
    if(!atomic_load(&stop)) {
        if(libusb_submit_transfer(t)==0) s->active++;
        else {s->error=-1;atomic_store(&stop,true);}
    }
}
int hackrf_stream_run(libusb_device_handle *h,hackrf_sink_t sink,void *ctx) {
    struct libusb_transfer *t[NTRANS]={0};
    stream_t s={.sink=sink,.ctx=ctx};
    bool canceled=false;
    /* OFF is sent on all exit paths, including RX-mode failure. */
    if(!atomic_load(&stop) && hackrf_rx_mode(h,1)) s.error=-1;
    for(int i=0;i<NTRANS && !s.error && !atomic_load(&stop);i++) {
        t[i]=libusb_alloc_transfer(0);
        if(!t[i]) {s.error=-1;break;}
        /* Shim supplies its DMA buffer while callback executes; no second 240 KB allocation. */
        libusb_fill_bulk_transfer(t[i],h,0x81,NULL,TRANS_LEN,complete,&s,0);
        if(libusb_submit_transfer(t[i])) {s.error=-1;break;}
        s.active++;
    }
    if(s.error) atomic_store(&stop,true);
    while(s.active) {
        if(atomic_load(&stop) && !canceled) {
            for(int i=0;i<NTRANS;i++) if(t[i]) libusb_cancel_transfer(t[i]);
            canceled=true;
        }
        struct timeval tv={0,20000};
        if(libusb_handle_events_timeout_completed(NULL,&tv,NULL)<0) {
            s.error=-1;atomic_store(&stop,true);
        }
    }
    if(hackrf_rx_mode(h,0)) s.error=-1;
    for(int i=0;i<NTRANS;i++) libusb_free_transfer(t[i]);
    return s.error;
}
