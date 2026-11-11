#pragma once
#include <stdint.h>

/* Useful manual tuning increments; index is persisted, not the old six-key ID. */
#define TUNE_STEP_COUNT 10
#define TUNE_STEP_DEFAULT 4                 /* 10 kHz */
int tune_step_index(int raw);                 /* sanitize persisted index */
uint32_t tune_step_hz(int index);
const char *tune_step_label(int index);
int tune_step_next(int index);
uint32_t tune_step_offset(uint32_t freq, int index, int direction);
