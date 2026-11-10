#pragma once
#include <stdbool.h>

/* Native panel coordinates are 480 x 800. Landscape is a clockwise quarter
 * turn in software; the DPI panel does not support hardware axis exchange. */
static inline void waveshare_screen_to_panel(bool portrait, int sx, int sy, int *px, int *py)
{
    *px = portrait ? sx : sy;
    *py = portrait ? sy : 799 - sx;
}

static inline void waveshare_panel_to_screen(bool portrait, int px, int py, int *sx, int *sy)
{
    *sx = portrait ? px : 799 - py;
    *sy = portrait ? py : px;
}
