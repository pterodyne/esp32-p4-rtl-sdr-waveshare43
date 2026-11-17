/* Pure host test for on-screen tune records; no RF/USB hardware is simulated as reception. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hf_diag.h"

int main(void)
{
    hf_diag_state_t d = {0};
    char line[96];
    assert(strstr(hf_diag_line(&d, 0, line, sizeof line), "UNKNOWN"));
    hf_diag_identity(&d, "", "", true);
    assert(!d.exact_v4 && strstr(hf_diag_line(&d, 0, line, sizeof line), "EMPTY"));
    hf_diag_identity(&d, "RTLSDRBlog", "Blog V4L", true);
    assert(!d.exact_v4 && strstr(hf_diag_line(&d, 1, line, sizeof line), "V4L"));
    hf_diag_identity(&d, "RTLSDRBlog", "Blog V4", true);
    assert(d.exact_v4);
    hf_diag_tune(&d, 100000000, 0, (hf_diag_tuner_t){.pll = 1, .gpio5_attempted = true, .gpio5_rc = 0, .gpio5_level = 1});
    hf_diag_tune(&d, 10000000, 0, (hf_diag_tuner_t){.pll = 0, .gpio5_attempted = false});
    assert(d.hf.valid && d.vhf.valid);
    assert(strstr(hf_diag_line(&d, 3, line, sizeof line), "10000000"));
    assert(strstr(hf_diag_line(&d, 4, line, sizeof line), "UNLOCK"));
    assert(strstr(hf_diag_line(&d, 5, line, sizeof line), "not reached"));
    hf_diag_tune(&d, 10000000, -3, (hf_diag_tuner_t){.pll = -1, .gpio5_attempted = true, .gpio5_rc = -3, .gpio5_level = 0});
    assert(strstr(hf_diag_line(&d, 3, line, sizeof line), "rc=-3"));
    assert(strstr(hf_diag_line(&d, 4, line, sizeof line), "not checked"));
    assert(strstr(hf_diag_line(&d, 5, line, sizeof line), "LOW rc=-3"));
    hf_diag_tune(&d, 101000000, 0, (hf_diag_tuner_t){.pll = 1, .gpio5_attempted = true, .gpio5_rc = 0, .gpio5_level = 1});
    assert(strstr(hf_diag_line(&d, 7, line, sizeof line), "LOCK"));
    assert(strstr(hf_diag_line(&d, 8, line, sizeof line), "HIGH rc=0"));
    assert(strstr(hf_diag_line(&d, 9, line, sizeof line), "NOT measured"));
    puts("HF diagnostic state: exact identity, both transitions, unlocked PLL and GPIO errors");
    return 0;
}
