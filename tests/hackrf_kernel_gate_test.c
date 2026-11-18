/* Selection/safety gate test, NOT execution of P4 instructions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hackrf_resample.h"
static int corrupt=1;
void hackrf_dot96_p4(const int16_t *c,const int16_t *i,const int16_t *q,int32_t *out) {
 out[0]=out[1]=0;
 for(int k=0;k<96;k++) {out[0]+=c[k]*i[k];out[1]+=c[k]*q[k];}
 if(corrupt) out[0]++;
}
int main(void) {
 assert(!hackrf_resample_selftest());
 corrupt=0;assert(hackrf_resample_selftest());
 puts("Selected P4 kernel oracle gate detects corrupt kernel PASS (host seam)");
}
