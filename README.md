# emuZero launcher

A thin front end for [NimbleZero](https://github.com/djkaos81/NimbleZero) on the M5Stack CardputerZero.
It shows the installed apps and opens NimbleZero's own Options, Wi-Fi setup and Store screens; NimbleZero
keeps doing the work underneath. It contains no NimbleZero code; it calls NimbleZero's programs
(`nz-admin`, the Store back end) as separate processes.

Drawn with [LVGL](https://lvgl.io) 9.2.2 (MIT), chosen on measurements in [spike/](spike/README.md).

The same repository holds **emuZero Notes** ([apps/notes/](apps/notes/README.md)), a notes app in the
manner of iPhone Notes that replaces the image's Editor tile. It is a program of its own
(`emuzero-notes`), built alongside the launcher by the same `make` targets.

## Building

LVGL is fetched once and checked against a pinned hash:

    third_party/fetch.sh

One source tree, three builds:

| Command | What it builds | Needs |
|---|---|---|
| `make device` | the CardputerZero program, `out/fbdev/emuzero-launcher` | a NimbleZero Buildroot output (`BR=.../out/compat`) |
| `make desktop` | the same screen in a window on a PC (`--zoom N`, default 3) | SDL2, FreeType and libpng development files |
| `make headless` | draws into memory and writes a PNG after a key script | FreeType and libpng development files |
| `make check` | headless checks against `test/root`: the app list, each layout and status bar state as PNGs in `out/check`, the command lines Enter and O start, App info's Back / Remove / Update paths (`test/bin` stands in for `dpkg-query`, `dpkg` and `sudo`), the themes and the Theme page | as above |

The desktop and headless builds read the device's paths under `EMUZERO_ROOT` (for example
`EMUZERO_ROOT=test/root`), and fall back to the PC's own DejaVu fonts.

    out/headless/emuzero-launcher --keys right,right,enter --shot screen.png

Other options: `--theme NAME` and `--layout carousel|grid` override the saved choice for one run;
`--themes` lists the themes; `--list` prints the apps the home screen would show (id, name, `icon`
or `tile`) and exits. On the device the log (`/tmp/emuzero-launcher.log`) gives the start-up time
of each step up to the first frame.

Key names for `--keys`: `up down left right enter esc del backspace tab home end space comma`, or a
single character. On the device, `--kbd /dev/input/eventN` reads only that keyboard; by default every
keyboard is read, including one plugged in later.

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

Two layouts; the theme names one, and the Theme page can choose either:

- `carousel`: three framed cards, the selected one large in the middle, names under them, a
  position bar; Left/Right step through the apps and wrap around.
- `grid`: pages of 2 x 5 icons with names; arrows move, Left/Right past the page edge flips the
  page.

A layout is one file implementing `struct layout` (`src/layout.h`): build, show a selection, and
where an arrow moves the selection. The home screen keeps the app list, the selection and the keys.

Keys: arrows, or F / X / Z / C (they carry the arrow legends on the CardputerZero keyboard) move;
Enter starts the app; O opens Options; T opens Theme; I or Del opens App info. On a PC build Esc
quits.

## Themes

A theme is a folder with a `theme.conf`, in `~/.local/share/emuzero/themes/` or
`/usr/share/emuzero/themes/`. It sets colours, fonts, the home layout, a wallpaper, how the cards
look, where the status bar sits and how its badges look, and an optional window frame around the
apps (border or 3-D bevel, title bar with stripes and a close box). A new theme needs no code:
[themes/README.md](themes/README.md) lists every key. Two come with the launcher: `minimal` (the
default) and `paper` (a sample: black on white, a grid in a striped window).

The **Theme** tile (or T) opens a panel along the bottom: the theme, and the home layout (the
theme's own, Carousel or Grid). Each change shows at once on the home screen above; Enter keeps it
(in `~/.config/emuzero/settings`), Esc puts back what was there.

## Status bar

Name on the left; on the right a USB badge while the USB controller runs, Wi-Fi bars (dark when the
radio is on but not connected, gone when it is off), the time and the battery level (a lightning
mark on the charger; yellow at 35 % and below, red at 15 %). The values come from the same places
`nz-admin status` and the fuel gauge use: `/sys/class/power_supply/<battery>/capacity` and `status`,
`wlan0`'s UP flag and operstate with the link quality from `/proc/net/wireless`, and whether the
`dwc2` driver is bound to the SoC's USB controller. The bar updates at each new minute and on every
key; nothing polls in between.

## Starting apps

Enter runs the entry's `Exec` (field codes such as `%f` removed) with `/bin/sh -c`, in its own
process group, in `Path` if the entry has one, and waits:

- the app gets the console's foreground; a `Terminal=true` app gets the text console, a graphical
  app's output goes to `/tmp/emuzero-app.log`;
- the screen still goes dark after the screen-off time from Options (`/data/etc/nz/screen-off`,
  default 120 s, 0 = never; read through `sudo -n nz-admin status` when the file is root's alone).
  Dark = backlight off and the display blanked, so the panel sleeps. The first key only wakes the
  screen and the app never sees it;
- Ctrl+Alt+Backspace stops the app (SIGTERM, SIGKILL after 2 s);
- when it ends the home screen is drawn again (about 30 ms on the device), with a short message if
  the app failed or was stopped. The app list is read again, since the Store may have changed it.

The same screen-off time applies on the home screen. The launcher's own messages go to
`/tmp/emuzero-launcher.log`.

## App info

I or Del on an app opens App info: name, description, version, package and size on disk, with the
actions Back (always selected first), Update (only when the Store's index,
`/data/cache/nz-store/catalogue.tsv`, has a newer version) and Remove. Remove asks first with "No,
keep it" selected and names the other apps the same package brings. The package comes from
`dpkg-query -S`; entries no package owns (NimbleZero's own) and held packages (part of the system)
cannot be removed. Remove and Update run `sudo -n /usr/sbin/nz-admin store remove|install <package>`,
the same helper NimbleZero's Store uses.

## Source files

    src/main.c          arguments, LVGL start-up
    src/ui.c            home screen: theme chrome, fonts, keys, starting apps, screen timeout
    src/statusbar.c     the status bar; src/status.c reads battery, Wi-Fi and USB
    src/appinfo.c       App info, Remove and Update
    src/sys.c           reading values from files, running NimbleZero's programs
    src/apps.c          desktop entry scanning
    src/icons.c         icon decoding, scaling and drawn tiles
    src/layout_*.c      the carousel and grid layouts
    src/theme.c         theme folders (theme.conf) and the launcher's settings file
    src/themepage.c     the Theme page
    themes/             the themes that come with the launcher, and the list of keys
    src/paths.c         EMUZERO_ROOT and font lookup
    src/plat_fbdev.c    device: /dev/fb0, our own evdev reader, running apps, screen off and panel
                        sleep; sleeps until a key or a timer is due
    src/plat_sdl.c      desktop window
    src/plat_headless.c memory + key script + PNG
    src/lv_conf.h       LVGL configuration for all three
    spike/              the renderer comparison (Phase 2, row 1)
    test/root/          a pretend device file system for the checks (test/make-icons.py draws its icons)
    test/bin/           stand-ins for dpkg-query, dpkg and sudo used by the checks
    apps/notes/         emuZero Notes, its own program (see its README)

## Licence

MIT, see [LICENSE](LICENSE).
