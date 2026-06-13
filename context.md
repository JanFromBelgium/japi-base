# Context for AI-assisted coding on Japi Base

This document is for people who want to work on Japi Base together with an
AI coding assistant (Claude, Cursor, Aider, ChatGPT, or any future tool).
It gives the assistant the project conventions and workflow that aren't
visible from the code alone. For the *what* and *how to build*, read
[`README.md`](README.md) first.

## Repository layout — the short version

The entire firmware lives in `Japi Base Pico 2/` and the public API
fits in two files: `japi_base.h` (engine API) and `third_party_libs.h`
(consolidated FatFs + littlefs + pico-lfs). See the README's
"Repository layout" section for the per-file breakdown.

## Coding conventions

- **Hardware pin assignments** belong in `japi_base.h` (PIN_VSYNC, PIN_SD_*,
  PIN_AUDIO_*, …). That is the single source of truth for the board.
- **Third-party code** goes into `third_party_libs.c` with a per-component
  licence block at the top. Adding a fourth library means appending to the
  same file, not creating a new directory.
- **`japi_base.c/.h/.pio` is the reusable engine.** `demo.c/.h` is a specific
  application built on top. Future applications (an editor, a BASIC, …) go
  into their own `*.c` files alongside `japi_base.*`, never inside it.
- **One concern per file. Consolidate over split.** When a file ends up with
  a single caller, fold it back in.
- **VGA buffer-flip convention:** apps writing to the text buffer must call
  `vga_wait_vblank()` at the end of an update so the engine flips the
  double buffer. Skipping this gives tearing.
- **`config.sys` keyboard layout:** the active PS/2 layout is chosen at
  startup by reading `A:\CONFIG.SYS` from the LittleFS flash floppy.

## Build

```
cd "Japi Base Pico 2"
mkdir -p build && cd build
cmake .. && make -j4
```

Output: `japi_base.uf2` (drag onto the BOOTSEL drive of a Raspberry Pi
Pico 2). The project targets `PICO_BOARD pico2`, SDK 2.2+, and overclocks
to 260 MHz at 1.30 V (set in CMakeLists / boot code).

## Workflow conventions (when contributing changes)

- **Safety-anchor tag first.** Before any non-trivial refactor, create a
  `pre-<change-name>` git tag on `HEAD` so a `git reset --hard` always
  has somewhere to land.
- **Worktrees for isolation.** Bigger refactors are done in a
  `.claude/worktrees/<name>` worktree, then fast-forward-merged into
  `master`. Keep `master` clean and linear.
- **Hardware verify before merge.** The CI is Jan flashing the resulting
  `.uf2` onto real hardware. Build-success alone is not "done"; SD card,
  flash floppy, audio, keyboard and VGA timing all have to be exercised
  on the Pico before a change merges to `master`.
- **No `--no-verify`, no force-push to `master`.**

## Things not to do

- Don't reintroduce the carlk3/FatFs/littlefs as separate subdirectories
  or libraries. They were consolidated for a reason; the consolidation
  is meant to stick.
- Don't enable card-detect on the SD card: the carlk3 driver installs a
  background GPIO interrupt that wrecks the VGA scanline timing.
- Don't rewrite features speculatively. The author's stated quality bar
  is *no known defects*; a one-line bug fix beats a refactor that risks
  one.

## Licence

BSD-3-Clause. The GPL-3 pico-lfs adapter was replaced by Japi Base's own
littlefs flash block device, so the whole work is permissive now.
See [`LICENSE`](LICENSE).
