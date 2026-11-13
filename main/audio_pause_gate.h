#pragma once
#include <stdbool.h>
#include <stdatomic.h>

/* The UI may retune only after the audio task has left any blocking codec
 * write. The ack is set by that task at its next pause checkpoint. */
typedef struct {
    atomic_bool requested;
    atomic_bool acknowledged;
} audio_pause_gate_t;
#define AUDIO_PAUSE_GATE_INIT {ATOMIC_VAR_INIT(true), ATOMIC_VAR_INIT(false)}

static inline void audio_pause_gate_request(audio_pause_gate_t *gate, bool pause)
{
    atomic_store_explicit(&gate->acknowledged, false, memory_order_release);
    atomic_store_explicit(&gate->requested, pause, memory_order_release);
}

static inline bool audio_pause_gate_task_observe(audio_pause_gate_t *gate)
{
    bool paused = atomic_load_explicit(&gate->requested, memory_order_acquire);
    atomic_store_explicit(&gate->acknowledged, paused, memory_order_release);
    return paused;
}

static inline bool audio_pause_gate_acknowledged(const audio_pause_gate_t *gate)
{
    return atomic_load_explicit(&gate->acknowledged, memory_order_acquire);
}
