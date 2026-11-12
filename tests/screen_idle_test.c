#include <assert.h>
#include <stdio.h>
#include "screen_idle.h"
int main(void) {
    screen_idle_t s = {0};
    assert(screen_timeout_valid(30) && screen_timeout_valid(60) && screen_timeout_valid(90) && screen_timeout_valid(0));
    assert(!screen_timeout_valid(-1) && !screen_timeout_valid(45));
    assert(screen_idle_update(&s, 30, 29999999, false) == 0);
    assert(screen_idle_update(&s, 30, 30000000, false) == SCREEN_SLEEP);
    assert(s.asleep);
    assert(screen_idle_update(&s, 30, 31000000, true) == (SCREEN_WAKE | SCREEN_CONSUME));
    assert(!s.asleep);
    assert(screen_idle_update(&s, 30, 32000000, true) == SCREEN_CONSUME);
    assert(screen_idle_update(&s, 30, 33000000, false) == SCREEN_CONSUME); /* one false sample */
    assert(screen_idle_update(&s, 30, 33040000, true) == SCREEN_CONSUME); /* still same finger */
    assert(screen_idle_update(&s, 30, 34000000, false) == SCREEN_CONSUME);
    assert(screen_idle_update(&s, 30, 34100000, false) == SCREEN_CONSUME); /* release debounce */
    assert(screen_idle_update(&s, 30, 34110000, true) == 0); /* next gesture usable */
    assert(screen_idle_update(&s, 30, 64000000, true) == 0); /* held touch never blanks */
    assert(screen_idle_update(&s, 0, 999999999, false) == 0);
    for (int sec = 60; sec <= 90; sec += 30) {
        s = (screen_idle_t){0};
        assert(!screen_idle_update(&s, sec, (int64_t)sec * 1000000 - 1, false));
        assert(screen_idle_update(&s, sec, (int64_t)sec * 1000000, false) == SCREEN_SLEEP);
        assert(screen_idle_update(&s, 0, (int64_t)sec * 1000000 + 1, false) == SCREEN_WAKE);
    }
    puts("screen idle: deadlines, never, held contact, wake gesture consumed through release");
}
