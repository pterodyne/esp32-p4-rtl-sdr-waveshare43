# Native-rate controls and narrow zoom verification

This branch starts at `b498d55` (hardware-working native-rate baseline). It selectively ports REC diagnostics from `364ac98` and immediate independent-stage gain persistence from `3cf164d`. It does **not** merge `f8114e6` or `e8b1e89`: native capture, DSP input, normal 2.4 MS/s and the existing 2 MS/s ADS-B path remain unchanged. No 8 MS/s resampling/FIR/queue/PIE implementation is included.

## Delivered behavior

* Accepted LNA/VGA adjustments snapshot and commit the backend-specific pair immediately on the UI task. A failed open/set/commit leaves that snapshot pending for bounded retries in normal, foreground and stream-loss/reopen loops. Temporary foreground gains do not replace it. A command failure does not save an unaccepted pair. This cannot guarantee retention when power is removed during a flash write or before a failed persistence retry.
* Failed REC preparation latches ERR/INFO on the receiver. INFO preserves the failure instead of automatically replacing it with a directory refresh. Mount, metadata rejection, worker/codec readiness, create/fdopen/header errors are distinguished; errno is captured before cleanup. This is diagnostic coverage, **not a claim that the physical card failure is fixed**.
* Persisted zoom IDs 0–3 remain 2.4M/1.2M/600k/300k. New IDs 4 and 5 are 200k and 100k. Zoom changes only the view span, not sampling or audio bandwidth.

## Waveform/display settings feasibility

The plotted waveform is an FFT power spectrum, not a time-domain oscilloscope. The current code requests `1024 << zoom` but clamps to `NFFT_MAX=4096`; 300k, 200k and 100k all use 4096 samples. At 2.4 MS/s, bin spacing is **585.9375 Hz** (Hann-window effective discrimination is wider). There are approximately 341 bins across 200k and 170 bins across 100k. Wide views use max aggregation to retain narrow peaks; narrow views repeat the actual bins across columns. Coarse steps are honest, not new RF resolution. The existing 0.5 per-frame spectrum smoothing and 0.25 dB/frame peak decay remain unchanged. Auto/manual reference and range already provide display settings. Optional bounded temporal smoothing (off/fast/slow) is feasible later without a rate change, but changes response time, not resolution. A larger FFT or true zoom decimator would be separate DSP/memory/CPU work and is not silently added here.

## Evidence and limits

`tools/test_hackrf.sh` includes actual UI save/action seams with transactional NVS stubs, persisted span/real FFT clamp tests, and the production FFT-column mapping. `tools/test_controls.sh`, `tools/test_rf.sh`, `tools/test_sd_audio.sh` and `tools/test_fatfs_recording.sh` cover existing controls, RF, real concurrent SD worker/audio-task routing and the installed ESP-IDF 5.5.5 FatFs engine. FatFs tests execute each generated configuration on RAM-backed sectors, 120 metadata names per configuration, exclusive collisions/stat/delete/list, and an LFN-disabled negative control reproducing `FR_INVALID_NAME`. Linux file acceptance alone is not used as FAT evidence. Current configurations accept the names, so the old 8.3 configuration defect does not prove the remaining card problem's cause.

Host DSP tests exercise actual USB/LSB demodulation and opposite-sideband rejection. `tests/ssb_audio_display_runtime_test.py` additionally executes the production audio-task body with real demodulation and FFT/view rendering, plus the actual UI recovery branch, in a deterministic virtual-IQ/codec scheduler. NFM/USB/LSB/AM each complete 3000 PCM blocks and 300 displayed frames over six virtual seconds without recovery or a loss-message flash. Injected periodic stream loss makes its no-flash assertion fail, and four forced losses reach the real message/reopen path. This is **not** FreeRTOS/USB hardware scheduling or a target timing measurement. USB/LSB host CPU here is about 0.10–0.11 seconds per six virtual seconds versus 0.08 for NFM/AM, not evidence of target headroom. These tests do not establish target CPU/USB scheduling or reproduce the reported periodic blue flash. The native stream/audio scheduling remains unchanged; a targeted hot-buffer allocation mitigation is described below. Do not claim that symptom fixed from a successful preview or build. Existing runtime logging includes USB errors/backlog, stream ended/restart/reopen, demod load and audio lag; correlate those with the flash before altering recovery or DSP.

