# P4 SDR v1.2.0 — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 port

Fork of [John O'Dell's P4 SDR](https://github.com/John-ODell/esp32-p4-rtl-sdr)
(GPL-3.0; original license and attribution retained).

This branch targets the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3**. It uses the
board's 480×800 ST7701 MIPI-DSI panel and GT911 capacitive touch (no
calibration), and retains the original RTL-SDR, SD card,
ES8311 audio and ESP32-C6 Wi-Fi/BLE code. The display starts in native
portrait and **MENU → LOOK → TURN** cycles through all four 90-degree
orientations (0°/90°/180°/270° clockwise), rotating touch with the picture.
The selection is saved; older `port` portrait/landscape settings are migrated
on load. Landscape draws in software, so it may be slower than portrait.
The user has confirmed all four rotations work on a rev3 board. The previous
floating waterfall VOL/SQL controls flickered on board and the interface was
reported to flash every few seconds. The revised UI stops waterfall blits
112 pixels above the bottom: a static ZOOM −/+ row changes horizontal spans
(2.4/1.2 MHz, 600/300 kHz), and fixed WFM/NFM25/NFM12/AM/USB/LSB mode buttons
sit to the left of VOL/SQL steppers. Footer drawing is cached when unchanged.
This is host-tested and cross-built but **not yet verified on the board**; if
flashing continues, inspect UART logs for stream restarts and USB errors.
The Waveshare header has two tuning arrows and a highlighted STEP selector.
Tap STEP to cycle 100 Hz, 1/5/9/10/12.5/25/50/100 kHz and 1 MHz; hold an
arrow to repeat. The selected step is saved and the readout retains a 100 Hz
digit if any adjustment leaves one. The frequency readout itself is not a look
shortcut; use **MENU → LOOK** to change theme, which is committed immediately.
Phosphor uses only green RGB565.
Use the footer or **MENU → AUDIO** for 3 kHz USB/LSB reception.
The footer **MUTE/UNMUTE** key silences the speaker without losing the saved
volume or interrupting IQ reception; it resets to unmuted after a reboot.
**MENU → RF INPUT** adds Bias-T and TUNER/ADC I/ADC Q. The firmware commands
Bias-T OFF on boot and USB reconnect (unless EEPROM forces it ON), but a failed
USB write leaves power unknown: RF INPUT shows "BIAS ? RETRY OFF" and OFF retries.
A completed USB command does not measure antenna voltage. Bias-T is not persisted;
only enable it for a compatible active antenna/LNA with adequate USB supply. On Blog V4
and V4L the ADC I/Q keys are shown but disabled: HF uses the onboard
upconverter, and forcing direct sampling yields no RF input. Other dongles
require the selected ADC input to be physically wired. Direct mode is persisted.
**MENU → BAND** paginates 30 receive presets with PREV/NEXT. It retains
North American RX presets at 315, 345, 433.92 (the existing 433/70CM slot),
and 915 MHz, and adds AM broadcast (600/1000/1600 kHz), ten shortwave AM
starting points (3.95–21.6 MHz) and CB AM (channels 1/9/19/40), plus
channel 38 USB/LSB. These are tuning/filter templates, not guaranteed active
stations or packet decoders; the V4 HF path remains TUNER/upconverter.
**MENU → SCAN → SENSORS** is an experimental foreground 433.92 MHz decoder for
named Fine Offset WH2 weather and Steelmate TP-S15-family TPMS packets only.
It uses the same USB driver and CU8 IQ ring, pauses audio/waterfall, shows
readings and dropped bytes, and restores the previous frequency on EXIT. The
recorded IQ examples pass host tests at their 250 kS/s source rate. When
replayed at this firmware's 2.4 MS/s rate, WH2 decodes and Steelmate decodes
two of three repeated packets (one fails the strict preamble check). There is
no Blog V4 live-capture or on-board throughput verification yet. This is not
generic rtl_433 support and
must not be relied on for vehicle safety. See [ROADMAP.md](ROADMAP.md) for
later formats and the separate mute/volume/squelch power-cycle persistence task.
SSB currently has a fixed audio gain rather than speech AGC; set SQL to OPEN
(0) because its power-based squelch can mute an already-present voice signal.

