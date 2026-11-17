#include <assert.h>
#include "battery_voltage.h"
int main(void) {
 assert(battery_from_adc_mv(1300, true)==3900);
 assert(battery_from_adc_mv(1300, false)==-1);
 assert(battery_from_adc_mv(0,true)==-1);
 assert(battery_from_adc_mv(4095,true)==-1);
 assert(battery_level(3901)==2); assert(battery_level(3900)==1);
 assert(battery_level(3600)==1); assert(battery_level(3599)==0);
 assert(battery_level(-1)==-1);
 return 0;
}
