# SD listening-audio recording and playback

**Not raw IQ:** REC saves the receiver's demodulated/squelched listening PCM at
48,000 Hz, signed little-endian 16-bit mono, as a canonical WAV. Capture remains
2.4 MS/s IQ internally; that stream is **not** written to the card. Volume and
speaker mute are output controls, so they do not alter the captured PCM.

## Controls

- Header order: TOUCH, AUTO RESET, GAIN, ZOOM, PRESETS, **red-circle REC**, **right-triangle PLAY**; battery is left of
  MENU. Frequency has a dedicated second row. Portrait keeps battery/MENU at
  the right of that second row. Split STEP and its tuning arrows remain below.
- **PRESETS** directly opens the existing **BAND** preset list (not saved
  channel memories; those remain MENU → SCAN → MEMORY).
- **PLAY** opens **SD AUDIO WAV**. REFRESH reads `/sd/AUDIO`; select a filename,
  then PLAY. PREV/NEXT browse four files per screen and subsequent 64-file
  directory batches, without imposing a 64-file total limit. Each batch is
  sorted by filename; ordering across batches follows the card directory.
- **REC on the main receiver header** starts recording directly, without opening
  a page. It becomes a **red stop-square with STOP label** during preparation and
  capture; tapping it again cancels preparation or stops recording. Neither key
  repeats on hold. The red safety glyph is the sole Phosphor monochrome exception.
  REC snapshots current receiver frequency, actual demodulator mode and the
  system UTC clock **when requested**, before queuing any card work. Filenames
  look like `261116-220530_99.1M_FM.wav`. Fractional MHz is exact to integer Hz,
  without trailing zeros; decimal kHz is used only when shorter (MHz wins ties).
  Modes are FM, NFM (both widths), AM, USB or LSB, not filter bandwidth labels.
  Existing files are never overwritten (`O_EXCL`); same-second collisions append
  `_1`, `_2`, etc. **STOP** drains
  accepted PCM, writes the final RIFF/data sizes, flushes/syncs/closes and
  releases the card mount. Wait for **WAV saved** before removing power/card.
- Entering PLAY immediately stops/finalizes any current live recording. Wait for
  **WAV saved**, then REFRESH to see its filename. Recording is unavailable on
  this page, so neither page silence nor playback can be accidentally recorded.
  **DELETE** opens a confirmation naming the selected file; **CANCEL** or BACK
  cancels without deleting, **YES DELETE** queues deletion. The full selected
  basename and confirmation identity are frozen separately from display text
  and list indexes. Confirmed deletion first silences/closes playback on the
  filesystem worker, then deletes only a regular file with a known recording
  basename inside `/sd/AUDIO` (old `RXnnnnnn.WAV` or compact metadata format).
  Generic imported WAVs remain playable but cannot be deleted by this control.
  No SD, missing files, symlinks (host) and delete errors leave files untouched
  and report failure. The ESP-IDF FAT filesystem has no symlinks. DELETE remains
  usable during playback; PLAY/REFRESH are disabled while active; STOP
  remains accessible. BACK, MENU or another shortcut leaving the page cancels
  playback, including pending mount/open operations.
- Playback substitutes file PCM for live RX at the existing codec sample rate;
  it does not mix RX into playback and cannot record its own output. The **page**
  owns live-source suppression independently of the worker state: idle, loading,
  STOP, EOF and errors all remain live-silent until exit. The codec is **not**
  globally muted for this suppression, so WAV samples reach it unchanged. Only
  page exit restores live RX, with volume, user mute and tuning unchanged.
  Playback still respects the user's existing speaker mute/zero-volume settings;
  entering or leaving PLAY never un-mutes a deliberately muted speaker.
- Supported imports: canonical 44-byte-header PCM WAV, **48 kHz / mono / 16-bit**.
  Other rates, stereo, compressed formats, extended headers, malformed/truncated
  files and unsafe/path-like names are rejected. Names are at most 63 characters
  using letters, digits, `_`, `-`, `.`, and `.WAV`/`.wav`. Display shortening never
  changes the stored basename used for PLAY or DELETE.

### Clock policy

The application has no existing SNTP setup, wall-clock setter or external RTC
read path. This change consumes `time(NULL)` only; it does not configure a
network, set a timezone, or alter system time. UTC values representable by
`time_t` in 2024–2099 are accepted; boot epoch, negative/error values and dates
outside this supported interval are invalid. This is system-clock plausibility
validation, not independent proof that a clock has been synchronized. With the
current unsynchronized boot clock, names explicitly use an indexed fallback,
e.g. `unsynced_7.1001M_USB_1.wav` or `unsynced_100.101k_LSB_2.wav`; no date is
invented. The first unused index is found using exclusive creation. Old
sequential recordings remain listed, playable and deletable.

