# P4 SDR — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

ESP-IDF port of [John O'Dell's P4 SDR v1.2.0](https://github.com/John-ODell/esp32-p4-rtl-sdr/tree/v1.2.0)
for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3** with its 480 × 800 ST7701
MIPI-DSI screen and GT911 touch controller. It adapts the original RTL-SDR
receiver to this integrated display board.

The inherited application includes a spectrum/waterfall receiver, AM/FM audio,
RDS, ADS-B aircraft display, channel memories, microSD logging, and Wi-Fi/BLE
scanning through the on-board ESP32-C6. Features beyond the display have not
all been checked on this board. **MENU → LOOK → TURN** cycles the screen and
touch through 0°, 90°, 180° and 270° clockwise from the native portrait
orientation; the choice is saved for the next boot. Four-way rotation has been
confirmed working on the user's rev3 board.

Choose a color scheme through **MENU → LOOK**. The frequency display across the
top no longer cycles themes when tapped. A chosen look is committed to NVS
immediately (rather than after the usual three-second settings delay) and is
loaded for the splash screen on the next boot, including after power removal.
This behavior has host tests and cross-builds; it has not been rechecked on the board.

The waterfall stops above a fixed **112-pixel footer**: a **ZOOM −/+** row
changes the spectrum/waterfall's horizontal span through 2.4 MHz, 1.2 MHz,
600 kHz and 300 kHz (hold to repeat). The vertical dB scale is still under
**MENU → RANGE**. Six mode buttons
(WFM, NFM25, NFM12, AM, USB, LSB) are on the left and **MUTE/UNMUTE**, **VOL −/+**
and **SQL −/+** on the right. Mute silences the speaker without changing the
saved volume or stopping the RF stream; tap again to restore the same level.
Hold the volume/squelch keys to repeat (mute is a single toggle). Volume ranges from
0 (off) to 10 and squelch from 0 (open) to 9; **MENU → AUDIO** also offers
these modes and settings. The two header arrows tune by the increment shown on
the highlighted **STEP** button. Tap STEP to cycle **100 Hz, 1, 5, 9, 10, 12.5,
25, 50, 100 kHz and 1 MHz**; hold either arrow to repeat. The chosen step is
saved. The frequency readout shows the 100 Hz digit when needed; tune SSB to
the suppressed-carrier frequency. Phosphor now uses only the green
RGB565 channel, including its splash and status messages.

The older floating audio controls were reported to flicker on hardware, and
the interface was reported to flash every few seconds. The static footer and
bounded waterfall pass host tests and cross-builds, but **their on-board effect
is not yet confirmed**; if flashing remains, capture the UART log around the
event (especially stream restart and USB error messages). **Set SQL to OPEN
(0) for SSB**: its nonzero power-based squelch may mute speech and does not
discriminate sidebands.

**MENU → RF INPUT** adds Bias-T OFF/ON and direct-ADC TUNER/I/Q choices.
Bias-T supplies about 4.5 V to a **compatible active antenna or LNA** via
the dongle's antenna socket; the firmware commands OFF after every boot or USB
reconnection and does not store ON in settings. A failed USB command leaves
antenna power **unknown**, not confirmed OFF: RF INPUT shows "BIAS ? RETRY OFF"
and its OFF key retries the write. A successful USB write does not measure
antenna voltage; check the socket with a meter if safety depends on it.
Do **not** turn it on with a DC-shorted antenna,
and check your USB power budget—battery operation with this board/dongle has
already browned out. If the dongle's EEPROM forces Bias-T ON, the OFF key is
disabled and the EEPROM setting must be changed separately. The RF page shows
ADC I/Q choices but disables them for the **RTL-SDR Blog V4/V4L**, whose HF
path uses a built-in upconverter instead of direct sampling; simply tune the
V4 to the HF frequency in TUNER mode. On other dongles, I/Q only works if the
corresponding ADC input is physically wired. Direct mode, unlike Bias-T, is
saved and reapplied after device reopen. See the [V4 user's guide](https://www.rtl-sdr.com/v4/).

**MENU → BAND** includes North American receive presets at 315 MHz and
345 MHz (low-power Part 15 devices), 433.92 MHz (the existing 433/70CM slot),
and 915 MHz (within the 902–928 MHz ISM band). The menu is paged (**PREV/NEXT**)
and adds AM broadcast starting points at 600/1000/1600 kHz, ten shortwave AM
starting points from 3.95 to 21.6 MHz, and CB channels 1/9/19/40 in AM plus
channel 38 in USB and LSB. Presets tune a **receive-only** frequency and choose
an audio demodulator/filter; they do not guarantee a station or legal authority
to transmit. The Blog V4's built-in HF upconverter remains in **TUNER** mode.
ISM presets are **not** packet decoders. **MENU → SCAN → SENSORS** is a separate,
experimental foreground decoder at 433.92 MHz for **Fine Offset WH2** weather
and **Steelmate TP-S15-family** TPMS packets only. It uses the existing RTL-SDR
USB/IQ ring; audio and waterfall pause while it listens. EXIT restores the
previous frequency and audio. The screen shows decoded IDs/readings, a decoder
load estimate and dropped-IQ bytes; any dropped ring span resets both packet
parsers. Displayed packet rows expire after 60 seconds, while counts remain
session totals. This does **not** decode arbitrary TPMS, weather sensors, or ISM traffic.
Known recorded CU8 examples pass host tests at 250 kS/s. When replayed at
2.4 MS/s, WH2 decodes and Steelmate decodes **two of three** repeated packets;
the third fails a strict preamble check. Live Blog V4 sensitivity, USB
throughput, false-positive behavior and decoding of your own devices have
**not been verified on hardware**. Do not
rely on this experimental TPMS screen for vehicle safety decisions.

Mute is currently transient and volume/squelch use a delayed settings save.
Restoring all three after an immediate power-off is a separate unchecked
[roadmap task](ROADMAP.md); do not assume that behavior is implemented here.

## Hardware status

| Image | P4 revision | Status on this board |
| --- | --- | --- |
| `p4-sdr-waveshare43-rev3.bin` | v3.1 and newer | **User-flashed older image:** display, RTL-SDR on J3 with USB-supplied power and no battery, and four-way rotation worked. The old floating controls flickered; the new footer and SSB have not yet been verified on board. |
| `p4-sdr-waveshare43-rev1.bin` | v1.x | Cross-compiled; not tested on this board. |

**Battery-power limitation (user report):** with the RTL-SDR connected via J3,
the board browns out when running from its battery. The same setup works with
the battery removed and the board powered from a USB host. An additional power
controller/charge circuit will be required for battery operation; its design and
current budget have **not** been established yet. Do not assume the board's
existing battery path can power the display and dongle together.

The dongle was not detected through a **passive USB-C adapter** on the port
marked **USB**, which does not source USB VBUS in the bare-board setup. J3 works;
that observation does not validate USB-C host-role cabling. Audio, SD logging
and wireless scanning are not being claimed as hardware-verified here.

## USB host connections

The port marked **USB TO UART** is for flashing and serial logs; it cannot host
the RTL-SDR. The port marked **USB** and the J3 header's high-speed USB pins
share the *same* ESP32-P4 USB controller. The existing firmware uses that
controller in host mode for either connection—**no rebuild or firmware switch
is required** to use J3.

| J3 40-pin header | Signal |
| --- | --- |
| 27 | USB D+ (`USBD_P`) |
| 25 | USB D− (`USBD_N`) |
| 26 | GND |
| 1 or 3 | Board 5 V rail |

Verify the header orientation against the board silkscreen and
[Waveshare pinout](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3#pinout-definition).
J3 pins 21/23 are a **different**, full-speed USB pair and are not the native
high-speed host data pins used by this firmware. Do not connect a peripheral
to both J3 and the **USB** Type-C socket at the same time. A J3-powered USB-A
breakout needs a suitable current-limited/fused 5 V supply, common ground and
short, paired high-speed data wiring; check the 5 V rail under load first.
Do not bridge the header's 5 V directly into the Type-C connector. A powered
hub alone may not resolve the Type-C host-role/CC wiring. See
[board notes](WAVESHARE-43.md) for additional caveats.

## Flash and build

The firmware images are **full flash images written at address `0x0`**. Match
the image to your P4 silicon revision; a full flash can overwrite saved
settings and memories. `dist/` contains locally generated binaries and is
ignored by Git; cloning this repository alone does not download them.

```sh
python -m esptool --chip esp32p4 -p PORT flash_id
python -m esptool --chip esp32p4 -p PORT -b 921600 write_flash 0x0 dist/p4-sdr-waveshare43-rev3.bin
```

Use `dist/p4-sdr-waveshare43-rev1.bin` instead only for a v1.x chip. Connect
the board's **USB TO UART** port to your computer for flashing and for a
115200-baud serial console. Check images against `dist/SHA256SUMS-waveshare43`.

To build both revisions, install and export **ESP-IDF 5.5.5** with ESP32-P4
tools, then run:

```sh
. "$HOME/esp/esp-idf/export.sh"  # adjust to your ESP-IDF installation
./tools/build_waveshare43.sh
```

The build script generates both images and checksums in `dist/`. See
[WAVESHARE-43.md](WAVESHARE-43.md) for board-specific build settings and
troubleshooting. For the ADS-B map generator, see `tools/mapgen/`.

## Credits and license

Original application: [John O'Dell's P4 SDR](https://github.com/John-ODell/esp32-p4-rtl-sdr).
Its ADS-B position tracking incorporates work by
[John Stockdale](https://github.com/jstockdale/T-Display-P4/tree/adsb).
GPL-3.0-or-later; see [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
