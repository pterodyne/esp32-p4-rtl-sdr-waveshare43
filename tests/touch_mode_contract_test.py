"""Guard the ESP-IDF UI route for immediate TOUCH persistence."""
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'main/ui.c').read_text()
assert '#include "touch_mode_store.h"' in src
boot = src.split('void ui_run(', 1)[1].split('draw_header(-1);', 1)[0]
assert 'view_set_touch_mode(touch_mode_loaded(nvs_int("touchmode", VIEW_TOUCH_FREE)))' in boot
press = src.split('static void do_action(', 1)[1].split('static void panel_event(', 1)[0]
mode = press.split('if (act == VIEW_TOUCH_MODE)', 1)[1].split('if (act == VIEW_STEP)', 1)[0]
assert 'touch_mode_try_save();' in mode and mode.index('view_set_touch_mode(') < mode.index('touch_mode_try_save();')
poll = src.split('static void poll_touch(', 1)[1].split('if (!contact)', 1)[0]
assert 'touch_mode_try_save();' in poll, 'failed commit must retry even during stream recovery/foreground modes'
print('TOUCH mode restores before header and commits promptly with polling retry')
