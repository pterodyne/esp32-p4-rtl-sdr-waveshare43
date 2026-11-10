#include <assert.h>
#include "waveshare_geometry.h"

int main(void)
{
    int x, y;
    waveshare_screen_to_panel(true, 0, 0, &x, &y);
    assert(x == 0 && y == 0);
    waveshare_panel_to_screen(true, 479, 799, &x, &y);
    assert(x == 479 && y == 799);
    waveshare_screen_to_panel(false, 0, 0, &x, &y);
    assert(x == 0 && y == 799);
    waveshare_screen_to_panel(false, 799, 479, &x, &y);
    assert(x == 479 && y == 0);
    waveshare_panel_to_screen(false, 479, 0, &x, &y);
    assert(x == 799 && y == 479);
    for (int sy = 0; sy < 480; sy += 37)
        for (int sx = 0; sx < 800; sx += 43) {
            int px, py;
            waveshare_screen_to_panel(false, sx, sy, &px, &py);
            waveshare_panel_to_screen(false, px, py, &x, &y);
            assert(x == sx && y == sy);
        }
    return 0;
}
