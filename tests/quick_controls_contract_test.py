"""Guard the firmware's quick-gain and auto-range reset touch routes."""
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'main/ui.c').read_text()
actions = src.split('static void do_action(', 1)[1].split('static void panel_event(', 1)[0]
gain = actions.split('if (act == VIEW_AGC_CYCLE)', 1)[1].split('if (act == VIEW_RANGE_RESET)', 1)[0]
for op in ('quick_gain_next(', 'sdr_set_gain_index(', 's_ctl.gain_idx', 'settings_changed();', 'draw_header(act);'):
    assert op in gain, op
reset = actions.split('if (act == VIEW_RANGE_RESET)', 1)[1].split('if (act == VIEW_STEP)', 1)[0]
for op in ('s_ctl.range_auto = true;', 'view_set_range(', 'view_reset();', 'peak_wait_fresh_iq();', 'settings_changed();', 'draw_header(act);'):
    assert op in reset, op
assert reset.index('view_set_range(') < reset.index('view_reset();')
assert 'sdr_set_gain_index(' not in reset
print('Quick gain cycles real tuner presets; range reset re-enables auto and refreshes FFT floor')
