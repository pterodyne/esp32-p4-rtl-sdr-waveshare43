#!/bin/sh
# Produce full-flash images for both ESP32-P4 silicon revisions on the
# ESP32-P4-WIFI6-Touch-LCD-4.3 board. Requires ESP-IDF v5.5 exported.
set -eu
cd "$(dirname "$0")/.."
mkdir -p dist
backend="${SDR_BACKEND:-rtl}"
case "$backend" in
    rtl) suffix='' ;;
    hackrf) suffix='-hackrf' ;;
    *) printf '%s\n' 'SDR_BACKEND must be rtl or hackrf' >&2; exit 2 ;;
esac
for rev in rev1 rev3; do
    b="build-waveshare43$suffix-$rev"
    defaults='sdkconfig.defaults;sdkconfig.waveshare43'
    if [ "$backend" = hackrf ]; then defaults="$defaults;sdkconfig.hackrf"; fi
    if [ "$rev" = rev1 ]; then defaults="$defaults;sdkconfig.rev1"; fi
    idf.py -B "$b" -D "SDKCONFIG=$b/sdkconfig" -D "SDKCONFIG_DEFAULTS=$defaults" build
    (cd "$b" && python -m esptool --chip esp32p4 merge_bin \
        -o "../dist/p4-sdr-waveshare43$suffix-$rev.bin" @flash_args)
done
(cd dist && sha256sum "p4-sdr-waveshare43$suffix-rev1.bin" "p4-sdr-waveshare43$suffix-rev3.bin" > "SHA256SUMS-waveshare43$suffix")
