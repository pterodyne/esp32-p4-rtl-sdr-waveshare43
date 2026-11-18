# HackRF One RX test build — Waveshare 4.3

This is a selectable **receive-only** backend for the existing Waveshare
ESP32-P4-WIFI6-Touch-LCD-4.3 BSP. It is a host-tested, cross-built experimental
port. The owner reports working FM at **LNA 40 / VGA 62**; this is positive
FM-only evidence, not validation of HF, other modes, or low-rate alias rejection.
No flashing, UART access, transmission, or RF measurements were performed
for this UI change.
The user's HackRF is connected to the P4's 40-pin USB-host data connection,
not passed through to the Linux build host. This backend uses that same
ESP-IDF USB Host controller and existing shim; it does not use host Linux USB.
No pin, display, touch, or VBUS routing changes are made.

## Build and images

With ESP-IDF 5.5.5 exported:

```sh
. /home/pterodyne/esp/esp-idf/export.sh
SDR_BACKEND=hackrf tools/build_waveshare43.sh
sh tools/test_hackrf.sh
```

`SDR_BACKEND=hackrf` adds `sdkconfig.hackrf` to the defaults and isolates build
state in `build-waveshare43-hackrf-rev1` and `build-waveshare43-hackrf-rev3`.
Kconfig also offers **SDR receiver → USB receiver backend**. Existing RTL builds
remain the default; `tools/build_waveshare43.sh` without the environment
variable still builds RTL in the original directories and filenames. Do not
reuse a saved sdkconfig for a different backend; defaults do not overwrite
existing selections.

Local generated full images (not published release assets):

- `dist/p4-sdr-waveshare43-hackrf-rev1.bin`
- `dist/p4-sdr-waveshare43-hackrf-rev3.bin`
- `dist/SHA256SUMS-waveshare43-hackrf`

Each full image contains the revision-matched bootloader, partition table and
application at the offsets from its `flasher_args.json`; its intended full-image
flash offset is **0x0**. Only use the image matching confirmed silicon revision.
Full-image flashing **can reset NVS/settings**. This work does not authorize or
perform flashing. Both revision images have the same features. Matching ELF,
map, sdkconfig and partition files remain in each build directory.

## Wire protocol and sample rates

Enumeration matches HackRF One VID:PID **1d50:6089** (not Jawbreaker, rad1o,
DFU mode, or HackRF Pro). It requires negotiated **USB high speed** and validates
interface 0 / alternate 0 bulk IN **0x81**, maximum packet size **512**. RTL also
uses endpoint 0x81, but its vendor control protocol is entirely different.
The new driver implements HackRF controls, not RTL register accesses.
Ten asynchronous 24 KiB transfers use the shim's DMA buffer pool. Only one
owner task handles completions and resubmission. Stop cancels and drains all
submitted transfers before freeing them; close refuses to release a device
still owned by RX. Disconnect/error ends RX and the existing UI recovery path
closes/reopens the receiver. Reopen repeats the safety initialization.

HackRF sends **signed interleaved 8-bit I/Q**. Every byte is XORed with 0x80
before entering the existing unsigned-IQ PSRAM ring. Pair order is unchanged;
odd-length completions are rejected rather than silently swapping I and Q.
The ring is 1 MiB and its 64-bit publication counter is protected on the 32-bit P4.

The DSP's native rate stays **2.4 MS/s** (4.8 MB/s USB IQ). ADS-B uses **2 MS/s**
(4 MB/s), then restores the receiver rate. Both are programmed using HackRF's
fractional-rate request: little-endian `freq_hz`, divider=1. The official firmware handler calls `sample_rate_frac_set(2 * freq_hz, divider)`
in `firmware/common/hackrf_core.c`; this is not its separate fixed-rate
`sample_rate_set` switch/table. Thus absence of 2.4 MS/s from that fixed table
does not make the fractional-rate USB request unsupported.
No silent 8 MS/s
capture or incorrectly labelled decimation is substituted. Unsupported application
rates and rate changes while RX is active are rejected.

**Important limitation:** the HackRF clock API accepts these low rates, but
Great Scott Gadgets explicitly **does not recommend sampling below 8 MS/s**.
The MAX5864 is not specified below 8 MHz and the MAX2837's minimum 1.75 MHz
baseband filter provides insufficient anti-alias rejection at 2/2.4 MS/s.
We select that minimum filter at both rates. Nearby strong signals may alias
into the displayed spectrum/audio; sensitivity and decoder behavior are unknown.
This is an explicit low-rate test compromise, not a claim that HackRF only
supports 8/10/20 MS/s or that low-rate RF quality is guaranteed. A future
production-quality path should capture >=8 MS/s and implement measured,
anti-aliased conversion to the DSP rates, after proving USB/DMA/CPU headroom.

## RF controls and safety

- Only OFF (mode 0) and RX (mode 1) are accepted. No TX mode, TX gain, TX sample
  output or bulk OUT path exists in the backend.
- RF amp is commanded **OFF** on every open and close, and has no UI enable.
- Antenna Bias-T is commanded **OFF** on open and close, never persisted.
  On API >=1.08, OFF/RX mode-entry bias overrides are disabled first so they
  cannot silently re-enable power when streaming starts (TX options untouched).
  Older firmware lacks that options request; nonstandard/custom firmware is
  outside this implementation's assurances.
