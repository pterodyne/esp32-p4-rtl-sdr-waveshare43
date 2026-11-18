/* Host protocol tests; fake only the USB transport. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hackrf_rx.h"
static int calls, fail;
static unsigned reqs[32], vals[32], idxs[32], lens[32], dirs[32];
static unsigned char payload[32][8];
int libusb_control_transfer(libusb_device_handle *h, uint8_t dir, uint8_t req,
 uint16_t val,uint16_t idx,unsigned char *data,uint16_t n,unsigned timeout) {
 (void)h; assert(timeout); assert(calls < 32);
 reqs[calls]=req; vals[calls]=val; idxs[calls]=idx; lens[calls]=n; dirs[calls]=dir;
 if (n && !(dir&128)) memcpy(payload[calls],data,n);
 calls++;
 if (fail) return -1;
 if(dir&128) { assert(n==1); *data=1; }
 return n;
}
int main(void) {
 unsigned char iq[]={0x80,0xff,0,0x7f};
 hackrf_rx_unsigned(iq,4);
 assert(iq[0]==0 && iq[1]==127 && iq[2]==128 && iq[3]==255);
 assert(hackrf_rx_rate(NULL,2400000)==0);
 assert(reqs[0]==6 && lens[0]==8 && dirs[0]==0x40);
 unsigned char rate[]={0,0x9f,0x24,0,1,0,0,0};
 assert(!memcmp(payload[0],rate,8));
 assert(reqs[1]==7 && vals[1]==(1750000&65535) && idxs[1]==(1750000>>16));
 assert(hackrf_rx_rate(NULL,2000000)==0);
 int before=calls; assert(hackrf_rx_rate(NULL,12345)<0 && calls==before);
 assert(hackrf_rx_tune(NULL,1090000000)==0);
 unsigned char freq[]={0x42,4,0,0,0,0,0,0};
 assert(reqs[4]==16 && !memcmp(payload[4],freq,8));
 assert(hackrf_rx_safe(NULL)==0);
 assert(reqs[5]==1 && vals[5]==0);
 assert(reqs[6]==17 && vals[6]==0);
 assert(reqs[7]==23 && vals[7]==0);
 assert(hackrf_rx_gain(NULL,24,30)==0);
 assert(reqs[8]==19 && dirs[8]==0xc0 && idxs[8]==24);
 assert(reqs[9]==20 && idxs[9]==30);
 before=calls; assert(hackrf_rx_gain(NULL,41,30)<0 && calls==before);
 assert(hackrf_rx_mode(NULL,2)<0 && calls==before); /* TX is rejected */
 before=calls; assert(hackrf_rx_clear_bias_options(NULL,0x0107)==0 && calls==before);
 assert(hackrf_rx_clear_bias_options(NULL,0x0108)==0);
 assert(reqs[calls-1]==48 && vals[calls-1]==0x24);
 fail=1; assert(hackrf_rx_rate(NULL,2400000)<0);
 puts("HackRF protocol/format tests PASS");
}
