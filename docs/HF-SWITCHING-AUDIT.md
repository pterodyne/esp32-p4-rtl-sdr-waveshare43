# HF switching audit (local, no hardware changes)

## Scope and evidence limits

Inspected `feat/narrow-433-sensors` at `172a6e4` in the explicitly requested next worktree. It was clean before this investigation. No network git operations, commits, publication, flashing, USB access or hardware alterations were performed. The supplied observations (excellent FM, antenna-sensitive FM/433/915, no identified stations below 28.8 MHz, battery operation, hardwired header USB with dongle USB-A removed) are user evidence, not reproduced here. A separately powered loop need not respond to Bias-T. No antenna-voltage or live RF claim follows from these tests.

Ranked falsifiable possibilities: (1) failed band-switch transaction cached as successful: inject GPIO failure and retry HF; (2) failed USB identity read suppresses V4 path: inspect identity-return handling and obtain actual on-board strings; (3) failed tuner initialization/tuning hidden by success/UI: inject failures at those seams and capture real return codes; (4) electrical/RF-path issue despite correct successful commands: needs board evidence, cannot be resolved by host tests. Initially only (1) was fixed; the user-approved exact-boundary correction below was subsequently added. No hardware identity was forced.

## Verified bug and minimal fix

`components/rtlsdr/blog/src/tuner_r82xx.c` originally assigned `priv->input = band` before V4/V4L input and GPIO5 writes. The later failure exits left that requested band cached. A later tune within the same band skipped the switch block, including GPIO5, despite the preceding failure. This can strand a failed HF switch until another band transition/reinitialization; it is NOT proof that a failure happened on this board.

Current fixed lines 1218–1249 (V4), 1272–1297 (V4L): mark input unknown (`0xff`, outside valid HF/VHF/UHF values) before the sequence, commit the band only after all writes succeed (1248, 1296). Unknown is necessary: simply retaining the previous band can also skip a needed restoration when returning to it after a partially completed switch. No register values, polarity or identity rules were changed by that retry fix. The later boundary-only change is documented below; these initial line references precede its two added comments.

`tests/r82xx_band_retry_test.c` includes the actual tuner C implementation. Transport-facing callbacks simulate successful exact I2C lengths and locked PLL, then inject GPIO5 failure. Before the fix the command aborted at `priv.input != HF` (exit 134), proving erroneous caching. Afterward V4 and V4L both retry the failed HF switch; successful same-band tuning avoids redundant GPIO writes; a failed UHF transition also re-applies HF when returning to the previous band. These tests validate command flow, not reception.

## Identification and USB strings

- `librtlsdr.c:1485–1490`: exact, case-sensitive manufacturer/product match, `RTLSDRBlog` + `Blog V4` (or `Blog V4L` at tuner call sites). R828D detection alone is not V4 identification.
- `libusb_esp.c:237–252`: reads IDF-cached descriptor pointers from `usb_host_device_info`, converts UTF-16 ASCII, returns NOT_FOUND for absent cached descriptors. There is no explicit GET_DESCRIPTOR fallback here.
- `librtlsdr.c:783–821`: descriptor lookup errors propagate, but the individual string-read return values at 802, 809, 816 are ignored; function returns zero with cleared/possibly empty strings. `rtlsdr_open:1594` also does not act on that result.
- Therefore an unavailable manufacturer/product string can silently prevent V4 upconversion and choose generic R828D clock/input handling. This is a concrete error-reporting hole, not evidence it happened. A focused next regression should stub each string-read error and require the USB-string API to propagate it; then preserve valid strings exactly and surface missing identity explicitly, never substitute V4 based solely on tuner type.
- `main/rf_policy.c:7–9` disables direct ADC for missing identity, every R828D, or Blog V4-prefixed product. A disabled ADC choice therefore does NOT establish positive V4 detection.

## R828D setup, frequency path and switches

