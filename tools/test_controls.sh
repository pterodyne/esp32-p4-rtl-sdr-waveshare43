#!/bin/sh
# Host controls/policy regression suite; no IDF or hardware required.
set -eu
cd "$(dirname "$0")/.."
: "${TMPDIR:?Set TMPDIR to a writable scratch directory}"
out="$TMPDIR/p4-control-tests"
mkdir -p "$out"
flags='-O2 -Wall -Wextra -Werror -Itests/stubs -Itools/preview/stub -Imain -Icomponents/rpi_display/include -Icomponents/rtlsdr/blog/include'
for name in audio_mute audio_pause_gate audio_save_pending bias_state sensor_event_age settings_save_due ui_audio_policy waveshare_geometry screen_idle screen_timeout_store display_power; do
    ${CC:-cc} $flags "tests/${name}_test.c" -o "$out/$name"
    "$out/$name"
done
for name in tune_step theme_store audio_settings_store rf_policy weather_wh2 tpms_steelmate; do
    ${CC:-cc} $flags "tests/${name}_test.c" "main/${name}.c" -lm -o "$out/$name"
    "$out/$name"
done
python3 tests/ui_audio_recovery_test.py
python3 tests/ui_controls_contract_test.py
D=components/rpi_display
sources="tools/preview/preview.c main/view.c main/panel.c main/adsb.c main/adsb_view.c main/amap.c main/scan_view.c main/sensor_view.c main/spectrum.c $D/rpi_canvas.c $D/rpi_text.c"
for board in original waveshare43; do
    define=''
    if [ "$board" = waveshare43 ]; then define='-DCONFIG_SDR_WAVESHARE_43'; fi
    ${CC:-cc} -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
        $define -Itools/preview/stub -I"$D/include" -I"$D" -Imain $sources -lz -lm -o "$out/preview-$board"
    for test in audio bands theme phosphor footer-recovery rf ism sensors screen; do
        "$out/preview-$board" "--test-$test"
    done
done
printf 'Host controls/policy suite passed; binaries: %s\n' "$out"
