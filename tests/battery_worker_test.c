#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../main/battery_voltage.c"
static jmp_buf end;
static int fail_cal,fail_sample,bad_sample,reads,delays,total_wait;
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*c,void**p){assert(c->unit_id==1);*p=(void*)1;return 0;}
int adc_oneshot_config_channel(void*a,int ch,const adc_oneshot_chan_cfg_t*c){assert(a && ch==4 && c->atten==12);return 0;}
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*c,void**p){assert(c->chan==4);if(fail_cal)return -1;*p=(void*)2;return 0;}
int adc_oneshot_get_calibrated_result(void*a,void*c,int ch,int*mv){assert(a && c && ch==4);reads++;*mv=bad_sample && reads==3 ? 0 : reads%2 ? 1290:1310;return fail_sample && reads==4 ? -1:0;}
void adc_cali_delete_scheme_curve_fitting(void*c){assert(c);}
void adc_oneshot_del_unit(void*a){assert(a);}
void vTaskDelete(void*p){(void)p;longjmp(end,1);}
void vTaskDelay(int ms){delays++;total_wait+=ms;if(ms==4700)longjmp(end,1);assert(ms==20);}
int xTaskCreate(void(*f)(void*),const char*n,int stack,void*arg,int priority,void*out){(void)f;(void)n;(void)arg;(void)out;assert(stack>=3072 && priority==1);return 1;}
int main(void){
 for(volatile int mode=0;mode<4;mode++){
  fail_cal=mode==1;fail_sample=mode==2;bad_sample=mode==3;
  reads=delays=total_wait=0;atomic_store(&voltage,-1);
  if(!setjmp(end))measure(NULL);
  assert(battery_voltage_get()==(mode==0?3900:-1));
  assert(reads==(mode==1?0:16));
  if(mode!=1)assert(delays==17 && total_wait==5020);
 }
 puts("battery actual worker: calibration failure, read error, invalid sample, 16-sample averaging and 5s cadence");
}
