#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tune_step.h"

int main(void)
{
    static const uint32_t steps[] = {100, 1000, 5000, 9000, 10000,
                                     12500, 25000, 50000, 100000, 1000000, 500000, 5000000};
    assert(TUNE_STEP_COUNT == (int)(sizeof steps / sizeof steps[0]));
    for (int i = 0; i < TUNE_STEP_COUNT; i++) {
        assert(tune_step_index(i) == i);
        assert(tune_step_hz(i) == steps[i]);
        assert(tune_step_label(i) && tune_step_label(i)[0]);
        static const int next[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 9, 0};
        assert(tune_step_next(i) == next[i]);
    }
    assert(!strcmp(tune_step_label(5), "12.5k"));
    assert(tune_step_index(-1) == TUNE_STEP_DEFAULT);
    assert(tune_step_index(TUNE_STEP_COUNT) == TUNE_STEP_DEFAULT);
    assert(tune_step_hz(-1) == 10000); /* bad saved index: useful default */
    assert(tune_step_offset(27185000, 4, +1) == 27195000);
    assert(tune_step_offset(27185000, 5, -1) == 27172500);
    assert(tune_step_offset(1000, 9, -1) == 0); /* do not wrap below zero */
    assert(tune_step_offset(915000000, 10, 1) == 915500000);
    assert(tune_step_offset(915000000, 11, -1) == 910000000);
    assert(tune_step_offset(UINT32_MAX, 11, 1) == UINT32_MAX);
    assert(!strcmp(tune_step_label(10), "500k") && !strcmp(tune_step_label(11), "5M"));
    puts("Tuning steps: 100 Hz to 5 MHz, stable saved IDs, ordered cycle, safe arithmetic");
    return 0;
}
