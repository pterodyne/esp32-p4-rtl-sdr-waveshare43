#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Standard interface 0 alternate 0 with the documented 512-byte HS bulk endpoint. */
static inline bool usb_rx_descriptor_valid(const uint8_t *p,size_t n,uint8_t ep) {
    bool iface=false;
    if(n<9 || p[1]!=2) return false;
    for(size_t i=0;i+2<=n;) {
        unsigned len=p[i];
        if(len<2 || len>n-i) return false;
        if(p[i+1]==4) {
            if(len<9) return false;
            iface=p[i+2]==0 && p[i+3]==0;
        }
        if(p[i+1]==5 && iface) {
            if(len<7) return false;
            if(p[i+2]==ep && (p[i+3]&3)==2 && p[i+4]==0 && p[i+5]==2) return true;
        }
        i+=len;
    }
    return false;
}
