# SD audio UI correction — local verification

## Cause and fix

`audio.c` previously selected the file source only while `sd_audio_playing()`
reported the worker's PLAYING state. The recordings page itself owned no audio
route: idle browsing, mount/header preparation, STOP, EOF and errors therefore
fell back to live demodulated RF. Inspection found substitution, not an intentional
live+WAV mixer, in the actual PLAYING exchange path.

The page now owns a separate live-source gate in `sd_audio.c`, under the same mux
as PCM exchange and worker mode. `audio_task` services this exclusive output even
without live IQ: it writes zeros when idle and WAV PCM during playback. The gate
survives STOP/EOF/errors and is released only on page exit. Header/menu/BACK paths
release ownership; page transitions advance the cancellation epoch and stop the
exchange atomically, preserving the existing post-mount/header publication check.
The gate never changes codec/user mute or volume; deliberately muted or zero-volume
speakers stay muted, including during playback, and there is no exit-time unmute.

Main header now has red-circle REC and right-pointing-triangle PLAY. REC directly
starts live demodulated recording without navigation; pending/active recording
shows a red stop-square and STOP label, and a second tap cancels/stops it. Neither
icon hold-repeats. Entering PLAY stops and finalizes any live recording; page REC
is disabled with a MAIN REC reminder. No playback or page-silence capture is
possible. PRESETS, ZOOM, split STEP/tuning, battery-unavailable field and MENU remain.
The red recording glyph is an explicit safety-color exception to Phosphor green.
WAV caps and `wav_audio.c/.h` were not changed by this correction.

## Executed verification

- RED: `sd-ui-red.log` shows nonzero live PCM on the idle page assertion before
  the gate fix. `sd-ui-controls-red.log` shows the missing separate PLAY hit before
  adding the header icons.
- `sh tools/test_sd_audio.sh`: PASS (`sd-ui-tests.log`). Real worker + host files,
  exact WAV bytes independently read with Python wave, idle/loading/STOP/EOF/error
  silence, WAV sample substitution, page-entry recording finalization,
  page-exit cancellation inside mount/header, prior STOP races/full queue,
  overflow/noSD, clean size cap and existing I/O error tests.
- The SD suite also compiles the actual production audio task body into
  `tests/audio_route_runtime_test.c`, substituting only deterministic hardware
  seams, and executes it against the real worker. It checks speaker-write PCM,
  WAV audibility with user output enabled, idle/STOP/EOF silence, no RF-reader
  dependency on the page, and preservation of prior mute × volume 0/7.
- `sh tools/test_controls.sh`: PASS (`sd-ui-controls.log`), both original and
  Waveshare configurations. New icon hits, circle/square pixels, nonrepeat policy,
  all five themes/four rotations, wake consumption and unchanged readout/control
  pixels; existing presets, zoom, split STEP, footer and other UI regressions pass.
- `sh tools/test_rf.sh`: PASS (`sd-ui-rf.log`); its injected-failure diagnostics
  are expected test stimuli, not hardware observations.
- AddressSanitizer + UndefinedBehaviorSanitizer preview: quick-header and SD-page
  tests PASS (leak detection disabled for the persistent preview buffers).
- Both silicon builds and merges PASS through the installed IDF export and
  `tools/build_waveshare43.sh` (`sd-ui-export.log`, `sd-ui-build.log`).
- All flasher_args.json constituents byte-match their merged offsets, both
  variants; `sd-ui-artifacts.log` records sizes, offsets and constituent hashes.
- `git diff --check`: PASS. Existing staged/unstaged/untracked work preserved;
  no install, commit, network Git operation, flashing or serial access performed.

## Artifacts

Full merged images, intended offset **0x0**, revision-matched only:

- rev3: `dist/p4-sdr-waveshare43-rev3.bin`, 1,066,688 bytes;
  SHA-256 `3be6ea1eed411d08d0473cdc3dc7124865ead7856b45621293eec2da5aff9ac4`.
- rev1: `dist/p4-sdr-waveshare43-rev1.bin`, 1,063,840 bytes;
  SHA-256 `572d1541c8e28455c2de5d0aabbdbdc27fba215a034b25377bc5c8bb3ff8a380`.

These supersede the earlier rev3 image identified as
`bea6a55afa41592b500e6b48baa330708d71f7858fe91c5611f9853417c3b3f5`.
Generated configs are 32 MB flash, rev1 minimum 100 and rev3 minimum 301.
`dist/SHA256SUMS-waveshare43` contains the current image checksums.

`dist/sd-ui-correction-previews/manifest.json` inventories 60 explicitly synthetic
final-source renders (five themes × four rotations × receiver/recording/page).
Each state's image differs within every theme/rotation triple. Representative
portrait receiver, landscape active recording and portrait playback-page renders
were visually inspected: distinct glyphs/labels, no key overlap, and battery/MENU
remain visible. These are not RF/card/battery measurements.

## Hardware limits / next review

No board was flashed or listened to. Physical SD latency/removal behavior, touch,
RF, audible output and power-cycle behavior remain unverified. Already queued
codec/I2S samples may take the existing output-buffer latency to drain at a route
transition; host block tests do not measure that hardware tail. Full-image writes
can erase settings/NVS. Parent review and physical verification are still needed
before presenting this as installed or hardware-confirmed firmware.
