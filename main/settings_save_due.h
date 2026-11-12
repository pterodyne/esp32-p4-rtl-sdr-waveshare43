#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Force pending general receiver settings out before a foreground mode retunes
 * the SDR; an unscheduled save never writes anything. Audio controls use a
 * separate immediate NVS record and do not depend on this debounce. */
static inline bool settings_save_due(int64_t pending_at, int64_t now, bool force)
{
    return pending_at != 0 && (force || now >= pending_at);
}
