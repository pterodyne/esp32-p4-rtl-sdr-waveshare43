#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Force pending receiver settings out before a foreground mode retunes the
 * SDR; an unscheduled save never writes anything. Commit error handling and
 * immediate volume/squelch/mute persistence belong to the roadmap task. */
static inline bool settings_save_due(int64_t pending_at, int64_t now, bool force)
{
    return pending_at != 0 && (force || now >= pending_at);
}
