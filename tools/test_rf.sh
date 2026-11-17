#!/bin/sh
# Real vendored RF paths with simulated transport; no hardware or IDF needed.
set -eu
cd "$(dirname "$0")/.."
: "${TMPDIR:?Set TMPDIR to a writable scratch directory}"
out="$TMPDIR/p4-rf-tests"
mkdir -p "$out"
flags='-O2 -ffunction-sections -fdata-sections -Icomponents/rtlsdr/blog/include -Icomponents/rtlsdr/shim'
${CC:-cc} $flags tests/r82xx_band_retry_test.c -Wl,--gc-sections -o "$out/r82xx-band-retry"
"$out/r82xx-band-retry"
${CC:-cc} $flags tests/rtlsdr_rf_io_test.c -Wl,--gc-sections -Wl,--wrap=libusb_control_transfer -o "$out/rtlsdr-rf-io"
"$out/rtlsdr-rf-io"
