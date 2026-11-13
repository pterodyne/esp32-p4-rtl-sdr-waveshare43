#include <assert.h>
#include <stdio.h>
#include "audio_pause_gate.h"

int main(void)
{
    audio_pause_gate_t gate = AUDIO_PAUSE_GATE_INIT;
    assert(audio_pause_gate_task_observe(&gate));
    assert(audio_pause_gate_acknowledged(&gate));
    audio_pause_gate_request(&gate, false);
    assert(!audio_pause_gate_acknowledged(&gate));
    assert(!audio_pause_gate_task_observe(&gate));
    audio_pause_gate_request(&gate, true);
    assert(!audio_pause_gate_acknowledged(&gate)); /* old ack cannot authorize an early retune */
    assert(audio_pause_gate_task_observe(&gate));
    assert(audio_pause_gate_acknowledged(&gate));
    puts("Audio pause ack follows task quiescence, not the UI request");
    return 0;
}