`dist/preview/native-200k.png`, `native-100k.png`, and `native-100k-portrait.png` are **synthetic host renders**, not live RF or board evidence. They show the actual zoom labels and display span; unknown gain/battery markers deliberately remain unknown. No device was flashed, no UART opened, and no TX was enabled.

## SSB blue-flash mitigation (hardware cure unproven)

The installed ESP-IDF 5.5.5 `components/esp_lcd/dsi/esp_lcd_panel_dpi.c:112–116` explicitly documents a blue display on DPI underrun and logs `can't fetch data from external memory fast enough, underrun happens`. This is a concrete mechanism consistent with the reported symptom, **not a captured underrun from this board**. The former shared DSP allocator placed every decimating FIR's taps/history and demod stage workspace in PSRAM, competing with the continuously fetched DPI framebuffer. The mitigation moves only those hot, bounded buffers and the 200-byte PCM output workspace to explicitly requested internal, byte-addressable RAM, with no PSRAM fallback. The large IQ ring/audio IQ input and framebuffers remain in PSRAM. The static 241-tap SSB coefficient/history arrays were already internal BSS (four 964-byte symbols) and remain unchanged. No FIR coefficients, sample rates, decimation, USB queue, task priority or recovery policy changed.

Measured payload budget from executed allocation tests:

| Internal allocation | Bytes |
|---|---:|
| Four demod FIR taps/history | 17,384 |
| Demod stage workspace | 7,688 |
| RDS FIR taps/history | 2,540 |
| PCM output workspace | 200 |
| **Additional internal heap payload** | **27,812** |

Allocator metadata/rounding is extra. `tests/dsp_memory_placement_test.py` fails on the former PSRAM capability flags, then passes with exact internal capability checks and the real DSP running. Failure injection covers all 11 DSP allocations and both audio-task allocations; partial allocations are freed and audio readiness remains false rather than claiming a working codec owner without a DSP/task workspace. No SPI RAM fallback hides insufficient internal memory.

The final HackRF rev3 size report shows static DIRAM use of 260,962 bytes (154,304 BSS, 86,414 code, 20,244 data), leaving 394,398 bytes in the size tool's 655,360-byte region. This linker remainder is **not runtime free heap**; USB/DMA/task allocations also consume internal memory, and the size tool warns it cannot assign `sram_seg` to a chip memory type. Final revision maps place all four static SSB arrays in internal `0x4ff...` RAM. Runtime minimum heap and largest block under actual SSB/SD/display load remain hardware checks.

A baseline-versus-mitigation DSP comparison produced bit-identical 96,000 PCM bytes across WFM/NFM/AM/USB/LSB; the signal suite retains USB/LSB opposite-side rejection of 86.6/87.2 dB. Host timing does not prove reduced P4 bus contention. The installed public DPI event callbacks expose transfer completion, framebuffer completion and VSYNC, **not an underrun callback/counter**. No SDK patch, private ISR hook or fabricated counter is added; the SDK's existing bounded-per-interrupt underrun log remains the supported observation. Hardware verification must determine whether this mitigation removes flashing and whether the SDK underrun message correlates with it. REC ERR/INFO diagnostics remain intact; the physical REC failure is still unproven.

Full images for HackRF/RTL and revision 1/3 are built with ESP-IDF 5.5.5. Use only the matching hardware revision/backend. Full-image writes at 0x0 can reset NVS/settings. Generated images in ignored `dist/` are local artifacts, not published releases.
