#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Export ESP-IDF 5.5.5 first}"
: "${TMPDIR:?Set scratch TMPDIR}"
out="$TMPDIR/p4-fatfs-recording"
mkdir -p "$out/freertos"
printf '#define portTICK_PERIOD_MS 1\n' > "$out/freertos/FreeRTOS.h"
printf '\n' > "$out/freertos/semphr.h"
for build in build-waveshare43-rev1 build-waveshare43-rev3 build-waveshare43-hackrf-rev1 build-waveshare43-hackrf-rev3; do
    cc -O2 -Wall -Wextra -Werror -I"$build/config" -I"$out" -I"$IDF_PATH/components/fatfs/src" -Imain \
       tests/fatfs_recording_test.c main/wav_audio.c "$IDF_PATH/components/fatfs/src/ff.c" "$IDF_PATH/components/fatfs/src/ffunicode.c" -o "$out/test"
    printf '%s: ' "$build"
    "$out/test"
done
# Negative control: recreate the old 8.3-only configuration using the SAME
# FatFs engine. The first metadata name must fail with FR_INVALID_NAME (6).
printf '#include "sdkconfig.h"\n#undef CONFIG_FATFS_LFN_HEAP\n#undef CONFIG_FATFS_LFN_STACK\n' > "$out/no_lfn.h"
cc -O2 -Wall -Wextra -Werror -Ibuild-waveshare43-rev1/config -I"$out" -I"$IDF_PATH/components/fatfs/src" -Imain \
   -include "$out/no_lfn.h" tests/fatfs_recording_test.c main/wav_audio.c "$IDF_PATH/components/fatfs/src/ff.c" "$IDF_PATH/components/fatfs/src/ffunicode.c" -o "$out/no_lfn"
if "$out/no_lfn" > "$out/no_lfn.log" 2>&1; then
    printf 'ERROR: 8.3 negative control unexpectedly accepted metadata\n' >&2
    exit 1
fi
python3 - "$out/no_lfn.log" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
assert ': 6 (LFN=0 max=255)' in s, s
print('8.3 negative control: metadata creation fails FR_INVALID_NAME, reproducing the historical configuration failure')
PY
