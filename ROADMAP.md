# Waveshare ESP32-P4 SDR roadmap

This is a living list of possible work, not a claim that all features are implemented or tested on hardware. The receiver uses a single RTL-SDR USB/IQ stream; every decoder must respect that ownership and the board's CPU, RAM, and power limits.

## Current work

- [x] Replace the six Waveshare tuning arrows with two arrows and a selected, cyclic tuning-step indicator (100 Hz through 5 MHz, including 500 kHz; stable saved IDs). Host previews cover portrait/landscape layouts and both revision builds pass; four-way on-board touch remains to be checked.
- [x] Add paginated AM broadcast, shortwave and CB receive presets with host-tested frequency/mode selection. The V4 tuner range covers the chosen centers; live board reception remains to be checked.
- [x] Prototype Fine Offset WH2 weather and Steelmate TP-S15-family TPMS decoders in an opt-in foreground screen using the existing USB driver's IQ ring. Recorded fixture and negative host tests pass; this is **not** generic device compatibility or proof of live V4 reception.
- [ ] On the rev3 board, measure live WH2/Steelmate hit rate, USB backlog, dropped-IQ counts, decoder load, memory and audio/waterfall restoration before calling the narrow formats hardware-supported. The strict TPMS preamble check rejects one of three packets when the 250 kS/s fixture is resampled to 2.4 MS/s; assess that tradeoff with a live capture before relaxing it. Characterize TPMS fixed-power burst gating against broadband/adjacent RF noise and add an adaptive floor or overflow/rejection counters if real captures justify it.
- [x] Implement prompt mute, volume and squelch persistence in one versioned NVS record, loaded before audio unpauses. Legacy saved volume/squelch migrate on first change; failed writes are logged and retried. Simulated immediate power loss, zero-volume versus mute, migration and NVS failure tests pass. Pending general settings flush before sensor-mode retuning.
- [x] User confirms the audio-control settings persist through power-off/restart on the rev3 board. This is user-reported hardware evidence; no fresh physical-board test was performed during the current changes.
- [ ] Separately check speaker silence before first audible output, actual-flash failure reporting and wear under repeated holds; retention confirmation does not establish these properties.
- [x] Sensors BAND selector adds temporary 315/345/433.92/868.30/915 MHz RF centers, resetting IQ/decoder/readings on retune and restoring the original receiver on exit. Frequency coverage does not add generic Fine Offset 915 MHz protocol support; WH2/TP-S15 remain narrow named formats.
- [x] BAND preset selection returns to waterfall; tuning arrows and STEP move to a reserved bottom bar. Host previews exercise all looks in portrait/landscape; verify four-way hardware touch.
- [x] Confine touch tuning to the upper spectrum; top-left TOUCH cycles temporary FREEFORM (default), SNAP to saved STEP, and nearest thresholded PEAK within 24 pixels (no candidate means no tune). Host tests cover waveform/axis/waterfall hit rejection and signal selection across themes/orientations.
- [ ] Validate all three touch-tuning modes and tap coordinates with actual rev3 hardware and signals; host-synthesized FFT peaks do not prove real RF or touch accuracy.
- [x] SETTINGS offers persisted 30/60/90-second/never display timeout, safely blanking output/backlight without cutting touch power and consuming the wake gesture. Default never preserves existing behavior. Host policy/NVS/power-order tests and both revision builds pass.
- [ ] Validate timeout, real backlight/display power reduction, tap wake and decoder/audio continuity on rev3 hardware, including wake during USB failure/recovery. The BSP has PWM backlight + ST7701 DISPON/OFF, not a proven independent panel power-rail switch.

## Backburner: broad rtl_433 support

- [ ] Investigate porting rtl_433's processing/decoder core from [davidcoulson/esphome-rtl433](https://github.com/davidcoulson/esphome-rtl433) to the current librtlsdr USB/IQ ring **without** installing a second USB driver, ESPHome, or an unauthenticated HTTP service.
- [ ] Review the source licenses before integration. That ESP32-P4 port contains AGPLv3 code while this fork has a GPLv3 LICENSE; obtain an explicit licensing/architecture decision before incorporating it.
- [ ] Prefer an opt-in foreground decoder screen initially; it may pause receiver waterfall/audio while it owns decoding. Only attempt simultaneous decoding after on-board USB backlog, ring-drop, CPU, heap, and display-frame measurements.
- [ ] Scope decoder sets by tuned band and validate with recorded IQ plus live RTL-SDR Blog V4 hardware; never claim “all ISM traffic.” rtl_433 handles many OOK/FSK device protocols but not LoRa CSS, Wi-Fi, or BLE. The V4 tuner does not cover 2.4 GHz; the ESP32-C6 Wi-Fi/BLE scanner is separate.

## Possible later: more TPMS formats without changing USB drivers

- [ ] Investigate adapting [PortaPack Mayhem TPMS RX](https://github.com/portapack-mayhem/mayhem-firmware/wiki/TPMS-RX) incrementally after the current CU8 IQ ring. Its GPL-2.0-or-later TPMS source can be used under this GPLv3 firmware with attribution, but its HackRF 2.4576 MS/s signed-IQ DSP and message threads are not a drop-in fit for the V4's 2.4 MS/s unsigned-IQ stream; resampling or filter/timing redesign and on-board CPU measurement are required.
- [ ] Only enable a format after an IQ fixture plus negative/collision tests and a live sensor check. Mayhem dispatches seven named TPMS formats, not every make named in its UI; do not present an unimplemented model as supported. Weather devices require different protocols and demodulation.

## Release gates

- User reports FM reception and the changes tried so far working on the rev3
  setup. HF reception has not yet been observed, with mostly indoor use and a
  marginal antenna reported; revisit it with a suitable antenna and known
  signal. This is not a confirmed software fault or a successful HF test.
- Host DSP/UI/protocol tests and ESP-IDF rev1/rev3 builds/checksums are required, but do not establish real packet decoding or RF sensitivity.
- Verify the rev3 board's audio, controls, SDR reception, decoder hit rate and error recovery with a suitable signal source before a hardware-support claim. Preserve upstream attribution and provide source/licensing notices alongside any redistributed firmware.