- Bias-T ON is an explicit RF INPUT action for a compatible **3.3 V** active
  antenna/LNA only. Failed commands leave the UI's power state unknown and OFF
  remains retryable. A USB acknowledgement is **not a voltage measurement**.
- HackRF has **no tuner AGC**. Independent **LNA -/+** (0–40 dB, 8 dB steps)
  and **VGA -/+** (0–62 dB, 2 dB steps) sit left of the frequency and battery
  readout in landscape. VGA is the HackRF stage (not “VNA”). Portrait reserves
  an additional 40-pixel row for frequency/battery, rather than overlapping the
  widest four-digit MHz plus 100-Hz readout. MENU stays on the top bar.
  The old top-bar GAIN preset toggle is absent **only on HackRF**; RTL's UI,
  tuner gain table, AGC and driver remain unchanged. The existing Controls gain
  page still offers paired shortcuts, which deliberately set both stages.
- Each main-screen stage tap sends just request 19 (LNA) or 20 (VGA), preserving
  the other stage. No hold-repeat: one deliberate adjustment on release, with
  ordinary wake touches consumed through stable release. Values show acknowledged
  commands, not measured analog gain. A failed command marks only that stage
  `?`, retains its previous desired value, and remains retryable at a limit.
  A failed paired shortcut conservatively marks both stages unknown.
- Independent desired values survive reopen and are saved as a versioned atomic
  `hrgain1` pair in NVS namespace `sdr`, using the existing three-second settings
  debounce with write/commit retry. Immediate power removal before that save can
  lose the latest adjustment. Missing/corrupt records default to L40/V0.
  HackRF does **not read or overwrite RTL's legacy `gaint` key**; the previous
  HackRF paired-total setting is not migrated because it cannot encode independent
  stages. Diagnostic sessions suppress saving the temporary pair. RF amp stays OFF.
- RTL direct-sampling I/Q choices are disabled. The existing RTL/Blog PLL/GPIO5
  diagnostic is not a HackRF diagnostic; it reports unknown/unavailable and
  cannot establish HackRF lock or RF input state.
- The current application retains its UI tuning range, **1–1766 MHz** for this
  backend. HackRF's full 6 GHz hardware range is not exposed by this port; the
  app has uint32 frequency APIs and RTL-era UI bounds. Sub-1 MHz presets clamp
  to 1 MHz. This is not a new HF reception claim.
- HackRF has no RTL counter test. Boot explicitly says the counter test is
  unavailable; it does not invent loss counts, throughput or a poor-link result.

## Verification and remaining hardware work

`sh tools/test_hackrf.sh` exercises real protocol byte packing/ack failures,
signed-to-unsigned boundaries, rejection of TX mode, rate filtering, bias override
version gating, async stop/drain/restart, partial submission, disconnect, odd IQ,
HS endpoint descriptor parsing, actual SDR open/reopen/safety/rate/gain/ring paths
with host USB/RTOS seams, stage isolation at every legal setting, grouped NVS
set/commit/range failures, and source contracts for the UI command/save routes.
The real host renderer checks gain-key hit/paint, widest-frequency separation
from keys/labels/battery, unknown states, pressed feedback, all five looks and
four rotations, plus the existing wake-consumption policy. These do not replace
physical touch or RF tests. Generate synthetic (not live RF) final-source PNGs:

```sh
sh tools/test_hackrf.sh
mkdir -p dist/gain-previews
"$TMPDIR/preview-hackrf" --test-hackrf-header dist/gain-previews
```

The existing controls, RTL RF-I/O and SD audio host suites are also regression
checks. Host seams do not emulate the actual ESP-IDF host-controller scheduler.

Before calling this hardware-verified, separately establish: actual HackRF
firmware/API and descriptors, negotiated HS on this header wiring, sufficient
protected VBUS/current margin, sustained sample throughput and discontinuities,
physical reconnect recovery, known-signal frequency accuracy, WFM/AM/SSB audio,
ADS-B's 2 MS/s switch and recovery, sensor reception, actual antenna voltage OFF
and ON, low-rate aliasing and runtime DMA/internal/PSRAM/CPU headroom. None has
been measured here. Do not treat byte delivery alone as loss-free RF sampling.

The future **7-inch** board is not ported: its exact model, panel/touch/BSP,
silicon revision and USB/power wiring have not been supplied. Do not flash the
4.3-inch image to an unspecified 7-inch board.

## Sources and attribution

Protocol implementation reference (not John O'Dell upstream):

- [Great Scott Gadgets hackrf v2024.02.1 libhackrf](https://github.com/greatscottgadgets/hackrf/blob/v2024.02.1/host/libhackrf/src/hackrf.c)
- [Official firmware USB transceiver controls](https://github.com/greatscottgadgets/hackrf/blob/v2024.02.1/firmware/hackrf_usb/usb_api_transceiver.c)
- [Official sampling-rate guidance](https://hackrf.readthedocs.io/en/latest/sampling_rate.html)

`main/hackrf_rx.c` is a small RX-only implementation of that published protocol,
with GPL-2.0-or-later attribution in its header; no complete libhackrf or TX
implementation is vendored. The application remains GPL-3.0; the original
project license and acknowledgements are preserved. See `THIRD_PARTY.md`.
