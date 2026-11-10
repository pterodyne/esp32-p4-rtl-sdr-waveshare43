# P4 SDR — Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

ESP-IDF port of [John O'Dell's P4 SDR v1.2.0](https://github.com/John-ODell/esp32-p4-rtl-sdr/tree/v1.2.0)
for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3** with its 480 × 800 ST7701
MIPI-DSI screen and GT911 touch controller. It adapts the original RTL-SDR
receiver to this integrated display board.

The inherited application includes a spectrum/waterfall receiver, AM/FM audio,
RDS, ADS-B aircraft display, channel memories, microSD logging, and Wi-Fi/BLE
scanning through the on-board ESP32-C6. Features beyond the display have not
all been checked on this board.

## Hardware status

| Image | P4 revision | Status on this board |
| --- | --- | --- |
| `p4-sdr-waveshare43-rev3.bin` | v3.1 and newer | **User flashed it successfully; display works.** RTL-SDR reception is not yet verified. |
| `p4-sdr-waveshare43-rev1.bin` | v1.x | Cross-compiled; not tested on this board. |

The RTL-SDR was **not detected through a passive USB-C adapter**: the native
USB-C port marked **USB** does not provide the dongle's 5 V VBUS in the
bare-board setup. This is a hardware power/host-cabling issue to resolve before
calling the SDR path tested. Touch, audio, SD logging and wireless scanning are
not being claimed as hardware-verified here.

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
