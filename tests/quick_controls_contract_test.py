"""Guard the firmware's quick-gain and auto-range reset touch routes."""
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'main/ui.c').read_text()
actions = src.split('static void do_action(', 1)[1].split('static void panel_event(', 1)[0]
gain = actions.split('if (act == VIEW_AGC_CYCLE)', 1)[1].split('if (act == VIEW_RANGE_RESET)', 1)[0]
for op in ('quick_gain_next(', 'sdr_set_gain_index(', 's_ctl.gain_idx', 'settings_changed();', 'draw_header(act);'):
    assert op in gain, op
reset = actions.split('if (act == VIEW_RANGE_RESET)', 1)[1].split('if (act == VIEW_STEP || act == VIEW_STEP_DOWN)', 1)[0]
for op in ('s_ctl.range_auto = true;', 'view_set_range(', 'view_reset();', 'peak_wait_fresh_iq();', 'settings_changed();', 'draw_header(act);'):
    assert op in reset, op
assert reset.index('view_set_range(') < reset.index('view_reset();')
assert 'sdr_set_gain_index(' not in reset
step = actions.split('if (act == VIEW_STEP || act == VIEW_STEP_DOWN)', 1)[1].split('if (act == VIEW_DN1M', 1)[0]
assert 'tune_step_previous(s_tune_step)' in step and 'tune_step_next(s_tune_step)' in step
assert 'settings_changed();' in step and 'view_set_tune_step(' in step
assert 'retune(' not in step, 'step selection must not change actual frequency'
arrows = actions.split('if (act == VIEW_DN1M || act == VIEW_UP1M)', 1)[1].split('if (act == VIEW_GAIN)', 1)[0]
assert 'retune(tune_step_offset(' in arrows, 'the outer arrows must still tune'
zoom = actions.split('if (act == VIEW_ZOOM_TOGGLE)', 1)[1].split('if (act == VIEW_RANGE_RESET)', 1)[0]
assert '(s_ctl.zoom + 1) % PANEL_ZOOMS' in zoom, 'header ZOOM must cycle forward with wrap'
for op in ('apply_view();', 'settings_changed();', 'draw_header(act);'):
    assert op in zoom, op
repeat = src.rsplit('else if (where == 0 && ', 1)[1].split('/*', 1)[0]
assert 'view_header_repeats(act)' in repeat
policy = (Path(__file__).resolve().parents[1] / 'main/view.h').read_text()
assert 'act < VIEW_GAIN' in policy and 'act != VIEW_STEP' in policy
header = (Path(__file__).resolve().parents[1] / 'main/view.h').read_text()
assert header.index('VIEW_STEP_DOWN') > header.index('VIEW_GAIN'), 'down selector must not hold-repeat'
assert header.index('VIEW_ZOOM_TOGGLE') > header.index('VIEW_GAIN'), 'zoom toggle must not hold-repeat'
poll = src.split('static void poll_touch(', 1)[1]
consume = poll.index('if (screen_action & SCREEN_CONSUME)')
assert consume < poll.index('touch_cal_map('), 'wake must be consumed before hit testing'
assert 'return;' in poll[consume:poll.index('touch_cal_map(')]
print('Quick controls: real gain table, auto-range reset, bidirectional saved STEP, separate tuning, '
      'wrap-forward header ZOOM, no selector repeat, wake guard')
