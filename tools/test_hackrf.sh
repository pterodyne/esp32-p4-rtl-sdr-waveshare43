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
python tests/hackrf_ui_test.py
