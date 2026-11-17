# Top-bar MENU and time investigation

Implemented: Waveshare MENU/settings now shares the first control row in portrait as well as landscape. TOUCH, AUTO, GAIN, ZOOM, PRESETS, REC and PLAY retain their existing bounds/actions. Battery stays explicitly unavailable (`BAT --.--V`) in its prior position, independently of MENU. Frequency remains on the separate readout row. No recording, deletion, playback ownership or RF policy code changed.

Verification: red test failed at the new portrait top-row MENU assertion before implementation. `tools/test_controls.sh`, `tools/test_sd_audio.sh`, `tools/test_rf.sh`, both revision builds and `git diff --check` pass. Quick-header coverage exercises all themes and four rotations, readout repaint isolation, exact touch mapping and wake/dropout consumption. Updated earlier menu-position assertions to the first row. Merged bootloader/partition/application bytes match every flasher_args.json offset. This is host/build verification, not physical panel/touch/SD/RF evidence. No flashing, serial opening, package installation or network git operation performed.

## NTP prerequisites — not implemented

`main/radio_scan.c` initializes the C6 Wi-Fi driver only for foreground active AP scans. Its handler only consumes WIFI_EVENT_SCAN_DONE, storage is RAM, and leaving the scanner stops/deinitializes Wi-Fi. There is no application station configuration/connection, credential provisioning, IP acquisition lifecycle, esp-netif setup, SNTP initialization, RTC reader or wall-clock setter. A scan is not an Internet connection. Starting NTP from this scan-only owner would not establish connectivity and would conflict with its teardown.

Existing recording metadata snapshots `time(NULL)` at request time. Its 2024–2099 range check is plausibility only, NOT proof of synchronization; boot epoch uses explicit unsynced names. This follow-up intentionally does not claim NTP support or tighten the metadata API without an actual trusted time source. A future implementation needs an approved station/provisioning lifecycle (without inventing credentials), coordination with scan ownership, async SNTP started only after IP acquisition, a sync callback-backed validity state, honest offline/not-yet-synchronized reporting, and recording snapshots containing explicit validity plus UTC. Do not block UI waiting for SNTP or label plausible host/system time synchronized.

## “Pulldown menu” — clarification needed

Existing MENU opens the existing settings panel; PRESETS is an independent quick route and settings retain their existing subpages. No existing pulldown/dropdown concept was found in application source/docs. Recommend interpreting the clear request as moving the existing MENU access to the top bar, as implemented. Ask whether “pulldown” means a visual dropdown replacing that panel, a swipe-down gesture, or simply the existing MENU before changing navigation.

Generated local full images (offset 0x0; can reset NVS/settings):

- rev1 `dist/p4-sdr-waveshare43-rev1.bin`: SHA256 `952e190cfec6f71bb76273c65c9c3a8193c7ca61069dcfb29786befb1f692fb3`
- rev3 `dist/p4-sdr-waveshare43-rev3.bin`: SHA256 `c425d34707809624b7ccbb0d85f81ac886bbec7aaae701441c95e3256c6c480b`

Known user-working rev3 image remains identified by supplied hash `b6b2c12809a071ad76e51c02704267369537a36848b91d32587945059878910b`; these new builds have not been installed or hardware-confirmed. Existing dirty changes were preserved; generated images include that inherited worktree state, not just this UI delta.
