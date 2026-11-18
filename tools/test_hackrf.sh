#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${TMPDIR:?Set TMPDIR to a scratch directory}"
cc -Wall -Wextra -Werror -Icomponents/rtlsdr/shim -Imain tests/hackrf_rx_test.c main/hackrf_rx.c -o "$TMPDIR/hackrf_rx_test"
"$TMPDIR/hackrf_rx_test"
cc -Wall -Wextra -Werror -Icomponents/rtlsdr/shim -Imain tests/hackrf_stream_test.c main/hackrf_stream.c -o "$TMPDIR/hackrf_stream_test"
"$TMPDIR/hackrf_stream_test"
cc -Wall -Wextra -Werror -Icomponents/rtlsdr/shim tests/usb_rx_descriptor_test.c -o "$TMPDIR/usb_rx_descriptor_test"
"$TMPDIR/usb_rx_descriptor_test"
cc -Wall -Wextra -Werror -Itests/hackrf_host -Icomponents/rtlsdr/shim -Imain tests/hackrf_sdr_test.c main/hf_diag.c -o "$TMPDIR/hackrf_sdr_test"
"$TMPDIR/hackrf_sdr_test"
cc -Wall -Wextra -Werror -Itests/stubs -Imain tests/hackrf_gain_store_test.c -o "$TMPDIR/hackrf_gain_store_test"
"$TMPDIR/hackrf_gain_store_test"
python tests/hackrf_ui_test.py
D=components/rpi_display
cc -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -DCONFIG_SDR_WAVESHARE_43 -DCONFIG_SDR_BACKEND_HACKRF -Itools/preview/stub -I"$D/include" -I"$D" -Imain tools/preview/preview.c main/view.c main/panel.c main/hf_diag.c main/adsb.c main/adsb_view.c main/amap.c main/scan_view.c main/sensor_view.c main/spectrum.c "$D/rpi_canvas.c" "$D/rpi_text.c" -lz -lm -o "$TMPDIR/preview-hackrf"
"$TMPDIR/preview-hackrf" --test-hackrf-header
python3 tests/hackrf_gain_immediate_test.py
python3 tests/narrow_zoom_test.py
python3 tests/spectrum_columns_test.py
python3 tests/ssb_audio_display_runtime_test.py
python3 tests/dsp_memory_placement_test.py
