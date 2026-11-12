#!/bin/sh
# Host controls/policy regression suite; no IDF or hardware required.
set -eu
cd "$(dirname "$0")/.."
: "${TMPDIR:?Set TMPDIR to a writable scratch directory}"
out="$TMPDIR/p4-control-tests"
mkdir -p "$out"
flags='-O2 -Wall -Wextra -Werror -Itests/stubs -Itools/preview/stub -Imain -Icomponents/rpi_display/include -Icomponents/rtlsdr/blog/include'
for name in audio_mute audio_pause_gate audio_save_pending bias_state sensor_event_age settings_save_due ui_audio_policy waveshare_geometry screen_idle screen_timeout_store display_power peak_iq_gate; do
    ${CC:-cc} $flags "tests/${name}_test.c" -o "$out/$name"
    "$out/$name"
done
for name in tune_step theme_store audio_settings_store rf_policy weather_wh2 tpms_steelmate; do
    ${CC:-cc} $flags "tests/${name}_test.c" "main/${name}.c" -lm -o "$out/$name"
    "$out/$name"
done
python3 tests/ui_audio_recovery_test.py
python3 tests/ui_controls_contract_test.py
# Guard the firmware touch route as well as the preview's real view/FFT math.
python3 - <<'PY'
from pathlib import Path
s = Path('main/ui.c').read_text()
press = s.split('if (n++ == 0)', 1)[1].split('/* move */', 1)[0]
release = s.split('if (!contact)', 1)[1].split('int px, py;', 1)[0]
assert 'view_spectrum_bottom()' in press and '? 2 : 5' in press
assert 'view_touch_frequency(' in release and 'view_spectrum_bottom()' in release
assert 'x >= 0 && x < LCD_W' in release, 'release X must be inside the spectrum'
assert 'mode != VIEW_TOUCH_PEAK || sdr_streaming()' in release, 'no peak tune on a lost stream'
retune = s.split('static void retune(', 1)[1].split('static void panel_show(', 1)[0]
helper = s.split('static void peak_wait_fresh_iq(', 1)[1].split('static void ', 1)[0]
assert 'view_peak_invalidate();' in helper
outer = s.split('for (;;) {', 1)[1].split('if (s_adsb_req)', 1)[0]
assert 'peak_wait_fresh_iq();' in outer, 'invalidate before polling touch on stream loss'
assert 'peak_wait_fresh_iq();' in retune, 'start a fresh-IQ wait on retune'
assert 'peak_wait_fresh_iq();' in outer, 'restart needs new IQ after stream loss'
assert 'peak_iq_gate_start(' in s and 'peak_iq_gate_ready(' in s
assert 'frame_end = sdr_ring_latest(' in s
assert s.index('if (!peak_iq_gate_ready(') > s.index('view_render(s_db, n, sdr_freq())'), 'do not re-enable stale Peak after rendering'
memory = s.split('static void tune_memory(', 1)[1].split('static void mscan_stop(', 1)[0]
assert 'peak_wait_fresh_iq();' in memory, 'memory hops must discard old IQ'
panel = s.split('static void panel_event(', 1)[1].split('static void poll_touch(', 1)[0]
assert 'peak_wait_fresh_iq();' in panel, 'BAND preset retunes must discard old IQ'
assert 'VIEW_TOUCH_MODE' in s and 'view_set_touch_mode(' in s
assert '"FREEFORM"' in Path('main/view.c').read_text()
assert 'tune_step_hz(s_tune_step)' in release
assert 'mode == VIEW_TOUCH_PEAK ? touch_x0 : x' in release
print('UI touch route: spectrum press/release, mode cycle, saved STEP, Peak press anchor')
PY
D=components/rpi_display
sources="tools/preview/preview.c main/view.c main/panel.c main/adsb.c main/adsb_view.c main/amap.c main/scan_view.c main/sensor_view.c main/spectrum.c $D/rpi_canvas.c $D/rpi_text.c"
for board in original waveshare43; do
    define=''
    if [ "$board" = waveshare43 ]; then define='-DCONFIG_SDR_WAVESHARE_43'; fi
    ${CC:-cc} -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
        $define -Itools/preview/stub -I"$D/include" -I"$D" -Imain $sources -lz -lm -o "$out/preview-$board"
    for test in audio bands theme phosphor footer-recovery rf ism sensors screen touch-tuning; do
        "$out/preview-$board" "--test-$test"
    done
done
printf 'Host controls/policy suite passed; binaries: %s\n' "$out"
