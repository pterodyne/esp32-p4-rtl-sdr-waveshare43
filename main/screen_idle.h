#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { SCREEN_SLEEP = 1, SCREEN_WAKE = 2, SCREEN_CONSUME = 4 };
typedef struct { int64_t last_touch, release_since; bool asleep, consume; } screen_idle_t;
static inline bool screen_timeout_valid(int seconds) { return seconds == 0 || seconds == 30 || seconds == 60 || seconds == 90; }
static inline int screen_idle_update(screen_idle_t *s, int seconds, int64_t now, bool contact)
{
    int action = 0;
    if (!screen_timeout_valid(seconds)) seconds = 0; /* corrupt setting fails awake */
    if (contact) s->last_touch = now;
    if (s->asleep && (contact || seconds == 0)) {
        s->asleep = false;
        s->consume = contact;
        s->release_since = 0;
        action |= SCREEN_WAKE;
    } else if (!s->asleep && !contact && seconds && now - s->last_touch >= (int64_t)seconds * 1000000) {
        s->asleep = true;
        action |= SCREEN_SLEEP;
    }
    if (s->consume) {
        action |= SCREEN_CONSUME;
        if (contact) s->release_since = 0;
        else if (!s->release_since) s->release_since = now;
        else if (now - s->release_since >= 100000) {
            /* Capacitive touch can briefly report no contact (or a read error)
             * while a finger is still down. Require a stable lift before
             * accepting the next tap as a new gesture. */
            s->consume = false;
            s->release_since = 0;
        }
    }
    return action;
}