- `librtlsdr.c:1625–1633`: probes R828D and separately prints V4 detection only on exact string match.
- `1663–1668`: confirmed V4 retains 28.8 MHz tuner crystal; other R828D gets 16 MHz.
- `213–231`: R828D I2C address/chip, crystal, max message length 8; calls `r82xx_init`.
- `tuner_r82xx.c:1166–1175`: V4/V4L requested frequency **below** 28.8 MHz gets +28.8 MHz, then IF is added. Nonidentified hardware does not get that offset.
- `1193`: notch disabled at <=2.2 MHz, 85–112 MHz, 172–242 MHz; enabled elsewhere. No basis found to change this speculatively.
- `1202`: V4 HF selection uses **<=**28.8 MHz, VHF >28.8 and <250 MHz, UHF >=250 MHz. At exactly 28.8 MHz the HF selection and strict `<` upconversion threshold disagree. This was a real boundary inconsistency. The user-approved correction now selects HF strictly below 28.8 MHz in both models, retaining the existing strict upconversion rule. V4 selects VHF at/above 28.8 MHz (below 250 MHz); V4L retains its two-input design and selects Air-In/UHF at/above 28.8 MHz. It does not acquire a separate VHF input.
- V4 HF uses cable2 register 0x06 bit 0x08 and GPIO5 low (`!cable_2_in`); VHF uses cable1 register 0x05 bit 0x40; UHF uses air-input register 0x05 bit 0x20 cleared. V4L uses cable1 for HF, same GPIO5 polarity. Antenna Bias-T is GPIO0 (`librtlsdr.c:2126–2128`), separate from GPIO5 upconverter switching.
- HF tracking filter bypass is reapplied every tune (`1204–1215`, V4L `1258–1269`), outside the band-change cache guard. No speculative bypass change was made.

## Transport and error propagation

- `libusb_esp.c:343–345` subtracts the setup packet from IDF `actual_num_bytes`. This is CORRECT for the installed IDF, not an off-by-eight bug: `/home/pterodyne/esp/esp-idf/components/usb/hcd_dwc.c:2375–2389` includes setup bytes for both no-data and data control transfers; `enum.c:545,585` uses the same subtraction.
- `librtlsdr.c:408–431`, `455–473`, `2078–2091` preserve USB byte counts/errors through bulk I2C callbacks. R82xx `write:274–283`, `read:336–353` rejects short transfers as well as negative errors.
- Checked GPIO helper `librtlsdr.c:2097–2123` requires exactly one byte at every read/write, propagates failure for GPIO5 as well as GPIO0. Existing RF I/O regression passes.
- Other paths still lose errors: probe helper `444–452` ignores read/write status and returns a byte; I2C repeater helper `578–580` is void and discards the demod write result. Legacy register reads `475–489` and `514–530` return data even after failed transfers (local two-byte buffers are not initialized). Those are audit findings, not modified here.
- `r82xx_init:1441–1450` (line numbers shifted by the fix) overwrites initial register-write return status before checking it, and sets `init_done` even if the final sysfreq operation fails. `librtlsdr.c:1700–1707` ignores tuner init failure and returns successful open. Next minimal tests should inject initial/final init failures separately, assert immediate failure/no initialized state, then assert failed open cleans up rather than returning a device.
- `tuner_r82xx.c:564–567` reports PLL unlocked by `has_lock=0` but returns 0; `1185–1187` jumps to the exit without turning rc negative, so `r82xx_set_freq` can report success without lock. Next focused regression: feed unlocked PLL readbacks and require negative tune status, then minimally translate unlocked state to an error at the set-frequency boundary.
- `main/sdr.c:105–106` ignores initial rate/frequency return values. `194–202` caches requested frequency before an unchecked tune, and `247–253` does the same for rate. Thus displayed frequency/rate is not proof of hardware success. Next regression should inject nonzero rate/tune returns at open and retune, require visible/logged failure and no claimed successful hardware state; avoid broad UI/API redesign without tracing every caller.

## Verification

- RED: compiled and ran focused band retry test before production edit; exit 134 at the premature-cache assertion.
- GREEN: `sh tools/test_rf.sh` passes both new band/GPIO retry and existing short-EEPROM/GPIO I/O regressions. Expected `failed=-1` diagnostics are injected failures.
- `sh tools/test_controls.sh` passes all host control/policy and original/Waveshare preview checks.
- `. /home/pterodyne/esp/esp-idf/export.sh; ninja -C build-waveshare43-rev1; ninja -C build-waveshare43-rev3` both pass. App partition remains 36% free in both. These are local builds, not flashed or released.
- App SHA256 rev1: `22ef1e3f36abf3fc9a55b172ac73c3d6dc69afaf53a11edab7722e0584ad415e`; rev3: `921de8d4727e24060bc394ddf092e04aaf2a25dc9e3e83385d8008ccf0ae0cd3`.
- `git diff --check` passes.

