# Renderer spike (Phase 2, row 1)

Two programs draw the same launcher screen (status bar, three-app carousel, app name, position) from the
device's real `.desktop` files, so the renderer can be chosen on numbers:

- `spike-lvgl`: LVGL 9.2.2, fbdev display driver, evdev keypad, FreeType (DejaVu), libpng.
  `--mode default` is the usual LVGL loop; `--mode tuned` reads the keyboard in event mode with `poll()` and
  pauses the display refresh timer until something is invalidated.
- `spike-hand`: about 200 lines of hand-written drawing. It uses an RGB565 back buffer copied to `/dev/fb0`, FreeType, and libpng (icons are scaled once at load).
- `tstart PROG`: runs PROG with `T0_NS` set just before `execve`, so first-frame times include the loader.

Build with `../third_party/fetch.sh && make` (needs the NimbleZero Buildroot output for the toolchain).
On the device, pause nz-launcher (`kill -STOP`), then run `./tstart ./spike-lvgl --bench 50 --idle 30`.

## Results (CardputerZero, 2026-11-24, warm page cache)

| | hand-written | LVGL default loop | LVGL tuned loop |
|---|---|---|---|
| First frame after exec | 13–15 ms (93 ms cold) | 21–22 ms | 21–25 ms |
| Full redraw | 0.7 ms | 1.8 ms | 1.7 ms |
| Idle CPU | 0.06 % | 0.08 % | 0.004 % |
| Idle wake-ups | 0.4/s | 30/s | 0.1/s |
| Battery current, 40 s, screen on, USB off (nothing running: 202 mA, then 201) | 201 mA | 202 mA | 200 mA |
| Max RSS | 3.3 MB | 3.6 MB | 3.6 MB |
| Stripped binary | 22 KB | 596 KB | 596 KB |

Both renderers draw the same picture, and both are far inside the 150 ms first-frame target. Idle current is the same
within the gauge's noise (±2 mA), even with LVGL's default 30 wake-ups a second.

**Choice: LVGL, with the tuned loop.** It costs about 8 ms more at start and 574 KB more binary. In return it brings
widgets, layouts, font handling, styles (themes) and animation, which the hand-written renderer would have to grow
one by one. The tuned loop (input in event mode, refresh timer paused while nothing is invalid) keeps the process
asleep between keys and clock changes, so it stays out of the way of the panel-sleep idle.
