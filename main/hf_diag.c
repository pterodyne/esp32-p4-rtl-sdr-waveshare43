#include "hf_diag.h"
#include <stdio.h>
#include <string.h>

void hf_diag_identity(hf_diag_state_t *d, const char *m, const char *p, bool read_ok)
{
    d->identity_read = read_ok;
    snprintf(d->manufacturer, sizeof d->manufacturer, "%s", read_ok && m ? m : "");
    snprintf(d->product, sizeof d->product, "%s", read_ok && p ? p : "");
    d->exact_v4 = read_ok && m && p && !strcmp(m, "RTLSDRBlog") && !strcmp(p, "Blog V4");
}

void hf_diag_tune(hf_diag_state_t *d, uint32_t hz, int rc, hf_diag_tuner_t tuner)
{
    hf_diag_tune_t t = {.valid = true, .requested_hz = hz, .center_rc = rc, .tuner = tuner};
    d->latest = t;
    if (hz < 28800000) d->hf = t;
    else if (hz < 250000000) d->vhf = t;
}

static const char *pll(const hf_diag_tune_t *t)
{
    return !t->valid || t->tuner.pll < 0 ? "not checked" : t->tuner.pll ? "LOCK" : "UNLOCK";
}

const char *hf_diag_line(const hf_diag_state_t *d, int i, char *out, size_t n)
{
    const hf_diag_tune_t *t = i >= 3 && i <= 5 ? &d->hf : &d->vhf;
    if (!n) return out;
    switch (i) {
    case 0: snprintf(out, n, "USB M: %s", !d->identity_read ? "UNKNOWN (read failed)" :
                     !d->manufacturer[0] ? "EMPTY / UNKNOWN" : d->manufacturer); break;
    case 1: snprintf(out, n, "USB P: %s", !d->identity_read ? "UNKNOWN (read failed)" :
                     !d->product[0] ? "EMPTY / UNKNOWN" : d->product); break;
    case 2: snprintf(out, n, "Exact Blog V4: %s", d->exact_v4 ? "YES" : "NO / UNKNOWN"); break;
    case 3: case 6:
        snprintf(out, n, "%s req %lu Hz center rc=%d", i == 3 ? "HF" : "VHF",
                 (unsigned long)(t->valid ? t->requested_hz : 0), t->valid ? t->center_rc : 0);
        if (!t->valid) snprintf(out, n, "%s: no tune recorded", i == 3 ? "HF" : "VHF");
        break;
    case 4: case 7:
        snprintf(out, n, "%s PLL %s", i == 4 ? "HF" : "VHF", pll(t));
        break;
    case 5: case 8:
        if (t->valid && t->tuner.gpio5_attempted)
            snprintf(out, n, "%s GPIO5 %s rc=%d", i == 5 ? "HF" : "VHF",
                     t->tuner.gpio5_level ? "HIGH" : "LOW", t->tuner.gpio5_rc);
        else snprintf(out, n, "%s GPIO5 not reached / unchanged", i == 5 ? "HF" : "VHF");
        break;
    case 9: snprintf(out, n, "RF NOT measured (commands only)"); break;
    default: out[0] = 0; break;
    }
    return out;
}
