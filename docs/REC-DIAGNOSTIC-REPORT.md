# REC investigation (baseline e8b1e89)

## Outcome and limits

The reported physical-board failure is **not reproduced or declared repaired**.
No UART, board access, flashing, RF capture/TX, or network was used. The repair in
this change is to the proven silent-failure diagnostic path: REC failures now
latch ERR on REC and INFO on the adjacent PLAY button. Tap INFO to read the
stage-specific error on the recordings page. Entering INFO does not automatically
REFRESH over the evidence. BACK returns to live RX; retry REC there. Explicit
REFRESH may replace the status with a directory listing.

## Ranked hypotheses and evidence

1. **Historical 8.3 configuration vs metadata filenames**: real ESP-IDF 5.5.5
   FatFs, RAM-backed sectors, using each existing build's generated sdkconfig,
   accepts 120 metadata names per variant. Tests cover offline and UTC clocks,
   embedded frequency dots, all six bandwidth indexes, frequency extremes,
   exclusive create collision, write, stat, delete and legacy directory listing.
   A negative-control build of the same engine with LFN disabled rejects the
   first metadata name with FR_INVALID_NAME (mapped to EINVAL by target VFS).
   Thus the old naming/configuration mismatch is proven, but it is **not present
   in these four generated builds**: heap LFN=3, maximum 255. Nothing establishes
   the configuration/hash of the image currently running on the user's board.
2. **Metadata/delete allowlist rejects normal names**: normal metadata passes
   wav_safe_name and wav_recording_name and reaches actual worker creation;
   the new FatFs test also passes both predicates. Colon/wildcards are not emitted
   by the formatter. Multiple dots are legal with LFN. The target rejects a
   wildcard; the app rejects unsafe names before enqueueing. Newlib/target errno
   values are not assumed to equal Linux numeric values: diagnostics print the
   actual numeric errno plus strerror.
3. **Queue/STOP/page ownership cancels REC**: existing actual-worker tests cover
   queue saturation, STOP during mount/header, epoch cancellation, PLAY entry and
   exit, and refusal to record substituted playback. Normal queued starts run;
   playback page exits release exclusivity. There is no observed normal-runtime
   stuck page flag in the tested seams.
4. **Audio task pause or missing PCM**: actual production audio-task host seam
   exercises live demod PCM -> sd_audio_exchange -> worker, saved samples, output
   substitution, and preserved volume/mute. It is not evidence of live hardware
   SDR samples, codec readiness, or task scheduling. A board recording remaining
   at header-only length would still require hardware/runtime observation.
5. **Mount/card/create failure hidden by main screen**: reproduced. Previously
   REC ignored unavailable codec, filename generation and rejected-request
   failures. Asynchronous mount/create errors were only present in an unshown
   snapshot; main polling redrew only the idle REC glyph. The red regression
   fails on the old generic mount diagnostic. The new path distinguishes codec,
   filename rejection, worker allocation/init, queue full, page ownership, mount,
   mkdir, open/create, fdopen and header-write stages. Filesystem operations
   capture errno before cleanup. Overflow/finalization failures latch ERR too.

## End-to-end source trace

* `view_button_at` identifies VIEW_RECORD; header actions are deliberately not
  hold-repeated. `ui.c` freezes sdr_freq/bandwidth/time metadata on the accepted
  REC action, requests STOP if record_requested is already true, otherwise
  queues SD_AUDIO_RECORD without opening another page.
* `sd_audio_request` validates names, rejects playback-page ownership, stores
  the current command epoch and publishes pending state. STOP is independent
  of queue capacity and invalidates older starts. The worker owns all SD I/O.
* `sdcard_mount` reference-counts the mount under its mutex. No automatic format
  is allowed; max_files=2. Worker creates `/AUDIO`, uses O_CREAT|O_EXCL and
  collision suffixes, fdopen and wav_begin, then atomically checks cancellation
  before publishing RECORDING. Target VFS maps these flags to FA_CREATE_NEW.
* Audio pause is observed before demod/routing. Main REC itself does not pause
  audio. Normal live output demodulates and calls sd_audio_exchange; recording
  copies those mono PCM16 samples into the bounded ring. Playback-page ownership
  substitutes output and never records its own playback.
* Worker drains 512-sample blocks, writes WAV payload, drains on STOP and updates
  header/fsync/closes before releasing mount. Buffer/card failures are explicit.
  List accepts safe WAV names; DELETE additionally requires recording-shaped
  names and regular-file stat, disallows an active/pending recording, and closes
  playback before unlink. No naming/delete rules were weakened.

## Reproduction / verification

```
bash tools/test_sd_audio.sh
IDF_PATH=/home/pterodyne/esp/esp-idf bash tools/test_fatfs_recording.sh
bash tools/test_controls.sh
bash tools/test_rf.sh
bash tools/test_hackrf.sh
```

The SD test includes actual worker failures for missing card and EINVAL on
exclusive creation. It checks the stage/error latch, no pending recording after
failure, preservation across INFO page transitions, normal PCM recordings,
collision names and delete flow. Python independently parses the saved WAV and
compares its PCM samples. Real FatFs uses the installed IDF source/configuration;
only block storage, allocation and single-thread synchronization are host hooks.
It is not a test of SDMMC electrical behavior or the full newlib VFS/stdio stack.

## Requested board observations (not performed)

Record the running image/backend/revision/hash if available. With live audio,
tap REC once: note whether it stays STOP, returns to REC, or shows ERR/INFO.
If ERR, tap INFO **before REFRESH**, and report the exact full status text.
For `REC create WAV errno=...`, the stage and numeric/text errno distinguish
invalid name/config from full/protected/damaged card. Mount failure is deliberately
reported as mount-stage uncertainty (card seating, FAT format, initialization),
not incorrectly diagnosed as a filename failure. If STOP remains but the WAV
contains no PCM after stopping, filename creation succeeded and the unresolved
question is the hardware PCM/task/ring path, not LFN. No on-board recording
success should be claimed from these host tests or cross-builds.
