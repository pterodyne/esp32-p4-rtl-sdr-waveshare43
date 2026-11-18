# Fixed 8 MS/s HackRF path: verification and limits

## Scope and evidence boundary

This change replaces native 2/2.4 MHz HackRF clocking with one fixed **8 MS/s
CS8** input and filtered **3/10 → 2.4 MS/s CU8** or **1/4 → 2 MS/s CU8** output.
It preserves the four display spans, RTL backend and independent stage controls.
The previous owner-confirmed FM/stage-UI result belongs to the previous image.
**No 8 MHz hardware capture, P4 throughput, audio, RF, ADS-B or board boot result
is claimed here.** No flash, serial, hardware access or TX was performed.

See [implementation and 16 MB/s budget](HACKRF-RX.md#throughput-and-memory-budget--not-a-p4-performance-result).
Only the official GSG HackRF protocol/filter source and Espressif PIE instruction
reference were retrieved; no original application-upstream network action,
publication, push or merge was used.

## TDD observations

The following failures were observed before their implementations/fixes:

- New resampler count/center tracer: missing `hackrf_resample.h/.c`; then green
  exact 600/500 output-byte counts from 2000 raw bytes at 2.4/2 MHz.
- Protocol: `hackrf_rx_rate(NULL,8000000)==0` failed against the old low-rate
  implementation. Green requires the exact 8 MHz payload, 6 MHz LPF command,
  rejects 2/2.4 MHz hardware requests, and still rejects TX mode 2.
- Copy-only pipeline/rate tests initially failed to compile for the missing
  pipeline functions/state. Green tests the actual backend with USB/RTOS seams,
  not a synthesized receiver output.
- Odd USB chunk continuation: stream test line 43 failed against the old
  odd-length rejection. Green accepts arbitrary byte splits without I/Q reversal.
- ADS-B guard/stage-restore route contracts failed before the guards existed.
  The subsequently extracted checked-rate request failed compilation before
  implementation. Its runtime tests exercise real backend success, rollback,
  unknown-rate failure and active-owner refusal; UI source contracts ensure
  the checked path gates decoder entry and audio/RX restoration. The full
  foreground UI is cross-compiled, not run as an interactive host program.
- Selected-kernel oracle test failed compilation before the self-test existed;
  green includes deliberate kernel corruption detection in a host seam.
- Initial P4 assembly build failed on illegal PIE register operands, not a
  completed build. Standalone assembly and all firmware builds were rerun after
  fixing operand selection. The assembled-disassembly model also caught the
  CFG-policy regression: like esp-dsp, unaligned access must remain enabled
  rather than restoring a stale global CFG value across preemption.

The target uses a 96-tap Q15 output-driven polyphase filter, not upsampled
zero-fill processing. A scalar P4 dot implementation would require 460.8 million
MAC/s at 2.4 MHz before loop overhead. The P4 path therefore uses an unrolled
PIE kernel, while host tests use the scalar/vectorizable C reference. The real
assembler accepts the target kernel. A disassembly instruction model checks
256 deterministic vectors, all eight halfword alignments, exact read bounds,
I/Q stores and global CFG policy. **That model is not execution on P4 silicon.**
Firmware independently compares the actual selected kernel with a scalar oracle
before USB open and refuses RX on mismatch; that runtime gate has not been
observed on hardware.

## Synthetic signal and continuity tests

`sh tools/test_hackrf.sh` checks:

- Exact long-stream counts for both rates and bit-identical results under
  arbitrary chunks, including byte-odd boundaries and startup resets.
- Output-capacity rejection without modifying state, invalid rates, signed
  extrema/DC normalization, saturation, and no arbitrary DC/gain changes.
- Real generated complex tones with coherent frequency/I/Q orientation checks:
  850 kHz normal, 650 kHz ADS-B, and negative-frequency 650 kHz. Pass RMS is
  56.4583 / 56.4695 for amplitude-80 input (normal / ADS-B).
- Alias-source tones from 1.4–3.8 MHz normal and 1.2–3.8 MHz ADS-B, in 200 kHz
  increments. Their settled CU8 outputs are all center; the conservative
  half-LSB reporting bound is **≤ -41.06 dBc**, not infinite rejection.
  This is synthetic digital evidence, not analog RF rejection or flat response
  throughout the displayed span's edge/transition band.
- Two-thread real raw-queue/worker processing of 2,000,000 bytes at each output
  rate, with odd chunks and both physical-ring and uint32 counter wrap; exact
  equality with uninterrupted filtering. Queue-full fault stops rather than
  silently dropping/splicing a block; reset clears the fault and FIR history.
- Existing RX-only safety, stop/drain/restart/disconnect, descriptor, Bias-T,
  stage gain/persistence and all-look/all-rotation renderer contracts.

ASan+UBSan runs of the resampler and backend/queue tests pass. The four existing
suite entrypoints also pass: `test_hackrf.sh`, `test_controls.sh`, `test_rf.sh`,
`test_sd_audio.sh`. Logs are local ignored outputs under
`dist/hackrf-8m-evidence/`, not published release assets.

## Measured host CPU benchmark (not P4)

Environment: x86_64 Linux, AMD RYZEN AI MAX+ 395, Debian GCC 14.2.0, `-O2`.
One observed run (reruns vary):

| Path | Output rate | Input processed | CPU seconds | Native throughput |
|---|---:|---:|---:|---:|
| FIR only | 2 MHz | 16,000,000 raw bytes | 0.034 | 236.735 MS/s |
| FIR only | 2.4 MHz | 16,000,000 raw bytes | 0.040 | 198.491 MS/s |
| Callback + raw queue + FIR + CU8 ring | 2 MHz | 24,576,000 raw bytes | 0.052245 | 235.200 MS/s |
| Callback + raw queue + FIR + CU8 ring | 2.4 MHz | 24,576,000 raw bytes | 0.063486 | 193.554 MS/s |

The FIR-only CPU times are rounded in the test output. These are CPU-time
benchmarks of actual host code with synthetic data and USB/RTOS seams; they do
not include physical USB, PSRAM, P4 PIE execution, display/audio/SD scheduling
or RF. No extrapolated P4 CPU percentage is presented.

## Build and artifact gate

Build using `/home/pterodyne/esp/esp-idf/export.sh` (ESP-IDF v5.5.5) and the
existing build script for HackRF/RTL × rev1/rev3. Generated configs must select
32 MB flash, backend explicitly, and minimum silicon revision 100/301 respectively.
Final-source merged images, bootloaders, tables, applications, ELF/map/config
and full/constituent SHA256 are local artifacts. `dist/hackrf-8m-evidence/manifest.json`
records the committed source identity, each merged-image hash and every
`flasher_args.json` constituent's offset/hash, with byte-for-byte comparisons at
those offsets. Do not substitute a pre-final-edit merged image.

The full-image offset is 0x0; use only the confirmed matching silicon revision.
Full-image flashing can reset NVS/settings. This report is not authorization to
flash and does not label host cross-builds as hardware verification.

## Independent review fix: normal-rate recovery and RTL checked requests

The ADS-B exit now retains an explicit pending normal-rate restoration state.
Both attached-device restart and close/open recovery must successfully check
2.4 MS/s before starting RX and resuming the paused audio reader. Pending state
bypasses foreground decoder requests (including requests queued on the exit
iteration) and blocks normal FFT/render/audio processing even if stop is still
draining. The existing mute, volume, squelch, saved frequency and stage gains
are not replaced by recovery defaults.

RTL's existing void setter/checked-request API now commits its rate only after
a successful hardware command. Failed changes attempt a checked rollback;
failed rollback reports rate zero and refuses RX start. Open checks its rate
command, closes on failure and retries instead of claiming an initialized
receiver. The last requested/confirmed reopen rate remains separate from the
known-clock flag; close invalidates clock knowledge. Active RX owners reject
rate changes.

Strict red/green evidence: the new executable UI seam first aborted at the
missing pending-state assertion; the actual RTL setter/getter seam first
aborted because a failed 2 MHz command falsely reported 2 MHz. Both then pass.
The UI test compiles the actual ADS-B restoration statements, recovery branches
and restoration helper from `ui.c`, injecting stopped/reopen/rate failures at
the hardware boundary. It exercises failed normal restoration with successful
2 MHz rollback and unknown clock followed by reopen, requires zero normal
frames/starts/resumes at the wrong rate, and checks audio resumes after checked
recovery. This is host control-flow evidence, not interactive UI or hardware
audio execution. Existing actual HackRF backend fault-injection tests remain.

After the fix, all four suites (`test_hackrf.sh`, `test_controls.sh`,
`test_rf.sh`, `test_sd_audio.sh`) pass. HackRF and RTL rev1/rev3 builds and merged
images were regenerated using ESP-IDF v5.5.5 / RISC-V GCC 14.2.0. Final source,
logs, configurations, constituent offsets/hashes and merged-image byte
comparisons are recorded locally in `dist/hackrf-review-fix-evidence/`.
No upstream access, push, merge, flash, UART or TX was performed.

## Remaining required hardware work

Measure the boot kernel self-test, sustained native accepted USB throughput,
callback latency vs 1.536 ms transfer cadence, queue peak/overflow, controller
backlog/errors, DMA minimum/largest block, internal/PSRAM peak and FIR/UI/audio
load. Repeat with waterfall, SD audio and ADS-B active; confirm no recurrent
recovery/starvation. Test retune/reconnect/rate transitions with known signals,
passband/alias behavior, WFM/AM/SSB listening and ADS-B packet counts. Verify
actual Bias-T voltage and power margin separately. Local zero-overflow counters
cannot prove zero loss inside HackRF or the USB controller. Sustainable P4
throughput and the physical 8 MHz path remain **unknown until measured**.
