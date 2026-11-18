#include <assert.h>
#include <stdio.h>
#include "usb_rx_descriptor.h"
int main(void) {
 unsigned char d[]={9,2,25,0,1,1,0,0x80,50,9,4,0,0,1,0xff,0,0,0,7,5,0x81,2,0,2,0};
 assert(usb_rx_descriptor_valid(d,sizeof d,0x81));
 assert(!usb_rx_descriptor_valid(d,sizeof d,0x82));
 d[22]=64;d[23]=0;assert(!usb_rx_descriptor_valid(d,sizeof d,0x81));
 d[22]=0;d[23]=2;d[12]=1;assert(!usb_rx_descriptor_valid(d,sizeof d,0x81));
 d[12]=0;d[18]=0;assert(!usb_rx_descriptor_valid(d,sizeof d,0x81));
 puts("USB HS RX descriptor tests PASS");
}