## Architecture and errors

`sd_audio.c` owns all file access on one low-priority core-0 worker, with a
128 KiB PCM ring (~1.36 seconds). The real-time audio owner only performs bounded
copies; neither the UI nor RX/audio tasks wait for SD filesystem operations.
Mount references use the existing `sdcard` manager, shared with `sdlog`; its lazy
mutex initialization is now protected across tasks. WAV audio does not reuse or
change decoder CSV logs. Card formatting on mount failure remains disabled.

Missing/mount-failed cards, file create/read/write/flush/sync/close errors and
buffer overflow are reported on the page. Overflow stops recording visibly
rather than silently splicing omitted audio. Finalization salvages complete
samples on a short write where the filesystem still accepts header/truncate
updates. The worker always closes and releases its mount before another
operation. Writer and reader share an explicit signed-32-bit stdio limit:
2,147,483,602 payload bytes (1,073,741,801 complete samples), plus the 44-byte
header = 2,147,483,646 total file bytes, below `INT32_MAX`. At 48,000 Hz mono
PCM16 this is **6 h 12 min 49.620854 s** (22,369.620854167 seconds), calculated
as `WAV_AUDIO_MAX_BYTES / (WAV_AUDIO_RATE * 2)`. Reaching it writes only the
remaining complete samples, finalizes cleanly and reports **WAV saved (size
limit reached)**; file I/O errors still report failure. No automatic segmentation
is implemented. This deliberately stays below FAT32's larger file limit because
the target reader uses signed-32-bit `fseek`/`ftell`, not a verified large-file API.
STOP also invalidates starts already preparing inside mount/header/open: the
worker rechecks cancellation atomically with active-state publication, then
closes/unmounts a cancelled start without enabling PCM exchange.

**Power-loss/card-removal caveat:** normal STOP/error finalization is tested;
there is no power-fail journal or guarantee that an abruptly removed/full/failed
card accepts its final header. Sudden power loss may leave an incomplete WAV;
malformed files are refused on playback. Never remove an active card. Physical
unmount-error propagation is limited by the existing void `sdcard_unmount()` API.

## Battery evidence

This worktree contains no board battery-voltage measurement API, ADC setup or
calibration path. The only `battery_mv` code is unrelated received TPMS telemetry.
The Waveshare BSP has no battery measurement API either. Consequently the new
field explicitly shows **BAT --.--V**, not an invented voltage, percentage or
charge state. No unverified experimental battery/ADC firmware was imported.

## Verification versus hardware

Run `sh tools/test_sd_audio.sh` and `tools/test_controls.sh` with TMPDIR pointing
at scratch. Host tests execute the real WAV codec and worker against actual
host files using a pthread FreeRTOS shim; an independent Python `wave` reader
checks exact size, format and samples. Additional tests verify actual-file
active-playback deletion, noSD/delete failures, STOP inside delete mount, host
symlink rejection, metadata request snapshots, synced/unsynced collisions,
exact frequency encoding, all modes and invalid clocks. They cover exclusive naming, invalid
names/formats, empty WAVs, signed-32-bit size-boundary arithmetic without huge
allocations, worker clean-stop at the size cap (injected byte counter), odd
short-write salvage, `/dev/full`, injected payload read/write/flush/close failures,
STOP during mount and WAV-header preparation for both REC and PLAY (plus full
queue cancellation), no SD, overflow, mount balance, browser batches beyond 64 files, playback EOF,
RX restoration and no simultaneous playback recording. An additional host harness
executes the actual `audio.c` task body with deterministic codec/IQ seams and the
real worker: idle/STOP suppression, audible WAV PCM, live restore and all prior
mute/volume-zero/volume-seven combinations. Controls tests exercise main REC/STOP
glyphs, both header hits, select/PLAY/STOP, DELETE confirmation/cancel/frozen
63-character identity, filename-row pixel bounds, disabled DELETE while recording,
BACK and BAND reuse in all five looks
and all four rotations, alongside existing touch wake-consumption regressions.

`dist/ui-sd-previews/` contains explicitly synthetic host previews. They are not
RF, battery or SD measurements. Both silicon variants are built with the existing
`tools/build_waveshare43.sh` using the exported installed ESP-IDF. Full images
are `dist/p4-sdr-waveshare43-rev{1,3}.bin`, flash offset **0x0**; merged image
writes can erase NVS/settings. Match the board silicon revision. Do not use an
app-only flash based on these host tests.

**No board was flashed, no serial/USB device was opened, and physical SD,
audio, touch, RF and power-cycle behavior have not been verified for this change.**
