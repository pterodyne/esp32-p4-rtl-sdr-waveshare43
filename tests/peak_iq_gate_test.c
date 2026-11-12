#include <assert.h>
#include <stdio.h>
#include "peak_iq_gate.h"

int main(void)
{
    peak_iq_gate_t gate = {0};
    /* Latest-frame copying needs the whole frame to have arrived after tune,
     * plus a settling span to drain pre-tune USB data. */
    peak_iq_gate_start(&gate, 100000);
    assert(!peak_iq_gate_ready(&gate, 100000, 49152, 96000));
    assert(!peak_iq_gate_ready(&gate, 100000 + 49152 + 96000 - 1, 49152, 96000));
    assert(peak_iq_gate_ready(&gate, 100000 + 49152 + 96000, 49152, 96000));
    assert(peak_iq_gate_ready(&gate, 100000 + 49152 + 96000, 49152, 96000));
    peak_iq_gate_start(&gate, 300000); /* another retune invalidates prior FFT */
    assert(!peak_iq_gate_ready(&gate, 300010, 49152, 96000));
    peak_iq_gate_start(&gate, 400000); /* stream stops before the frame completes */
    assert(!peak_iq_gate_ready(&gate, 400000 + 49152, 49152, 96000));
    assert(peak_iq_gate_ready(&gate, 400000 + 49152 + 96000, 49152, 96000));
    peak_iq_gate_start(&gate, 900000);
    assert(!peak_iq_gate_ready(&gate, 10, 49152, 96000)); /* restart resets byte counter */
    assert(!peak_iq_gate_ready(&gate, 10 + 49152 + 96000 - 1, 49152, 96000));
    assert(peak_iq_gate_ready(&gate, 10 + 49152 + 96000, 49152, 96000));
    puts("Peak IQ: rejects pre-retune/stopped frames until fresh post-transition data");
}
