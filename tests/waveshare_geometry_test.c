#include <assert.h>
#include "waveshare_geometry.h"

/* Quarter-turns clockwise from the native 480x800 portrait orientation. */
int main(void)
{
    const int corners[4][4] = {
        {0, 0, 479, 799},
        {479, 0, 0, 799},
        {479, 799, 0, 0},
        {0, 799, 479, 0},
    };
    for (int rotation = 0; rotation < 4; ++rotation) {
        int sw = rotation & 1 ? 800 : 480;
        int sh = rotation & 1 ? 480 : 800;
        int x, y;
        waveshare_screen_to_panel(rotation, 0, 0, &x, &y);
        assert(x == corners[rotation][0] && y == corners[rotation][1]);
        waveshare_screen_to_panel(rotation, sw - 1, sh - 1, &x, &y);
        assert(x == corners[rotation][2] && y == corners[rotation][3]);
        for (int sy = 0; sy < sh; sy += 37) {
            for (int sx = 0; sx < sw; sx += 43) {
                int px, py;
                waveshare_screen_to_panel(rotation, sx, sy, &px, &py);
                assert(px >= 0 && px < 480 && py >= 0 && py < 800);
                waveshare_panel_to_screen(rotation, px, py, &x, &y);
                assert(x == sx && y == sy);
            }
        }
        /* A non-square dirty rectangle must map to a complete panel rectangle,
         * and each destination pixel must read exactly one source pixel. */
        const int rx = 7, ry = 13, rw = 31, rh = 19;
        int px0, py0, px1, py1, count = 0;
        waveshare_screen_to_panel(rotation, rx, ry, &px0, &py0);
        waveshare_screen_to_panel(rotation, rx + rw - 1, ry + rh - 1, &px1, &py1);
        if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
        if (py0 > py1) { int t = py0; py0 = py1; py1 = t; }
        assert((px1 - px0 + 1) * (py1 - py0 + 1) == rw * rh);
        for (int py = py0; py <= py1; ++py)
            for (int px = px0; px <= px1; ++px) {
                waveshare_panel_to_screen(rotation, px, py, &x, &y);
                assert(x >= rx && x < rx + rw && y >= ry && y < ry + rh);
                ++count;
            }
        assert(count == rw * rh);
    }
    int rotation = 0;
    for (int i = 0; i < 4; ++i) {
        rotation = waveshare_next_rotation(rotation);
        assert(rotation == ((i + 1) & 3));
    }
    assert(rotation == 0);
    assert(waveshare_legacy_rotation(1) == 0); /* old port=1 was native portrait */
    assert(waveshare_legacy_rotation(0) == 3); /* old port=0 was CCW landscape */
    return 0;
}
