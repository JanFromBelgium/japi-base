# Japi Base host simulator

A dependency-free Linux host backend for the Japi Base platform API
(`japi_base.h`). The same editor and application source that runs on the
Raspberry Pi Pico 2 compiles and runs unchanged here, against an ANSI terminal
instead of the real VGA + PS/2 hardware — so logic can be developed and tested
on a dev machine before touching hardware.

It implements the platform symbols (`vga_*`, `japi_file_*`, `japi_bitmap_*`,
keyboard input) with:

- a truecolour ANSI text renderer for the 127×64 character screen, and
- a **half-block** compositor for the bitmap overlay: each character cell shows
  two vertical preview pixels (`▀`, foreground over background) in 64 colours.
  The logical bitmap buffer is byte-for-byte identical to the hardware, so
  pixel/drawing code is verified the same way on both; only the on-screen
  preview is approximate.

This is the single canonical simulator and part of the Japi Base platform: the
Japi Base editor and the JBB BASIC interpreter both link `japi_sim.c` from here,
so it is published and maintained here alongside the rest of the platform.

## Build and run

Needs a terminal of at least 127×65.

```sh
make test            # headless byte-exact bitmap test (AddressSanitizer)
make sim_gfx_demo && ./sim_gfx_demo    # visual graphics demo (any key quits)
```

Part of the Japi Base platform, published and maintained in this repository.
