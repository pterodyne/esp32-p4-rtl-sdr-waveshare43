#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HACKRF_NATIVE_RATE 8000000u
#define HACKRF_FIR_TAPS 96
/* Output-driven polyphase FIR: only the required 3/10 or 1/4 outputs are evaluated. */
typedef struct {
 int16_t coeff[3][HACKRF_FIR_TAPS] __attribute__((aligned(16)));
 /* Mirrored, planar history makes each 96-tap dot product contiguous. */
 int16_t history[2][2*HACKRF_FIR_TAPS] __attribute__((aligned(16)));
 unsigned head, phase, up, down;
 bool pending; int8_t i;
} hackrf_resample_t;
/* Firmware runs the selected dot kernel against a scalar oracle before USB. */
bool hackrf_resample_selftest(void);
bool hackrf_resample_init(hackrf_resample_t *,uint32_t);
/* Capacity must cover ceil(bytes/2 * up/down)*2 + 2. Returns SIZE_MAX on
 * insufficient capacity (state unchanged). Arbitrary byte splits are accepted. */
size_t hackrf_resample_process(hackrf_resample_t *,const uint8_t *,size_t,uint8_t *,size_t);
