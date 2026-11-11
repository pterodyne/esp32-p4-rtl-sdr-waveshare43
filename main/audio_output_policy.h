#pragma once
#include <stdbool.h>
#include <stdatomic.h>

/* Mute silences the codec without replacing the remembered volume. */
static inline bool audio_output_should_mute(int volume, bool muted)
{
    return volume == 0 || muted;
}

typedef struct {
    atomic_int pending_volume;              /* -1 = no pending codec update */
    atomic_bool muted;
    int desired_volume;                     /* only the UI task writes this field */
} audio_output_requests_t;

#define AUDIO_OUTPUT_REQUESTS_INIT { .pending_volume = ATOMIC_VAR_INIT(-1), \
                                     .muted = ATOMIC_VAR_INIT(false) }

static inline void audio_output_set_volume(audio_output_requests_t *r, int volume)
{
    r->desired_volume = volume < 0 ? 0 : volume > 10 ? 10 : volume;
    atomic_store_explicit(&r->pending_volume, r->desired_volume, memory_order_release);
}

static inline void audio_output_set_mute(audio_output_requests_t *r, bool muted)
{
    atomic_store_explicit(&r->muted, muted, memory_order_release);
    atomic_store_explicit(&r->pending_volume, r->desired_volume, memory_order_release);
}

static inline int audio_output_take_volume(audio_output_requests_t *r)
{
    /* Take BEFORE applying the slow I2C command. A subsequent tap stays queued. */
    return atomic_exchange_explicit(&r->pending_volume, -1, memory_order_acq_rel);
}

static inline bool audio_output_is_muted(audio_output_requests_t *r)
{
    return atomic_load_explicit(&r->muted, memory_order_acquire);
}
