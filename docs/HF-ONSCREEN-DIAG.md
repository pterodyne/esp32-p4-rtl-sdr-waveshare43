# Local HF on-screen diagnostic (not an RF fix)

This is a **local, unflashed** build. It includes the existing uncommitted R82xx failed-switch retry and strict 28.8 MHz boundary fixes; it does not change RF policy, assume V4 identity, drive additional pins, or repair the known unlocked-PLL return-code bug. The user's working rev3 firmware was a full image; earlier **app-only updates produced a blank display**. Do not use the app `.bin` or `idf.py flash` for this trial.

## Inspect on the board (no UART or USB-A dongle connector required)

1. With the SDR connected as already hardwired, tune a VHF station/preset, e.g. **MENU → BAND → FM (98 MHz)**. Return to the receiver, then **MENU → RF INPUT → HF DIAG**. Photograph the USB identity and VHF lines. **< BACK** returns to RF INPUT; another **< BACK** returns to Controls; **DONE** returns to the radio.
2. Tune **MENU → BAND → SW 9.65 (9.65 MHz)** or AM 1000, return to radio, then open **MENU → RF INPUT → HF DIAG** again. Photograph the HF and VHF lines together.
3. Tune FM again and reopen HF DIAG; photograph both lines to capture the **HF→VHF** direction. A same-band tune can correctly show `GPIO5 not reached / unchanged` because the driver caches a completed input switch. The diagnostic page is an on-demand snapshot, not a live waterfall overlay. If the dongle disconnects/reopens, previous per-band records are deliberately cleared.

`USB M/P` must read exactly `RTLSDRBlog` / `Blog V4` to show `Exact Blog V4: YES`; `Blog V4L`, missing strings, and an R828D tuner alone do not count. `req` is the requested center Hz (the regular frequency readout does **not** prove tuning). `center rc` is the *actual return* from `rtlsdr_set_center_freq` (0 is API success only). `PLL LOCK/UNLOCK` is the tuner lock-status readback; **this driver's unlocked PLL can return center rc=0**, so do not call that successful. `GPIO5 LOW/HIGH rc=...` reports the requested switch output level and USB command return; it is **not** a measured pin voltage or RF path. `not reached / unchanged` is distinct from a successful switch command. Neither host tests nor these messages prove HF reception, antenna behavior, an RF signal path, or board performance.

## Flash and backup: rev3 only for the user's rev3 board

The generated **merged full flash** image is `dist/p4-sdr-waveshare43-rev3.bin` at **offset `0x0`**. Use it only after confirming the board's silicon revision is rev3 (minimum full revision 301). Rev1 builds are separate and must not be substituted. Full-image flashing fills the image gaps, including the NVS region: **assume saved presets/settings/audio/rotation will reset**. Have a way back to the known working full rev3 image first. This is not a board flash/readback performed by the assistant.

From this worktree in an ESP-IDF 5.5 exported shell, using the board's **programming/serial port** (replace `PORT`):

```sh
# Optional but strongly recommended before changing anything: entire 32 MB flash backup.
python -m esptool --chip esp32p4 --port PORT read_flash 0x0 0x2000000 known-working-rev3-full-backup.bin
# Flash ONLY the merged full image at 0x0; do not use the app at 0x10000.
python -m esptool --chip esp32p4 --port PORT --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 32MB 0x0 dist/p4-sdr-waveshare43-rev3.bin
# Optional readback of the written image region; compare bytes (readback may require reset/boot mode).
python -m esptool --chip esp32p4 --port PORT read_flash 0x0 0x1004a0 diagnostic-rev3-readback.bin
sha256sum dist/p4-sdr-waveshare43-rev3.bin diagnostic-rev3-readback.bin
```

The byte count `0x1004a0` is this build's rev3 merged-image size; if the image changes, use its new exact byte size for readback. A full-flash backup contains prior settings and device data—store it privately. Readback hash equality verifies the written bytes, not display startup or RF reception. If the display is blank, stop and restore the previously working **full** image at `0x0`; do not attempt app-only repair or claim the diagnostic booted. Do not alter the hardwired SDR connection merely to collect serial logs.

## Local verification

Run `sh tools/test_rf.sh` and `sh tools/test_controls.sh` for driver, diagnostic-state, source-route, and simulated-panel tests (landscape/portrait). Run `sh tools/build_waveshare43.sh` in an exported IDF shell to build/merge both revisions, and `(cd dist && sha256sum -c SHA256SUMS-waveshare43)` for checksums. These are host/build checks, not on-board observations.
