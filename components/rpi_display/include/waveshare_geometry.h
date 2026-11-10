#pragma once

/* Quarter-turns clockwise from the panel's native 480x800 portrait axes.
 * Odd turns are 800x480; even turns are 480x800. */
static inline int waveshare_next_rotation(int rotation) { return (rotation + 1) & 3; }
static inline int waveshare_legacy_rotation(int portrait) { return portrait ? 0 : 3; }
static inline void waveshare_screen_to_panel(int rotation, int sx, int sy, int *px, int *py)
{
    switch (rotation) {
    case 1: *px = 479 - sy; *py = sx;       break;
    case 2: *px = 479 - sx; *py = 799 - sy; break;
    case 3: *px = sy;       *py = 799 - sx; break;
    default: *px = sx;     *py = sy;       break;
    }
}

static inline void waveshare_panel_to_screen(int rotation, int px, int py, int *sx, int *sy)
{
    switch (rotation) {
    case 1: *sx = py;       *sy = 479 - px; break;
    case 2: *sx = 479 - px; *sy = 799 - py; break;
    case 3: *sx = 799 - py; *sy = px;       break;
    default: *sx = px;     *sy = py;       break;
    }
}
