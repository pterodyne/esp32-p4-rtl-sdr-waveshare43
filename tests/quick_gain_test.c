#include <assert.h>
#include <stdio.h>
#include "quick_gain.h"

static const int gains[] = {-1, 100, 150, 200, 300, 400, 500};
static int gain_tenths(int i) { return gains[i]; }
int main(void)
{
    /* Same four presets as Controls > GAIN: AGC, 15 dB, 30 dB, MAX. */
    assert(quick_gain_next(0, 7, gain_tenths) == 2);
    assert(quick_gain_next(2, 7, gain_tenths) == 4);
    assert(quick_gain_next(4, 7, gain_tenths) == 6);
    assert(quick_gain_next(6, 7, gain_tenths) == 0);
    assert(quick_gain_next(1, 7, gain_tenths) == 2); /* a manual value between presets */
    assert(quick_gain_next(3, 7, gain_tenths) == 4);
    assert(quick_gain_next(5, 7, gain_tenths) == 6);
    assert(quick_gain_next(0, 1, gain_tenths) == 0); /* no tuner-gain list */
    assert(quick_gain_next(1, 2, gain_tenths) == 0);
    puts("top gain key cycles existing AGC/15/30/MAX presets and wraps");
}
