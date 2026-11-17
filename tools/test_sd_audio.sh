#!/bin/sh
# Real host filesystem + worker/PCM ring tests. No SD hardware or IDF required.
set -eu
cd "$(dirname "$0")/.."
: "${TMPDIR:?Set TMPDIR to scratch}"
out="$TMPDIR/p4-sd-audio-tests"
mkdir -p "$out"
cc=${CC:-cc}
flags='-O2 -Wall -Wextra -Werror'
$cc $flags -Imain tests/wav_audio_test.c main/wav_audio.c -o "$out/wav"
"$out/wav"
python3 - "$out/audio_task_under_test.h" <<'PY'
from pathlib import Path
import sys
s=Path('main/audio.c').read_text()
# Execute the real production task, substituting only its hardware dependencies.
Path(sys.argv[1]).write_text(s[s.index('#define READ_MAX'):s.index('bool audio_init(void)')])
PY
$cc $flags -Wno-unused-parameter -pthread -I"$out" -Itests/sd_audio_host -Imain '-DSDCARD_ROOT="host-card"' \
    tests/sd_audio_worker_test.c tests/audio_route_runtime_test.c main/sd_audio.c main/wav_audio.c -Wl,--wrap=wav_begin,--wrap=wav_open,--wrap=fread,--wrap=fwrite,--wrap=fflush,--wrap=fclose -o "$out/worker"
root=$(mktemp -d "$TMPDIR/p4-sd-audio.XXXXXX")
mkdir "$root/host-card"
(cd "$root" && "$out/worker")
python3 - "$root/host-card/AUDIO" <<'PY'
import pathlib, struct, sys, wave
p=pathlib.Path(sys.argv[1])/"RX000001.WAV"
raw=p.read_bytes()
assert len(raw)==9644 and struct.unpack_from("<I",raw,4)[0]==len(raw)-8
assert struct.unpack_from("<I",raw,40)[0]==9600
with wave.open(str(p),"rb") as w:
    assert (w.getframerate(),w.getnchannels(),w.getsampwidth(),w.getnframes())==(48000,1,2,4800)
    assert struct.unpack("<480h",w.readframes(480))==tuple(range(-240,240))
print("Independent Python wave reader: 9644 bytes, RIFF/data sizes, 48k mono PCM16, exact captured samples passed")
PY
python3 tests/sd_audio_route_test.py
python3 tests/fatfs_config_test.py
printf 'Host SD audio tests passed; real WAV files retained at %s/host-card/AUDIO\n' "$root"
