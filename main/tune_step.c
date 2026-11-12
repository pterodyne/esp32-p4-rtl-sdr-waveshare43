#include <stdint.h>
#include "tune_step.h"

typedef struct { uint32_t hz; const char *label; } step_t;
static const step_t steps[TUNE_STEP_COUNT] = {
    {100, "100Hz"}, {1000, "1k"}, {5000, "5k"}, {9000, "9k"},
    {10000, "10k"}, {12500, "12.5k"}, {25000, "25k"}, {50000, "50k"},
    {100000, "100k"}, {1000000, "1M"},
    /* Append: indexes 0..9 are already stored in NVS. */
    {500000, "500k"}, {5000000, "5M"},
};

static int valid_index(int i) { return i >= 0 && i < TUNE_STEP_COUNT ? i : TUNE_STEP_DEFAULT; }
int tune_step_index(int raw) { return valid_index(raw); }
uint32_t tune_step_hz(int i) { return steps[valid_index(i)].hz; }
const char *tune_step_label(int i) { return steps[valid_index(i)].label; }
int tune_step_next(int i)
{
    /* Increasing RF increment, without changing persisted IDs. */
    static const int next[TUNE_STEP_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 9, 0};
    return next[valid_index(i)];
}
uint32_t tune_step_offset(uint32_t freq, int i, int direction)
{
    uint32_t step = tune_step_hz(i);
    if (direction < 0) return freq < step ? 0 : freq - step;
    uint64_t next = (uint64_t)freq + step;
    return next > UINT32_MAX ? UINT32_MAX : (uint32_t)next;
}
