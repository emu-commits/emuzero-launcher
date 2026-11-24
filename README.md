# emuZero launcher

A thin front end for [NimbleZero](https://github.com/djkaos81/NimbleZero) on the M5Stack CardputerZero.
It shows the installed apps and opens NimbleZero's own Settings, Wi-Fi and Store screens; NimbleZero
keeps doing the work underneath. It contains no NimbleZero code; it calls NimbleZero's programs
(`nz-admin`, the Store back end) as separate processes.

Drawn with [LVGL](https://lvgl.io) 9.2.2 (MIT), chosen on measurements in [spike/](spike/README.md).

## Building

LVGL is fetched once and checked against a pinned hash:

    third_party/fetch.sh

One source tree, three builds:

| Command | What it builds | Needs |
|---|---|---|
| `make device` | the CardputerZero program, `out/fbdev/emuzero-launcher` | a NimbleZero Buildroot output (`BR=.../out/compat`) |
| `make desktop` | the same screen in a window on a PC (`--zoom N`, default 3) | SDL2, FreeType and libpng development files |
| `make headless` | draws into memory and writes a PNG after a key script | FreeType and libpng development files |
| `make check` | headless smoke test | as above |

The desktop and headless builds read the device's paths under `EMUZERO_ROOT` (for example
`EMUZERO_ROOT=test/root`), and fall back to the PC's own DejaVu fonts.

    out/headless/emuzero-launcher --keys right,right,enter --shot screen.png

Key names for `--keys`: `up down left right enter esc del backspace tab home end space comma`, or a
single character.

In the NimbleZero image the program is built by the `emuzero-launcher` Buildroot package
(`external/package/emuzero-launcher` in the emuZero fork of NimbleZero). For a working tree, put this in
`out/<config>/local.mk`:

    EMUZERO_LAUNCHER_OVERRIDE_SRCDIR = /path/to/emuzero-launcher

## Layout

    src/main.c          arguments, LVGL start-up
    src/ui.c            screens
    src/paths.c         EMUZERO_ROOT and font lookup
    src/plat_fbdev.c    device: /dev/fb0 + evdev; sleeps until a key or a timer is due
    src/plat_sdl.c      desktop window
    src/plat_headless.c memory + key script + PNG
    src/lv_conf.h       LVGL configuration for all three
    spike/              the renderer comparison (Phase 2, row 1)

## Licence

MIT, see [LICENSE](LICENSE).
