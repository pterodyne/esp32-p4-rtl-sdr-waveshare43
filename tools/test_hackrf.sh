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
cc -O2 -Wall -Wextra -Werror -Itests/hackrf_host -Icomponents/rtlsdr/shim -Imain tests/hackrf_sdr_test.c main/hf_diag.c main/hackrf_resample.c -lm -pthread -o "$TMPDIR/hackrf_sdr_test"
"$TMPDIR/hackrf_sdr_test"
cc -Wall -Wextra -Werror -Itests/stubs -Imain tests/hackrf_gain_store_test.c -o "$TMPDIR/hackrf_gain_store_test"
"$TMPDIR/hackrf_gain_store_test"
cc -O2 -Wall -Wextra -Werror -Imain tests/hackrf_resample_test.c main/hackrf_resample.c -lm -o "$TMPDIR/hackrf_resample_test"
"$TMPDIR/hackrf_resample_test"
cc -O2 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32P4 -Imain tests/hackrf_kernel_gate_test.c main/hackrf_resample.c -lm -o "$TMPDIR/hackrf_kernel_gate_test"
"$TMPDIR/hackrf_kernel_gate_test"
python tests/hackrf_pie_test.py
python tests/hackrf_ui_test.py
python tests/normal_rate_recovery_test.py
python tests/rtl_rate_failure_test.py
D=components/rpi_display
cc -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -DCONFIG_SDR_WAVESHARE_43 -DCONFIG_SDR_BACKEND_HACKRF -Itools/preview/stub -I"$D/include" -I"$D" -Imain tools/preview/preview.c main/view.c main/panel.c main/hf_diag.c main/adsb.c main/adsb_view.c main/amap.c main/scan_view.c main/sensor_view.c main/spectrum.c "$D/rpi_canvas.c" "$D/rpi_text.c" -lz -lm -o "$TMPDIR/preview-hackrf"
"$TMPDIR/preview-hackrf" --test-hackrf-header
