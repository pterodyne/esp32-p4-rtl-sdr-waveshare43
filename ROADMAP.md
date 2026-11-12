# Waveshare ESP32-P4 SDR roadmap

This is a living list of possible work, not a claim that all features are implemented or tested on hardware. The receiver uses a single RTL-SDR USB/IQ stream; every decoder must respect that ownership and the board's CPU, RAM, and power limits.

## Current work

- [x] Replace the six Waveshare tuning arrows with two arrows and a selected, cyclic tuning-step indicator (100 Hz through 1 MHz). Host previews cover portrait/landscape layouts and both revision builds pass; four-way on-board touch remains to be checked.
- [x] Add paginated AM broadcast, shortwave and CB receive presets with host-tested frequency/mode selection. The V4 tuner range covers the chosen centers; live board reception remains to be checked.
- [x] Prototype Fine Offset WH2 weather and Steelmate TP-S15-family TPMS decoders in an opt-in foreground screen using the existing USB driver's IQ ring. Recorded fixture and negative host tests pass; this is **not** generic device compatibility or proof of live V4 reception.
- [ ] On the rev3 board, measure live WH2/Steelmate hit rate, USB backlog, dropped-IQ counts, decoder load, memory and audio/waterfall restoration before calling the narrow formats hardware-supported. The strict TPMS preamble check rejects one of three packets when the 250 kS/s fixture is resampled to 2.4 MS/s; assess that tradeoff with a live capture before relaxing it. Characterize TPMS fixed-power burst gating against broadband/adjacent RF noise and add an adaptive floor or overflow/rejection counters if real captures justify it.
- [x] Implement prompt mute, volume and squelch persistence in one versioned NVS record, loaded before audio unpauses. Legacy saved volume/squelch migrate on first change; failed writes are logged and retried. Simulated immediate power loss, zero-volume versus mute, migration and NVS failure tests pass. Pending general settings flush before sensor-mode retuning.
- [ ] Verify the chosen mute, volume and squelch states through a full power-off and restart **on the rev3 board**; check speaker silence before first audible output and test failure reporting with actual flash if possible. Host NVS stubs cannot prove physical retention or flash wear under repeated holds.

## Backburner: broad rtl_433 support

- [ ] Investigate porting rtl_433's processing/decoder core from [davidcoulson/esphome-rtl433](https://github.com/davidcoulson/esphome-rtl433) to the current librtlsdr USB/IQ ring **without** installing a second USB driver, ESPHome, or an unauthenticated HTTP service.
- [ ] Review the source licenses before integration. That ESP32-P4 port contains AGPLv3 code while this fork has a GPLv3 LICENSE; obtain an explicit licensing/architecture decision before incorporating it.
- [ ] Prefer an opt-in foreground decoder screen initially; it may pause receiver waterfall/audio while it owns decoding. Only attempt simultaneous decoding after on-board USB backlog, ring-drop, CPU, heap, and display-frame measurements.
- [ ] Scope decoder sets by tuned band and validate with recorded IQ plus live RTL-SDR Blog V4 hardware; never claim “all ISM traffic.” rtl_433 handles many OOK/FSK device protocols but not LoRa CSS, Wi-Fi, or BLE. The V4 tuner does not cover 2.4 GHz; the ESP32-C6 Wi-Fi/BLE scanner is separate.

## Possible later: more TPMS formats without changing USB drivers

- [ ] Investigate adapting [PortaPack Mayhem TPMS RX](https://github.com/portapack-mayhem/mayhem-firmware/wiki/TPMS-RX) incrementally after the current CU8 IQ ring. Its GPL-2.0-or-later TPMS source can be used under this GPLv3 firmware with attribution, but its HackRF 2.4576 MS/s signed-IQ DSP and message threads are not a drop-in fit for the V4's 2.4 MS/s unsigned-IQ stream; resampling or filter/timing redesign and on-board CPU measurement are required.
- [ ] Only enable a format after an IQ fixture plus negative/collision tests and a live sensor check. Mayhem dispatches seven named TPMS formats, not every make named in its UI; do not present an unimplemented model as supported. Weather devices require different protocols and demodulation.

## Release gates

- Host DSP/UI/protocol tests and ESP-IDF rev1/rev3 builds/checksums are required, but do not establish real packet decoding or RF sensitivity.
- Verify the rev3 board's audio, controls, SDR reception, decoder hit rate and error recovery with a suitable signal source before a hardware-support claim. Preserve upstream attribution and provide source/licensing notices alongside any redistributed firmware.
