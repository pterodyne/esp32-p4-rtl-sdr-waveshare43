#pragma once
#include <stdbool.h>

/* This records only successful USB commands, never measured antenna voltage. */
typedef struct {
    bool on;
    bool command_ok;
} bias_state_t;

static inline bool bias_state_needs_command(const bias_state_t *s, bool on)
{
    return !s->command_ok || s->on != on;
}

static inline void bias_state_note_result(bias_state_t *s, bool requested_on, bool ok)
{
    s->command_ok = ok;
    if (ok) s->on = requested_on;
}
