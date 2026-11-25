# emuZero's icons

Pictures for apps that bring none of their own, named by the app's id (its `.desktop` file
name without `.desktop`). An app's own `Icon=` always wins; a theme can replace any of these with
`icons/ID.png` in its own folder (see `themes/README.md`). Installed in `/usr/share/emuzero/icons/`.

## From M5Stack's launcher (MIT)

These files are copied unchanged from M5Stack's launcher for the CardputerZero,
<https://github.com/CardputerZero/launcher>, commit `57392418bed852c3218c71d18a874fa8a22413a2`,
folder `projects/APPLaunch/APPLaunch/share/images/`. They are under that project's MIT licence,
whose notice is below.

| File here | M5's file | App |
|---|---|---|
| `nz-terminal.png` | `cli_100.png` | Terminal |
| `nz-ssh.png` | `ssh_100.png` | SSH client |
| `nz-netinfo.png` | `ip_panel_100.png` | Network info |
| `nz-options.png` | `setting_100.png` | Options |
| `nz-store.png` | `store_100.png` | Store |
| `nz-snake.png` | `game_100.png` | Snake |
| `camera_app.png` | `camera_100.png` | Camera (when its own picture is missing) |
| `files.png` | `file_100.png` | Files (when its own picture is missing) |
| `keyboard-guide.png` | `Keyboard.png` | Keyboard Guide (its picture comes with M5's launcher, so it is missing without it) |

```
MIT License

Copyright (c) 2026 M5Stack Technology CO LTD

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## emuZero's own (MIT, this project)

Drawn by `src/draw.py` in the same flat style, for the apps M5's set has no picture for:
`nz-diag.png` (Diagnostics), `nz-services.png` (Services), `htop.png` (System monitor),
`nano.png` (Editor), `emuzero-notes.png` (Notes), `nz-wifi.png` (Wi-Fi setup), `nz-poweroff.png` (Power off) and
`emuzero-theme.png` (the Theme tile). To draw them again: `python3 icons/src/draw.py icons`.
