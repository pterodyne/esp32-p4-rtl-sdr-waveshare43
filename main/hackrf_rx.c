/* SPDX-License-Identifier: GPL-2.0-or-later
 * RX-only wire protocol implementation, based on the published libhackrf API.
 * Protocol reference: Great Scott Gadgets hackrf v2024.02.1 host/libhackrf/src/hackrf.c
 * Original protocol implementation copyright 2012-2024 Great Scott Gadgets contributors.
 * No TX requests or TX bulk endpoint are implemented here.
 */
#include "hackrf_rx.h"
static void le32(unsigned char *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));
}
static int out(libusb_device_handle *h, int req, unsigned val, unsigned idx,
               unsigned char *data, unsigned n) {
    return libusb_control_transfer(h,0x40,req,val,idx,data,n,1000)==(int)n ? 0 : -1;
}
void hackrf_rx_unsigned(unsigned char *data, unsigned n) {
    for(unsigned i=0;i<n;i++) data[i]^=0x80;
}
int hackrf_rx_mode(libusb_device_handle *h, int receive) {
    if(receive!=0 && receive!=1) return -1;
    return out(h,1,receive,0,0,0);
}
int hackrf_rx_bias(libusb_device_handle *h, int on) {
    if(on!=0 && on!=1) return -1;
    return out(h,23,on,0,0,0);
}
int hackrf_rx_clear_bias_options(libusb_device_handle *h, uint16_t api) {
    /* API 1.08 introduced mode-entry bias overrides. Disable OFF/RX overrides;
     * do not modify TX options or ever enter TX. Older API has no such request. */
    return api>=0x0108 ? out(h,48,0x24,0,0,0) : 0;
}
int hackrf_rx_safe(libusb_device_handle *h) {
    /* Attempt all three even if one fails. Never inherit amp or antenna power. */
    int a=hackrf_rx_mode(h,0), b=out(h,17,0,0,0,0), c=hackrf_rx_bias(h,0);
    return a || b || c ? -1 : 0;
}
int hackrf_rx_rate(libusb_device_handle *h, uint32_t hz) {
    /* Explicit application rates, supported by HackRF's fractional clock API.
     * Below 8 MS/s is experimental: minimum analog filter BW is 1.75 MHz. */
    if(hz!=2400000 && hz!=2000000) return -1;
    unsigned char p[8]; le32(p,hz); le32(p+4,1);
    if(out(h,6,0,0,p,8)) return -1;
    unsigned bw=1750000;
    return out(h,7,bw&65535,bw>>16,0,0);
}
int hackrf_rx_tune(libusb_device_handle *h, uint32_t hz) {
    unsigned char p[8]; le32(p,hz/1000000); le32(p+4,hz%1000000);
    return out(h,16,0,0,p,8);
}
int hackrf_rx_stage(libusb_device_handle *h, bool lna, unsigned db) {
    if(db > (lna ? 40u : 62u) || db % (lna ? 8u : 2u)) return -1;
    unsigned char ok=0;
    return libusb_control_transfer(h,0xc0,lna ? 19 : 20,0,db,&ok,1,1000)==1 && ok ? 0 : -1;
}
int hackrf_rx_gain(libusb_device_handle *h, unsigned lna, unsigned vga) {
    if(lna>40 || lna%8 || vga>62 || vga%2) return -1;
    unsigned char ok=0;
    if(libusb_control_transfer(h,0xc0,19,0,lna,&ok,1,1000)!=1 || !ok) return -1;
    ok=0;
    return libusb_control_transfer(h,0xc0,20,0,vga,&ok,1,1000)==1 && ok ? 0 : -1;
}
