# HackRF One RX test build — Waveshare 4.3

This is a selectable **receive-only** backend for the existing Waveshare
ESP32-P4-WIFI6-Touch-LCD-4.3 BSP. It is a host-tested, cross-built experimental
port. The owner reports working FM at **LNA 40 / VGA 62**; this is positive
FM-only evidence on the previous low-rate path, not validation of this 8 MHz
streaming/resampling path, HF, or other modes.
No flashing, UART access, transmission, or RF measurements were performed
for this capture/resampling change.
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

HackRF captures only **8 MS/s CS8** (signed interleaved I/Q), **16 MB/s USB**.
There is no capture-rate menu and 2/2.4 MS/s are never sent to the hardware.
Request 6 carries little-endian 8000000, divider 1. Request 7 selects **6 MHz**:
this is the official libhackrf v2024.02.1 `max2837_ft` entry selected by its
widest-filter-at-most-75%-of-Fs policy (not the old 1.75 MHz workaround).

The existing DSP/ring contract remains **2.4 MS/s unsigned interleaved IQ**.
A stateful, output-driven **3/10 polyphase FIR** converts from 8 MS/s. ADS-B
requests **2 MS/s**, using a filtered **1/4** conversion, then restores 2.4.
Each phase has 96 Q15 coefficients: normalized Blackman-windowed sinc,
cutoff 1.05 MHz (normal) or 0.85 MHz (ADS-B). Conversion performs no DC
subtraction or gain adjustment. Signed zero maps to 128, -128 to 0, +127 to
255; FIR overshoot clips rather than wrapping. The fractional phase, history,
and a pending I byte survive arbitrary USB splits, including odd lengths.
Every start resets the FIR/queue. Retune quiesces/drains RX before programming
and restarting; disconnect/reopen and rate changes also restart fresh. Output
has the FIR's startup transient/group delay (~6 microseconds), not old RF history.

The displayed zoom spans stay **2.4/1.2/0.6/0.3 MHz**; these are display spans,
not hardware capture rates or a guarantee of flat response to the edges. The
FIR has a transition near the 2.4 MHz span edges. Audio/FFT/sensor/ADS-B rates
are not relabelled 8 MHz. Rate writes while the USB owner is alive are refused.
A partial clock/filter failure retries the previous fixed hardware setup; if
that also fails the rate getter reports 0 (unknown) and start is disabled.
ADS-B enters only when the acknowledged output rate is 2 MHz; its restore path
keeps RX/audio stopped if 2.4 MHz is not acknowledged. Independent LNA/VGA desired
settings are restored on exit rather than lost through RTL's preset index.

### Throughput and memory budget — NOT a P4 performance result

- Ten 24 KiB DMA transfers remain **245760 bytes** of internal DMA payload
  plus USB metadata/control allocations. At 16 MB/s each completes every
  **1.536 ms** (~651 callbacks/s), with **15.36 ms** total queued payload.
  The shared 64-entry completion queue exceeds the ten simultaneously owned
  transfers; this does not prove the controller/PSRAM can sustain the load.
- Callback does one bounded raw copy, no FIR and no queue wait. A **512 KiB
  PSRAM SPSC queue** holds **32.768 ms** at 16 MB/s. Queue exhaustion stops and
  drains RX, marks/logs overflow, and restarts through existing recovery. It
  never silently concatenates IQ across a dropped raw block.
- A separate priority-19 FIR task on core 1 consumes up to 24 KiB at a time;
  USB owner/client remain on core 0. A 24 KiB PSRAM output scratch buffer is
  reused; the existing 1 MiB CU8 ring holds ~218.45 ms at 4.8 MB/s or 262.14 ms
  at 4 MB/s. Raw write+read traffic is 32 MB/s before output writes/reads,
  display/audio/SD activity and caches. Startup allocates an extra worker
  stack (4096 bytes), FIR state and task metadata, not a second DMA payload.
- 96 taps require **460.8 million scalar MAC/s** for both channels at 2.4 MHz
  (384 million at 2 MHz). A scalar P4 implementation would be inappropriate.
  Firmware instead uses an unrolled P4 PIE signed-16 dot kernel: 24 eight-lane
  MAC instructions plus 48 vector loads per complex output. Mirrored planar
  internal history avoids inner-loop modulo and copying for each output.
  Instructions are not cycles: load stalls, unaligned accesses, caller/input
  overhead, scheduling and other DSP work are unmeasured. **No sustainable
  P4 rate, CPU utilization, available DMA heap or drop-free claim is made.**
- Before USB open, firmware tests the selected dot kernel against a scalar
  oracle at all eight halfword alignments and refuses RX on mismatch. This is
  a runtime safety gate, not a substitute for executing it on a board.
- On stream exit, firmware logs accepted raw bytes/elapsed microseconds, worker
  processing microseconds (including preemption), queue peak, max callback
  microseconds and raw-overflow flag. These metrics and the existing USB stats
  need sustained board measurements with waterfall/audio/SD and ADS-B active.
  No raw sample sequence counter exists: zero local overflows still cannot
  establish zero losses in HackRF firmware, USB/controller or upstream RF.

Host measurements and exact test/build evidence are recorded in
[8 MS/s verification report](HACKRF-8M-REPORT.md). Host throughput is not P4
throughput. This replaces the previous experimental below-spec hardware clock,
but the new 8 MHz streaming/PIE path is **not yet hardware-verified**.

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
  the other stage. No hold-repeat: one deliberate adjustment per press, with
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
and ON, analog/digital alias rejection and runtime DMA/internal/PSRAM/CPU headroom. None has
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
