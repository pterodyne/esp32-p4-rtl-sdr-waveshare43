#pragma once
#include <stdbool.h>

/* Station metadata keeps updating while the speaker is muted. */
static inline bool ui_should_log_rds(int bw, bool scanning, bool muted, bool has_ps)
{
    (void)muted;
    return bw == 0 && !scanning && has_ps;
}