**USB host wiring:** the 40-pin J3 header exposes the *same* native high-speed
USB controller as the Type-C **USB OTG** connector: J3 pin 27 is D+ (`USBD_P`),
pin 25 is D− (`USBD_N`), pin 26 is GND, and pins 1/3 are the board's 5 V rail.
The firmware uses this controller in host mode regardless of whether its data
pair is reached through J3 or the Type-C connector; switching to J3 needs **no
firmware change**. Do not connect a host peripheral to both paths at once. If
building a USB-A breakout from J3, verify orientation and 5 V under load and
use a suitable current-limited/fused 5 V feed rather than wiring an unprotected
port; high-speed USB data wiring must be short and paired. The Type-C socket's
VBUS is not a powered host output on the bare board. An externally powered hub
alone may not resolve USB-C host-role/CC wiring; verify the adapter's host role
before relying on that route.

**Hardware requirement:** connect an RTL-SDR dongle to the native USB host data
pair, with separately supplied/protected 5 V power. **User report:** the RTL-SDR
works on J3 with the battery removed and the board powered from a USB host; on
battery alone the board browns out. A suitable additional power controller and
charge circuit will be needed for battery operation. Its electrical design and
current budget remain to be investigated; do not assume the current battery
path is adequate. The old kit's USB-A hub and H3 host jumper do not exist on
this board. Use the **USB TO UART** Type-C port for flashing/serial console. An
antenna is needed for reception. Do not plug the USB-C OTG port directly into
a computer when using it as a host. The firmware's old “USB-A ports / H3 cap”
splash wording is not applicable here.

## Flash

Identify the **P4 silicon revision** first with `python -m esptool --chip
esp32p4 -p PORT flash_id`; the serial output reports a chip
revision. Pick **rev1** for v1.x, **rev3** for v3.1 or newer. The `dist/`
directory is generated by the build script (not tracked in Git). Its full-flash
images include the bootloader, partition table and app, and are flashed at
**offset 0x0**:

```sh
python -m esptool --chip esp32p4 -p PORT -b 921600 write_flash 0x0 dist/p4-sdr-waveshare43-rev3.bin
```

Use `dist/p4-sdr-waveshare43-rev1.bin` if your P4 is revision v1.x. Compare
against `dist/SHA256SUMS-waveshare43` before flashing. A full-image flash can
overwrite saved settings and channel memories. **A previous rev3 full-flash
image was user-flashed on this board: its display and RTL-SDR via J3 worked
with USB-supplied power and no battery, and its four-way rotation update was
also confirmed working by the user.** Its floating audio controls flickered;
this newer fixed-footer revision and SSB have not been verified on hardware.
Rev1 remains cross-compiled and untested.
Inspect the UART console at 115200 baud when testing new functions. The ESP32-C6
may need compatible ESP-Hosted coprocessor firmware for Wi-Fi/BLE scanning;
audio and SD operation have not been verified on this board.

## Rebuild

Install and export ESP-IDF **5.5.5** with `esp32p4` tools and run:

```sh
. "$HOME/esp/esp-idf/export.sh"  # adjust IDF path if needed
./tools/build_waveshare43.sh
```

The `sdkconfig.waveshare43` overlay selects the board and 32 MB flash, while
`sdkconfig.rev1` selects older silicon. To compile only the rev3 image:

```sh
idf.py -B build-waveshare43-rev3 \
  -D SDKCONFIG=build-waveshare43-rev3/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.waveshare43' build
```

Board display/touch setup uses
[`waveshare/esp32_p4_wifi6_touch_lcd_4_3` v1.0.1](https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_4_3),
including revision-aware DSI PHY clock selection. The SD slot and ES8311
pins match the Waveshare [board schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf).
The native 480×800 DPI framebuffer performs asynchronous DMA2D copies; the
adapter waits for completion before reusing its source buffer.
