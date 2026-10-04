# WroomTale

**English** · [Français](README.fr.md)

An RFID story and sound box for the **ESP32-WROOM-32**: 4 MB of flash, no
PSRAM. Inspired by [ESPuino](https://github.com/biologist79/ESPuino), rewritten
to fit those constraints.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="hardware/img/wiring-dark.png">
  <img alt="Wiring plan: a 38-pin ESP32-DevKitC V4 in the middle, USB socket at the bottom. On the left the three buttons to GND, the MAX98357A and its 470 µF capacitor, the WS2812B strip and the 18650 + 134N3P power supply. On the right the microSD reader on SPI and the PN532 on I2C." src="hardware/img/wiring-light.png">
</picture>

## Features

- One RFID tag → one folder of MP3s on the SD card, played in a loop.
- Branching stories: one audio clip per node, the next one picked with the
  buttons. Content written ahead of time, no network at run time.
- WS2812B strip: VU meter during playback, one hue per tag, one colour zone per
  story choice.
- Web configuration portal, in English or French, to bind tags without a
  serial console.
- A music-box jingle at start-up, synthesised and embedded in flash.
- 18650 battery gauge read from the discharge curve.

## Hardware

| Peripheral | Signal | GPIO | Without it |
|---|---|---|---|
| MAX98357A (I2S) | BCLK / LRC / DIN | 26 / 27 / 25 | no sound; undetectable, the bus is one-way |
| SD card (SPI) | SCK / MISO / MOSI / CS | 18 / 21 / 19 / 5 | only the jingle and beeps play; reported in the log |
| PN532 (I2C) | SDA / SCL / RESET | 17 / 16 / 4 | no tags; reported in the log |
| Buttons (to GND) | PREV / PLAY / NEXT | 32 / 33 / 14 | everything goes through the console or the portal |
| WS2812B strip | DIN | 13 | no visual feedback |
| Battery sense | divider on B+ | 35 | gauge at zero, `batt` reports no reading |

Detailed wiring and Perma-Proto layout:
**[docs/materiel.md](docs/materiel.md)**. Source of the plan:
[`hardware/wiring.html`](hardware/wiring.html).

Only the ESP32 is required to boot: each missing module drops its feature
without blocking start-up.

## Getting started

```bash
cd firmware
pio run -t upload         # flash over USB
pio device monitor        # serial console, 115200
```

SD card: one folder per album at the root, filled with `.mp3` files. Binding a
tag from the console:

```
tags                             list the bindings
bind <uid> /MyAlbum              a folder on the card
bind <uid> builtin:boot          the embedded jingle
```

Without a console: **holding PLAY for 2 s at start-up** opens the `WroomTale`
WiFi access point (password `wroomtale`), which stops after 5 min.

Full commands and settings: **[docs/console.md](docs/console.md)**.

Short press: pause/resume, next track, previous track.
Long press: volume +/-, stop on PLAY.

## Building

```bash
pio run -d firmware -t upload                      # the box
pio run -d firmware -e wroomtale-volume -t upload  # buttons set on the volume
pio run -d firmware -e bt -t upload                # A2DP spike
```

## Technical choices

| Topic | Decision | Reason |
|---|---|---|
| Audio lib | `earlephilhower/ESP8266Audio` | `ESP32-audioI2S` no longer supports the ESP32 without PSRAM; ESP8266Audio decodes in ~30 KB of heap |
| Decoder | Helix in IRAM | gapless playback without PSRAM |
| WiFi | on only in config mode | the WiFi tasks preempt the decoder; the radio draws ~3× the idle current |
| OTA | none | 4 MB leaves no room for two app partitions |
| Web UI | PROGMEM | no SPIFFS partition, 3.75 MB left to the application |
| Cores | audio on 1, RFID/buttons on 0 | PN532 polling (50 ms timeout) would starve the decoder |
| LEDs | RMT peripheral | WS2812B at 800 kHz, no interrupt tolerated |

## Documentation

In French.

| | |
|---|---|
| [docs/materiel.md](docs/materiel.md) | wiring, Perma-Proto, battery, LED strip |
| [docs/console.md](docs/console.md) | serial commands, web portal, NVS settings |
| [docs/histoires.md](docs/histoires.md) | story format, text-to-speech, tools |

## License

[GPL-3.0](LICENSE).
