# emuZero Notes

Notes for the CardputerZero, in the manner of iPhone Notes: open it and type, find any note
by typing a few letters, checklists in any note, copy and paste that's easy to learn. Every
note is a plain Markdown file in `~/Notes`, saved as you type. It replaces the image's Editor
(nano) tile; nano stays installed for the Terminal.

It lives in the launcher's repository but is its own program (`emuzero-notes`), with its own
small platform layer: it needs every modifier key (Shift, Ctrl, Alt and the Fn and Sym layers),
which LVGL's keypad cannot carry. It shares LVGL, the themes (it follows the theme chosen in
the launcher) and the font paths with the launcher. The design and its reasons are in
`NOTES-APP-PLAN.md` in the cardputerzero-os folder.

## Using it

- **The list** is a search box: type and only matching notes stay (title and text, any
  order, case and accents ignored). Enter opens the note; with no match, Enter makes a note
  with that title. Pinned notes come first, then Today, Previous 7 Days, and so on.
- **A note** opens in **View mode**: the Markdown drawn, plain letters move a highlight
  (F / X up and down, L / M page), Space ticks a checklist item, Enter or E edits that line.
  **Edit mode** is for typing; Esc goes back to View mode (the iPhone's "Done"), Esc again to
  the list. Ctrl+E switches either way.
- **Arrows** are on the Fn layer: Fn+F/X/Z/C, Fn+K/N Home/End, Fn+L/M PgUp/PgDn,
  Fn+Backspace Delete. Tap Fn for one key, double-tap it to lock.
- **Checklists**: Ctrl+L turns lines into items and back, Ctrl+Enter ticks, Enter starts the
  next item, Enter on an empty item ends the list, `[] ` at a line's start makes an item.
- **Copy and paste**: Ctrl+C / Ctrl+X take the whole line when nothing is selected;
  Ctrl+Space selects more (word, line, block, all); Ctrl+V pastes; Ctrl+Shift+V picks from the
  last 20 copies. `nz-clip` shares the clipboard with the shell: `echo hi | nz-clip`,
  `nz-clip -o`.
- **Tap Alt** for a menu of everything, by name. **Fn+H** shows the keys for the screen
  you're on; both are made from the one key table in `keys.c`. **Ctrl+G** opens the Markdown
  guide (`guide.md`).
- `emuzero-notes FILE` edits any one file (the old Editor's job) and quits when it's closed;
  CRLF line ends and a byte-order mark are kept.

## Files

```
~/Notes/Groceries.md        one note; named after its first line, renamed when you leave it
~/Notes/.Trash/             Recently Deleted (30 days; restore from the list's menu)
~/Notes/.history/           each note as it was when opened ("Revert to when opened")
~/Notes/.notes-state        pins, the note to reopen, settings (deleting it loses nothing else)
~/.local/state/emuzero/clipboard/history     the last 20 copies
```

Saving writes a temporary file, fsyncs it and renames it over the note, so the power switch
can never leave half a file. A note changed elsewhere (SSH, a PC) while open is never
overwritten: theirs is kept as "TITLE (other copy).md". `NOTES_DIR` uses another folder.

## Building and testing

From the repository root, as for the launcher:

| | |
|---|---|
| `make device` | `out/fbdev/emuzero-notes` for the CardputerZero (with the launcher) |
| `make desktop` | `out/sdl/emuzero-notes`, a window (F1 is Help, the Menu key or a tap of Alt the menu) |
| `make headless` | `out/headless/emuzero-notes`, plays key scripts and writes PNGs |
| `make notes-test` | unit tests (buffer, undo, Markdown, names, saving), with sanitizers |
| `make check` | the launcher's checks, then `apps/notes/test/check.sh`: key scripts played against a notes folder of their own, the files compared with what the keys should make, screens in `out/check-notes` |

Headless scripts (`--script FILE`, see `nplat_headless.c`): `type TEXT`, `key ctrl+shift+left`,
`wait MS` (virtual time, for autosave), `shot FILE.png`, `sh COMMAND`, `quit`.

`NOTES_TIMING=1` on the device logs key-to-frame times to the app log
(`/tmp/emuzero-app.log`). Measured on the device (headless build, every key drawn): first
frame and quit 0.11 s; typing 1.7 ms a key in a 50 KB note, 3 ms in a 1 MB note; 4–11 MB RSS.

## Code

| File | Job |
|---|---|
| `main.c` | start-up, signals (save, then quit), reopening the last note, routing commands |
| `nplat_*.c`, `keyspec.c` | screen and keys for the device (evdev with every modifier), desktop (SDL) and headless |
| `textbuf.c` | gap buffer, UTF-8 helpers |
| `doc.c` | the note's lines, undo/redo (grouped by word, one step per command) |
| `md.c` | which kind each line is; inline marks for View mode |
| `view.c` | wrapping by real glyph widths, cursor and hit mapping, drawing both modes; only lines on screen are laid out |
| `editor.c` | a note: moving, typing, checklists, selection, copy/paste, find, autosave |
| `list.c` | the list: groups, search, pins, Recently Deleted |
| `store.c` | the folder: atomic saves, names from titles, trash, history, state file |
| `search.c`, `clip.c`, `keys.c`, `ui.c`, `fonts.c`, `draw.c` | search, clipboard, the key table, bar and overlays, theme fonts and colours, drawing helpers |

MIT, like the launcher. Written from scratch: the two projects it learned from
(micro-journal-linux, Tiny-Journal) gave ideas only, no code.
