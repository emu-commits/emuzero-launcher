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
| `make check` | headless checks against `test/root`: the app list, then each layout after a key script (PNGs in `out/check`) | as above |

The desktop and headless builds read the device's paths under `EMUZERO_ROOT` (for example
`EMUZERO_ROOT=test/root`), and fall back to the PC's own DejaVu fonts.

    out/headless/emuzero-launcher --keys right,right,enter --shot screen.png

Other options: `--layout carousel|grid` picks the home screen layout; `--list` prints the apps the
home screen would show (id, name, `icon` or `tile`) and exits.

Key names for `--keys`: `up down left right enter esc del backspace tab home end space comma`, or a
single character.

In the NimbleZero image the program is built by the `emuzero-launcher` Buildroot package
(`external/package/emuzero-launcher` in the emuZero fork of NimbleZero). For a working tree, put this in
`out/<config>/local.mk`:

    EMUZERO_LAUNCHER_OVERRIDE_SRCDIR = /path/to/emuzero-launcher

## The home screen

Apps come from desktop entry files in `/usr/local/share/applications`, `/usr/share/applications`
and `/usr/share/APPLaunch/applications` (M5's launcher and the Store), listed together by name. An
entry is left out when it is hidden (`Hidden`, `NoDisplay`), not an application, or marked for other
launchers (`OnlyShowIn` without `emuZero`, or `NotShowIn=emuZero`); the same file name in two places
is listed once (the earlier directory wins). Icons are PNG files (absolute, relative to
`/usr/share/APPLaunch` for APPLaunch entries, or a `hicolor` theme name), decoded and scaled once per
size; an app without a readable icon gets a drawn tile with its first letter.

Two layouts, chosen with `--layout` (later a setting and part of the theme):

- `carousel`: three framed cards, the selected one large in the middle, names under them, a
  position bar; Left/Right step through the apps and wrap around.
- `grid`: pages of 2 x 5 icons with names; arrows move, Left/Right past the page edge flips the
  page.

A layout is one file implementing `struct layout` (`src/layout.h`): build, show a selection, and
where an arrow moves the selection. The home screen keeps the app list, the selection and the keys.

## Source files

    src/main.c          arguments, LVGL start-up
    src/ui.c            home screen: status bar, fonts, keys, layout switching
    src/apps.c          desktop entry scanning
    src/icons.c         icon decoding, scaling and drawn tiles
    src/layout_*.c      the carousel and grid layouts
    src/theme.h         colours and layout (one built-in theme for now)
    src/paths.c         EMUZERO_ROOT and font lookup
    src/plat_fbdev.c    device: /dev/fb0 + evdev; sleeps until a key or a timer is due
    src/plat_sdl.c      desktop window
    src/plat_headless.c memory + key script + PNG
    src/lv_conf.h       LVGL configuration for all three
    spike/              the renderer comparison (Phase 2, row 1)
    test/root/          a pretend device file system for the checks (test/make-icons.py draws its icons)

## Licence

MIT, see [LICENSE](LICENSE).
