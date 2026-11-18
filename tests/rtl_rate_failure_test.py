"""Compile real RTL rate setter/getter with a failing hardware command seam."""
from pathlib import Path
import os, subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'main/sdr.c').read_text()
body=s.split('void sdr_set_sample_rate(uint32_t hz)',1)[1].split('bool sdr_bias_tee(void)',1)[0]
h=(root/'main/sdr.h').read_text()
checked='static inline bool sdr_request_sample_rate(uint32_t hz)' + h.split('static inline bool sdr_request_sample_rate(uint32_t hz)',1)[1].split('\n}',1)[0] + '\n}\n'
preamble='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define SDR_SAMPLE_RATE 2400000
static void *s_dev=(void*)1, *s_rx_task;
static uint32_t s_rate=SDR_SAMPLE_RATE;
static bool s_rate_known=true;
static int fails, calls;
static int rtlsdr_set_sample_rate(void*d,uint32_t r){(void)d;(void)r;calls++;if(fails){fails--;return -1;}return 0;}
'''
test='''
int main(void){
 fails=1;assert(!sdr_request_sample_rate(2000000));assert(sdr_sample_rate()==2400000);assert(calls==2);
 fails=2;assert(!sdr_request_sample_rate(2000000));assert(sdr_sample_rate()==0);
 fails=0;assert(sdr_request_sample_rate(2400000));assert(sdr_sample_rate()==2400000);
 s_rx_task=(void*)1;int before=calls;sdr_set_sample_rate(2000000);assert(calls==before);assert(sdr_sample_rate()==2400000);
 s_rx_task=0;s_dev=0;sdr_set_sample_rate(2000000);assert(sdr_sample_rate()==0);
 return 0;
}
'''
p=Path(os.environ['TMPDIR'])/'rtl_rate_failure_test.c';p.write_text(preamble+'void sdr_set_sample_rate(uint32_t hz)'+body+checked+test)
exe=p.with_suffix('');subprocess.run(['cc','-Wall','-Wextra','-Wno-unused-variable',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('RTL actual checked rate failure/rollback/unknown/active-owner seam PASS')
