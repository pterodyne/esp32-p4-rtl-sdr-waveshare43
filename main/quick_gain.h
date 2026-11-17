#pragma once
/* AGC plus five manual targets, always resolved to the receiver gain table. */
#include <stdlib.h>

static inline int quick_gain_nearest(int count, int target, int (*tenths)(int))
{
    int best = 1;
    for (int i = 2; i < count; i++)
        if (abs(tenths(i) - target) < abs(tenths(best) - target)) best = i;
    return best;
}

static inline int quick_gain_next(int current, int count, int (*tenths)(int))
{
    if (count < 2 || !tenths) return 0;
    const int target[] = {quick_gain_nearest(count, 0, tenths),
                          quick_gain_nearest(count, 150, tenths),
                          quick_gain_nearest(count, 300, tenths),
                          quick_gain_nearest(count, 400, tenths), count - 1};
    int previous = current > 0 && current < count ? tenths(current) : -1;
    for (unsigned i = 0; i < sizeof target / sizeof target[0]; i++)
        if (tenths(target[i]) > previous) return target[i];
    return 0;
}
