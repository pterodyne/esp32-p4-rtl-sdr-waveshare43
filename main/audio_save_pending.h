#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { bool dirty; int64_t retry_at_us; } audio_save_pending_t;

static inline void audio_save_pending_mark(audio_save_pending_t *p)
{
    p->dirty = true;
    p->retry_at_us = 0;
}
static inline bool audio_save_pending_due(const audio_save_pending_t *p, int64_t now_us)
{
    return p->dirty && now_us >= p->retry_at_us;
}
static inline void audio_save_pending_result(audio_save_pending_t *p, bool success, int64_t now_us)
{
    p->dirty = !success;
    p->retry_at_us = success ? 0 : now_us + INT64_C(5000000);
}
