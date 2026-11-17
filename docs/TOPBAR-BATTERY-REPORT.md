# Topbar MENU and measured battery voltage

## Scope and evidence

All edits and generated evidence are local to this dirty worktree. Existing SD recording, playback, deletion, metadata/timestamp gating, RF and controls changes remain in place. No Wi-Fi/NTP/dropdown implementation, installs, commits, Git network operations, UART access or flashing.

Current source before this change already placed the MENU hitbox at theme `btn_y`, not the frequency row. Thus a report of a lower physical MENU cannot be explained by that source alone; running-image identity remains unverified. The old MENU was a narrow 56px key at the right edge with a duplicate gain subtitle. This change makes it occupy the entire remaining top-button row after PLAY (portrait x422..475; landscape x446..795), with the same theme y/height as all quick controls and one centered MENU label. There is no second-row MENU hitbox. Readout and battery occupy the distinct lower header row in both orientations. Synthetic host previews: `previews/topbar-battery/{portrait,landscape}.png`; visually inspected, not photographs or live readings.

## Authoritative circuit

- Official resources: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3/Resources-And-Documents
- Official schematic: https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf
- Page 1, BATTERY/ADC block: J2 BAT -> R12 **200 kΩ ±1%** -> BAT_ADC -> R15 **100 kΩ ±1%** -> GND; C173/C174 each 100 nF from ADC node to ground. Page 1 P4 U8 pin 22 is **GPIO20**, on BAT_ADC. Download and rendered evidence retained as `board-source.pdf`, `.txt`, `.png`.
- ESP-IDF target source `components/soc/esp32p4/include/soc/adc_channel.h`: ADC1 GPIO20 channel **4**. This is P4, not an S3 pin mapping. Source reference: https://github.com/espressif/esp-idf/blob/v5.5/components/soc/esp32p4/include/soc/adc_channel.h
- Page 1 charger **U19 ETA6098**, BAT pin 6; STAT pin 9 goes to **TP1**, not an MCU telemetry GPIO. Boost U17 SCT12A0DHKR is not an I2C battery gauge. RTC BAT header is a separate backup supply path, not a fuel gauge or a cell ADC. No PMIC probing or shared-bus changes are required.
- Chip log revision **3.2** identifies silicon and selects the rev3 build; it does **not** identify PCB revision. Physical PCB-to-schematic correspondence has not been verified here.

## Acquisition and semantics

`main/battery_voltage.c` starts one low-priority worker, configures ADC1/channel4 at 12dB attenuation, and requires P4 curve-fitting calibration. It calls `adc_oneshot_get_calibrated_result`, averages 16 calibrated divider-node readings spaced by 20ms, then waits 4700ms (approximately 5.02s per batch). Millivolts are multiplied by 3. No unknown GPIO is driven; no I2C writes or scans. UI reads an atomic published value and redraws only on change while receiver is visible. Calibration/setup failure, ADC read errors, or any implausible divider sample retain/display truthful `BAT --.--V`; no raw uncalibrated fallback. A batch after a transient sample error can recover automatically; startup initialization failure remains unavailable until restart. Plausibility window is 833..1450 divider mV (2499..4350 BAT mV); this is not a cell-presence detector.

Text is green above 3.900V, amber 3.600..3.900V inclusive, red below 3.600V. These are conservative rough loaded single-cell 18650 voltage bands, NOT guaranteed numerical SOC. Two decimals do not promise 0.01V accuracy: resistor tolerance, ADC error, load and temperature matter. Charging/USB power can raise the BAT-terminal voltage; an open battery-lead switch or disconnected cell can leave a charger-side reading that is NOT the isolated cell voltage. No charging/full/USB-presence claim is made. Confirm the physical divider, cell connection and voltage with a meter before relying on it.

## Verification

`tools/test_controls.sh`, `tools/test_sd_audio.sh`, `tools/test_rf.sh` passed. MENU regression was observed red before filling the remaining topbar. Voltage-policy tests cover conversion, calibration-invalid, invalid sample and exact color boundaries. Actual worker host harness exercises calibration failure, ADC error, invalid sample, averaging and 5020ms batch cadence. Real-render tests exercise voltage color boundaries, MENU hitboxes, unchanged button pixels across widest frequency updates, and screen/panel rotation mapping in all five themes and four rotations. Header-update route has a red/green source contract. Host tests are not physical ADC accuracy/touch/audio validation. `git diff --check` passed.

Both final-source silicon builds passed through existing `tools/build_waveshare43.sh` and exported local ESP-IDF. Every flasher_args.json constituent was byte-compared against the merged image at its declared offset: bootloader 0x2000, partition table 0x8000, app 0x10000.

| Image | bytes | SHA-256 |
|---|---:|---|
| dist/p4-sdr-waveshare43-rev1.bin | 1094224 | 160b5e41083f31299620181fb2b9a86682c63cf8a47439814356bf4946afba50 |
| dist/p4-sdr-waveshare43-rev3.bin | 1096992 | 1293fcbcc6a9de0f6f90fbc98c9314581fbbda97abd8fcac671ec883d5520d3f |

These are full images at 0x0; writing them can reset NVS/settings. Not flashed. Measured acquisition is implemented and cross-built, but physical readings and physical MENU position remain **unverified**, not claimed complete on hardware. Next authorized bench step: verify PCB matches the cited schematic, close any battery-lead switch, compare displayed BAT against a meter at J2 across battery-only and USB charging states, and verify topbar on the running rev3 image. No external ADC/gauge is needed for this documented board circuit.

Touched implementation: main/battery_voltage.{c,h}, main/view.c, main/ui.c, main/CMakeLists.txt. Tests: tests/battery_voltage_test.c, tests/battery_worker_test.c, tests/battery_route_test.py, tests/battery_host/**, tools/preview/preview.c, tools/test_controls.sh. Evidence: this report, circuit download/render, preview images, topbar-*.log, regenerated dist binaries/checksum manifest.
