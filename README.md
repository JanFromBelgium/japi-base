# Japi Base — Jan's Pico Projects Base

A well-documented, hackable retro computer built on a **Raspberry Pi Pico 2
(RP2350)**. Japi Base provides all the basic I/O of a small computer — video,
keyboard, storage and sound — on a single core and a single PIO block, leaving
the **second core and the remaining PIOs completely free for your own
programs**.

The goal is a system that is both educational and genuinely usable: clear
code, honest documentation, and no hidden defects.

> **Status:** the hardware platform (VGA, PS/2 keyboard, SD card, audio) is
> working and verified on real hardware. A code editor and a BASIC are planned
> as next steps.

## Features

- **VGA output** — 1024×768 @ 60 Hz (exact VESA timing). Text mode of
  127 columns × 64 rows, 64 colours for both foreground and background,
  8×12 pixel font (code page CP437, including box-drawing glyphs).
- **Bitmap graphics** — a character-aligned bitmap window overlaid on the text
  screen. The buffer is capped at ~128 KB: up to 416×312 logical pixels at
  scale 1, or an almost full-screen 832×624 via 2×2 pixels at scale 2.
- **PS/2 keyboard** — flexible driver with pluggable layouts
  (AZERTY/QWERTY/QWERTZ); the active layout is chosen in `config.sys`.
- **Storage** — a 360 KB LittleFS "flash floppy" (always available) plus an
  optional micro-SD card when inserted, behind one unified DOS-style file API
  (`A:` = flash floppy, `C:` = SD card).
- **Audio** — PWM stereo output with a built-in 4-channel wavetable synth
  (ADSR envelopes, volume/pan); advanced users can fill the sample buffer
  themselves.
- **Free for your code** — Core 0 and the unused PIOs/peripherals are entirely
  yours; the base I/O engine lives on Core 1 + PIO0.

## Demo application

The bundled firmware (`Japi Base Pico 2/main.c`) cycles through, advancing on
any key:

1. **Showcase** — colour palette, full character set, windows, text colours,
   runtime font redefinition, bar chart, a scale-1 bitmap drawing the four
   synth waveforms, and 4-channel stereo music (Tetris theme).
2. **Bouncing balls** — flicker-free animation on a solid felt background
   (832×624 screen pixels, scale 2).
3. **The Starry Night** — Van Gogh, Floyd–Steinberg dithered into the
   64-colour palette (public domain, Google Art Project).
4. **API quick reference** — live keyboard test and code examples.

## Hardware

All Japi Base I/O sits on the **left** side of the Pico 2 (USB at top); the
right side (GP16–GP28) stays completely free for your projects. VGA is driven
by a weighted-resistor DAC; audio by PWM through a simple RC filter.

| GPIO | Function | Notes |
|---|---|---|
| GP0 | VGA VSYNC | 100 Ω series |
| GP1 | VGA HSYNC | 100 Ω series |
| GP2 / GP3 | Blue LSB / MSB | 1 kΩ / 470 Ω |
| GP4 / GP5 | Green LSB / MSB | 1 kΩ / 470 Ω |
| GP6 / GP7 | Red LSB / MSB | 1 kΩ / 470 Ω |
| GP8 / GP9 | Audio L / R | PWM slice 4 A/B → 1 kΩ+3.3 nF RC + 10 µF |
| GP10–GP13 | SD SCK / MOSI / MISO / CS | SPI1 |
| GP14 / GP15 | PS/2 CLK / DATA | 4.7 kΩ pull-ups to 3.3 V |
| GP16–GP28 | **free** | yours |

Full wiring, DAC voltage levels and the audio filter schematic are in
**`Context Japi`** (the architecture document).
Build the hardware on a breadboard, flash `japi_base.uf2`, and you can see and
hear the demo. **You need a Raspberry Pi Pico 2!**

## Timing & memory budget

- CPU at **260 MHz**, PIO divider 1:1, 4 ticks/pixel → exact **65 MHz** pixel
  clock for 1024×768@60 Hz.
- 806 scanlines × 60 Hz ≈ **5374 CPU cycles per scanline** for the base
  engine (rendering + keyboard + audio).
- Audio: PWM at 40 kHz, sample rate **24 180 Hz** (one sample every two
  scanlines) to keep per-scanline IRQ work bounded.
- Bitmap buffer cap: **128 KB** (`JAPI_BITMAP_MAX_RAM`). The demo's full-screen
  Starry Night uses 416×312 = 129 792 bytes at scale 2.
- The scanline buffers and the whole render path live in RAM, so LittleFS
  flash writes never disturb the video signal.

## Building

Requirements: Raspberry Pi Pico SDK 2.x (`PICO_SDK_PATH` set), CMake, Ninja,
`arm-none-eabi-gcc`. Third-party libraries (FatFs_SPI, pico-lfs/littlefs) are
vendored, so no extra fetching is needed.

```sh
cd "Japi Base Pico 2"
mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

Flash by holding **BOOTSEL** while plugging in the Pico 2, then:

```sh
cp "Japi Base Pico 2/build/japi_base.uf2" /media/<user>/RP2350/
```

## Hello, Japi Base

```c
#include "japi_base.h"

int main(void) {
    japi_init();                                  // clock, VGA, keyboard, SD
    vga_clear(VGA_WHITE, VGA_DARK_BLUE);
    vga_print(2, 4, "Hello from Japi Base!", VGA_YELLOW, VGA_DARK_BLUE);
    japi_play(NOTE_C5, 200);                      // a short beep

    for (;;) {
        if (japi_has_char()) {
            uint16_t k = japi_get_char();
            if (k == JAPI_KEY_ESCAPE) break;
        }
    }
    return 0;
}
```

## Repository layout

| Path | Contents |
|---|---|
| `Japi Base Pico 2/` | Firmware: VGA/keyboard/audio/storage engine + demo app |
| `Japi Base Pico 2/FatFs_SPI/`, `pico-lfs/` | Vendored third-party libraries |
| `tools/` | `gen_starry.py` (image → C array) + source image; audio prototype |
| `Font Editor/` | Linux-side font design tool (BDF ↔ C header) |
| `Context Japi`, `Planning Japi` | Architecture document and project plan |

The previous Pico 1 (RP2040) prototype is archived outside this repository.

## Roadmap

- A keyboard-only code editor (QuickBASIC/Turbo-Pascal look and feel) on Core 0,
  offering intellisense, split-screen editing, find and replace.
- Port a BASIC (MMBasic candidate)
- Integrate my japiZ80 assembler.
- A Z80 emulator
- A build/usage manual once the feature set is stable.

## Credits

- **FatFs_SPI** — SD card over SPI, by carlk3.
- **pico-lfs** — LittleFS for RP2040/RP2350, by Timo Kokkonen.
- **littlefs** — the littlefs project.
- *The Starry Night* (Vincent van Gogh, 1889) — public domain, via Wikimedia
  Commons / Google Art Project.

## License

Released under the **MIT License** — see [`LICENSE`](LICENSE). The vendored
libraries keep their own licenses (see their `LICENSE` / `LICENSE.txt`).
