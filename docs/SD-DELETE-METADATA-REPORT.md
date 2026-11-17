# Recording deletion and compact metadata — host verification

## Delivered behavior

- Browser DELETE requires a separate YES DELETE; CANCEL/BACK do not issue deletion. Selected and confirmed full basenames are frozen independently of painted text and changing list indexes.
- Confirmed deletion silences playback immediately and closes its stream on the SD worker before unlinking. The playback page continues owning live-RF suppression through confirmation, STOP, deletion success and errors. No codec mute/volume settings are changed.
- Only regular files in `/sd/AUDIO` with recognized recording basenames are deletable: legacy `RXnnnnnn.WAV` and compact metadata names. Traversal/arbitrary imported names are rejected. Host symlinks are rejected; target FAT VFS does not have symlinks or `lstat`, so the target uses `stat`.
- Main-header red REC/stop-square and PLAY triangle, RF controls gate, WAV payload cap and atomic STOP cancellation remain intact.
- REC freezes actual `sdr_freq()`, mode and `time(NULL)` at request time. Exact integer-Hz frequency is represented as trimmed decimal MHz, or decimal kHz when strictly shorter; ties use MHz. File creation remains `O_EXCL`, never overwriting prior recordings.

## Tested names and clock policy

- `261116-220530_99.1M_FM.wav`
- `261116-220530_99.1M_FM_1.wav` (same-second collision)
- `261116-220530_1M_AM.wav` (shortest-frequency regression)
- `unsynced_7.1001M_USB_1.wav`
- `unsynced_100.101k_LSB_1.wav`, then `_2.wav` on collision
- `unsynced_100M_NFM_3.wav`

Mode mapping follows actual filter indexes: FM, NFM, NFM, AM, USB, LSB. Sub-kHz precision is retained without floating-point formatting.

No application SNTP initialization, wall-clock setter or external RTC reader was found. The code consumes the existing system UTC clock only, accepts representable timestamps in 2024–2099 and explicitly falls back to `unsynced` for boot epoch, negative/error or out-of-range clocks. This is system-clock plausibility validation, not an independent synchronization guarantee. No network clock configuration or system-time change was made.

## Execution evidence

- Test-first failures retained in `metadata-red.log`, `delete-red.log`, `delete-ui-red.log`, `metadata-snapshot-red.log`, `metadata-unsynced-red.log`, `metadata-short-red.log`, `delete-row-red.log`, `delete-stack-red.log`, `delete-selection-red.log`.
- `sh tools/test_sd_audio.sh`: PASS (`delete-sd.log`), then three additional complete passes (`delete-repeat.log`). Executes production worker against actual host files and production audio-task body via hardware seams. Includes active-playback actual-file deletion, cancellation during delete mount, noSD, symlink rejection, error handling, copied metadata request identity, synchronized/unsynchronized collisions, all mode encodings and invalid clocks. Existing WAV cap, overflow, short writes, STOP preparation/full-queue cancellation, EOF, audio routing and mount-balance tests pass. Independent Python wave reader validates the retained 9644-byte PCM fixture.
- `sh tools/test_controls.sh`: PASS (`delete-controls.log`), including confirmation/cancel, full 63-character identity under display clipping, filename inter-row pixel bounds, disabled recording-time deletion, all five themes/four rotations and existing touch/wake controls.
- `sh tools/test_rf.sh`: PASS (`delete-rf.log`).
- ASan/UBSan preview execution: SD page, quick-header and screen controls PASS; leak checks disabled for process-lifetime preview allocations.
- `git diff --check` and staged diff whitespace check: PASS. Existing dirty work was retained; no commit, installation, flashing or network-git operation was performed.

The first target build caught the absence of ESP-IDF `lstat`; the final build uses the explicit FAT/host distinction above. Final builds use the existing `/home/pterodyne/esp/esp-idf/export.sh` and `tools/build_waveshare43.sh`, with logs in `delete-export.log` and `delete-build.log`.

## Final-source artifacts

Both variants contain the same feature set. Flash offset for merged full images is `0x0`; full-image writes can reset NVS/settings. Match the actual board silicon revision. These files are generated locally, not published releases.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `dist/p4-sdr-waveshare43-rev1.bin` | 1080448 | `634a6555ab576265d13e96b1970de5e18a2886f52e1ece1ca7a2dd407cf41c6a` |
| `dist/p4-sdr-waveshare43-rev3.bin` | 1083248 | `b6b2c12809a071ad76e51c02704267369537a36848b91d32587945059878910b` |

`(cd dist && sha256sum -c SHA256SUMS-waveshare43)` passes. Every `flasher_args.json` constituent was byte-compared against the merged image at its configured offset: bootloader `0x2000`, partitions `0x8000`, application `0x10000`. Full constituent hashes are in `delete-artifacts.log`. Both configs specify 32 MB flash; minimum silicon revision codes are 100 and 301 respectively.

Final synthetic portrait/landscape previews are in `docs/previews/sd-delete/`. The portrait delete confirmation and landscape filename rows were visually inspected after correcting title truncation and filename-row overflow. They are host renders, not RF/card measurements.

## Remaining hardware/review limitations

No board was flashed and no serial/USB hardware was opened. Physical SD-card deletion/recovery, codec playback/live recovery, capacitive touch/wake, real RF, RTC synchronization and power-loss behavior still require board verification. Host tests and cross-builds do not establish these. An independent code review is still required before recommending the build for deployment.
