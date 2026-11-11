#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tune_step.h"

int main(void)
{
    static const uint32_t steps[] = {100, 1000, 5000, 9000, 10000,
                                     12500, 25000, 50000, 100000, 1000000};
    assert(TUNE_STEP_COUNT == (int)(sizeof steps / sizeof steps[0]));
    for (int i = 0; i < TUNE_STEP_COUNT; i++) {
        assert(tune_step_index(i) == i);
        assert(tune_step_hz(i) == steps[i]);
        assert(tune_step_label(i) && tune_step_label(i)[0]);
        assert(tune_step_next(i) == (i + 1) % TUNE_STEP_COUNT);
    }
    assert(!strcmp(tune_step_label(5), "12.5k"));
    assert(tune_step_index(-1) == TUNE_STEP_DEFAULT);
    assert(tune_step_index(TUNE_STEP_COUNT) == TUNE_STEP_DEFAULT);
    assert(tune_step_hz(-1) == 10000); /* bad saved index: useful default */
    assert(tune_step_offset(27185000, 4, +1) == 27195000);
    assert(tune_step_offset(27185000, 5, -1) == 27172500);
    assert(tune_step_offset(1000, 9, -1) == 0); /* do not wrap below zero */
    puts("Tuning steps: 100 Hz to 1 MHz, wrap and safe arithmetic");
    return 0;
}
