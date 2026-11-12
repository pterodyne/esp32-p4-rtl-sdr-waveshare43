#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A latest-frame copy can straddle a frequency change or stream restart.
 * Require a settling span plus the full copied frame to arrive AFTER the
 * transition before a displayed FFT is eligible for PEAK tuning. */
typedef struct { uint64_t after; bool waiting; } peak_iq_gate_t;

static inline void peak_iq_gate_start(peak_iq_gate_t *g, uint64_t newest)
{
    g->after = newest;
    g->waiting = true;
}

static inline bool peak_iq_gate_ready(peak_iq_gate_t *g, uint64_t newest,
                                      uint64_t frame_bytes, uint64_t settle_bytes)
{
    if (!g->waiting) return true;
    if (newest < g->after) { /* source counter reset on reconnect */
        g->after = newest;
        return false;
    }
    if (newest - g->after < frame_bytes + settle_bytes) return false;
    g->waiting = false;
    return true;
}
