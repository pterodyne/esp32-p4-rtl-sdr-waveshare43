/* A failed USB OFF transaction must never be cached as confirmed OFF. */
#include <assert.h>
#include <stdio.h>
#include "bias_state.h"

int main(void)
{
    bias_state_t s = {0};
    assert(bias_state_needs_command(&s, false)); /* startup OFF must be sent */
    bias_state_note_result(&s, false, false);     /* USB transfer failed */
    assert(!s.command_ok);
    assert(bias_state_needs_command(&s, false));  /* same OFF must retry */
    bias_state_note_result(&s, false, true);
    assert(s.command_ok);
    assert(!bias_state_needs_command(&s, false));
    bias_state_note_result(&s, true, false);
    assert(!s.command_ok && bias_state_needs_command(&s, false));
    bias_state_note_result(&s, false, true);
    assert(s.command_ok && !s.on);
    puts("Bias-T state: failed OFF is unknown and retryable");
    return 0;
}