## Exact-boundary follow-up (current artifacts)

The user observed abrupt many peaks at 28.800 MHz versus 28.820 MHz. Ranked explanations are the proven exact-point path/LO mismatch, the separate known PLL/error-reporting hole, and an unverified RF/noise/identity issue. Local driver evidence supports retaining its existing strict `<28.8 MHz` upconversion contract and aligning input selection to it; no external reference was consulted. This fixes only the exact endpoint, not all HF reception or the physical origin of peaks.

- RED: expanded actual-driver test ran with exit 1 before either threshold changed. Only the exact 28,800,000-Hz input/GPIO checks failed in both V4 and V4L; below/above and 28.820 MHz passed. LO at the exact point was already approximately 32.370 MHz while the selected input/GPIO still requested HF upconversion.
- GREEN: `sh tools/test_rf.sh` exit 0. Both models pass 28,799,999 / 28,800,000 / 28,800,001 / 28,820,000 Hz and return below the boundary. The test decodes real PLL register programming (2 kHz quantization tolerance), explicitly requires `has_lock`, and checks input cache, GPIO5 polarity and input registers. Existing failed-switch retry tests and short-EEPROM/GPIO I/O tests remain passing.
- Unlocked PLL injection confirms the pre-existing hole: tune returns 0 with `has_lock=0`, skips switching. This is characterized explicitly, not fixed or mistaken for a successful locked tune. Expected injected error diagnostics are not real hardware failures.
- `sh tools/test_controls.sh` exit 0, including original and Waveshare host previews.
- Exported local IDF and ran full `sh tools/build_waveshare43.sh`: exit 0 for both build and merge steps. Generated silicon minimums are 100 and 301 respectively; both use 32 MB flash. App partitions remain 36% free. Build log: `/home/pterodyne/.hermes/profiles/dev/cache/scratch/boundary-build.log`.
- Current merged images, flash offset `0x0`, under `/home/pterodyne/.hermes/profiles/dev/cache/scratch/esp32-p4-next-worktree/dist/`:
  - `p4-sdr-waveshare43-rev1.bin`: SHA256 `c7b30a32316027fb91e1f1f52b9eda264141c79d318f3ec2d7e907d8df591ef4`
  - `p4-sdr-waveshare43-rev3.bin`: SHA256 `52c457608bf7aead317d0945fec56df31f36be7d880e7855d49e910f8f15a0e5`
  - `SHA256SUMS-waveshare43` generated by build script.
- Current app images (same worktree): `build-waveshare43-rev1/p4_sdr.bin` SHA256 `d72715700123605f9f38091d49db065cc6dac685a9e73460ec7bd7bbec38977c`; `build-waveshare43-rev3/p4_sdr.bin` SHA256 `04196d90a8239e0d3985c3d35351a1977a0f41431e1102fe10a618b770ea18da`. These supersede the earlier retry-only hashes above.
- Self-review of production diff confirms only the two boundary expressions/comments were added to the existing retry fix. V4L retains Air-In, generic R828D/identity/PLL error semantics remain unchanged, and the retry fix is preserved. Independent delegation was unavailable; this is self-review, not independent approval. `git diff --check` passes. No commit, push, release or hardware test was performed.

## Worktrees and preservation

Seven registered worktrees were inventoried. Primary `/home/pterodyne/.hermes/profiles/dev/esp32-p4-rtl-sdr-43` at a32c5a1 has existing modified `main/view.c`, `main/view.h`, `tools/preview/preview.c` and untracked `ROADMAP.md`, `main/tune_step.c`, `main/tune_step.h`, `tests/tune_step_test.c`: untouched. Other registered trees: battery-probe dd41f79, gallery-landing b2d0145, roadmap-worktree e676acd, theme-worktree 55e8966, ui-retain-range 0573f1d; these were clean when inventoried. Existing rev1/rev3 builds are present in next, battery, theme, retain-range and primary trees; primary also has original and unsuffixed Waveshare builds. No inference was made about which local artifact is actually installed. Only configured remote is the user's `origin`; no fetch/pull/push was run.

Remaining decisive evidence: actual on-board USB identity, init/rate/tune/GPIO5 return codes during a VHF→HF transition, and reception after confirmed successful switching. Obtain through the P4's existing logging/diagnostics; do not require reconnecting the dongle's removed USB-A connector. Battery operation excludes the external charger, not every onboard noise source; no hardware cause is established here.
