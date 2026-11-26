# emuZero Wiki

An offline reference for the CardputerZero: type a few letters, read comfortably. One search box
over the **survival guides** of the [CYD Survival Wiki](https://github.com/everyday3d/esp32-cyd-survival-wiki)
(118 guides in 15 topics, their pictures and their "What is happening?" questions), the **US Army
Survival Manual** (FM 21-76, its 23 chapters) and, when its pack is on the device, **Simple English
Wikipedia** (267,115 articles). Maps from the CYD project are left out on purpose.

It lives in the launcher's repository and is built on Notes' modules: the same screen and keys
layer, fonts, theme colours, text sizes and the View-mode renderer, so an article reads like a
note in View mode. The design and its reasons are in `WIKI-APP-PLAN.md` in the cardputerzero-os
folder.

## Using it

- **The search box** (home): with nothing typed it shows the shelves: *What is happening?*,
  *Survival guides*, *Army Survival Manual*, *Recently read*, *Bookmarks*, *Wikipedia* (or *Get
  Wikipedia* when its pack is missing). Every key searches titles: survival first, then
  Wikipedia (the exact title first, then titles that start with what you typed, then titles with
  a word that does). **Enter** opens the highlighted row; **Ctrl+Enter** (or Shift+Enter, or the
  last row) searches **inside** the articles. Esc clears the box; Esc on an empty box quits.
- **Reading**: the plain letters move as in Notes' View mode: F / X a line (a paragraph taller
  than the screen scrolls before the highlight moves on), L / M or Space a page, K / N top and
  end. **Enter** follows the highlighted line's link (a chooser when it has several) or shows its
  picture on the whole screen (+ or Enter: actual size, arrows move). **C** lists the sections,
  **/** finds, **B** bookmarks, **Ctrl+C** copies the article's text. **Esc** or Backspace goes
  back: the previous article, then the list, then the search box.
- **What is happening?** (Ctrl+T): press 1 to 5; the answers lead to the next question or open
  the guide. Back returns to the question.
- Typing in a list goes straight back to the search box with that letter. **Tap Alt** for a menu
  of every command; **Fn+H** shows the keys of the screen you're on; both come from `wkeys.c`.
  **Fn+1** changes the text size.

## Packs

A pack (`.wpack`) is one SQLite file, read-only, made on a PC by `tools/build-pack.py` (Python
standard library only; the same input gives the same file):

| Pack | Made from | Size | Where |
|---|---|---|---|
| `survival.wpack` | `content/survival/` (copied from the CYD Survival Wiki at `06256dc`, MIT; FM 21-76 is public domain; see its `SOURCE.md`) | 1 MB | kept built as `packs/survival.wpack`; in the image: `/usr/share/emuzero/wiki/` |
| `simplewiki.wpack` | `wiki-slim.db` (Simple English Wikipedia; its snapshot date is unknown) | 234 MB | `~/Wiki/` or `/data/wiki/`, copied separately |

The survival pack is kept built in the repository so that an image build needs no Python with
`sqlite3` (Buildroot's host Python is built without it); `make check` fails when it no longer
matches `content/survival/`, and `make wiki-pack-update` makes it again.

```
apps/wiki/tools/build-pack.py survival apps/wiki/content/survival survival.wpack   (make wiki-pack-update does this)
apps/wiki/tools/build-pack.py simplewiki ~/Downloads/wiki-slim.db simplewiki.wpack (about 3 minutes, 450 MB of memory)
apps/wiki/tools/build-pack.py check PACK...                                        (every link, picture and triage answer resolves)
```

Inside: `articles` (Markdown bodies, zlib-compressed one at a time; links are `[text](id:N)`,
pictures `![alt](img:NAME)`), `images` (the JPEGs as they are), `triage`, `categories`,
`shelves`, `meta` (name, source, licence, the footer under every article). The image's
`libsqlite3` is built without FTS (only the `sqlite3` shell has it), so a large pack carries its
own word index in plain tables: `tw` (title words) and `bw` (every word of the text), each word's
article ids as ascending varint deltas, plus `top`: the 30 best titles for every one-to-three
letter start. Article ids follow a score (the article's length and how many other articles'
"Related pages" point to it, both logarithmic), so the first ids that match are the best ones.
The survival pack is small enough to search in memory.

Getting Wikipedia onto the device is described on the app's *Get Wikipedia* page: one `ssh` line
that copies it under a temporary name and renames it when complete. A download from a published
release waits for the owner (WIKI-APP-PLAN.md §9 D3).

## Building and testing

From the repository root, as for the launcher; the Wiki needs SQLite and zlib (the image has both;
a desktop build needs their development files, found through `pkg-config`):

| | |
|---|---|
| `make device` | `out/fbdev/emuzero-wiki` for the CardputerZero (with the launcher and Notes) |
| `make desktop` / `make headless` | a window on this PC / plays key scripts and writes PNGs |
| `make wiki-pack` / `make wiki-pack-update` | the survival pack built and checked, and compared with the kept one / copied over it |
| `make check` | the launcher's and Notes' checks, then `apps/wiki/test/check.sh`: key scripts against the survival pack and a small Wikipedia pack made from `test/fixture.py`; what each screen showed (`WIKI_TRACE`) is checked, screens are kept in `out/check-wiki` (every theme too) |

`WIKI_DIR=dir1:dir2` looks for packs there instead of the usual folders; `WIKI_TIMING=1` logs
search and article times to stderr (the app log, `/tmp/emuzero-app.log`, on the device).

Measured on this PC (Intel i7-6700HQ, the 234 MB pack): a title search per key 0–2 ms;
opening "United States" 8 ms, the largest article (243 KB) 9 ms; search inside articles 4–40 ms,
most of it reading the first 60 matches to order them; 7 MB of memory at start, 12 MB after
searches, that article and a picture at full size.

Measured on the CardputerZero (2026-11-26, both packs, the 234 MB one on the SD card), cold
(page cache dropped) / warm: opening the packs 38 / 9 ms; a title search per key mostly
1–30 / 1–10 ms (100 ms at worst, cold); "United States" 10 / 5 ms, "List of people from Texas"
(3,635 lines) 26 / 14 ms; search inside "purify water" 118 / 94 ms, "how to start a fire"
351 / 232 ms, "the" 233 / 192 ms; 5.6 MB of memory at start, 10.7 MB after all of that and a
picture at full size.

## Code

| File | Job |
|---|---|
| `wmain.c` | start-up, the screen stack (Back), commands, the built-in About and Get Wikipedia pages |
| `pack.c` | opening packs, articles, title search (memory, top lists, key index, word index), search inside, snippets |
| `home.c` | the screens made of rows: search box and shelves, lists, "search inside" results, triage questions |
| `reader.c` | an article in Notes' View mode: moving, tall paragraphs, links, contents, find, bookmarks, copy |
| `image.c` | pictures: LVGL's TJpgDec (compiled for this program only), scaling, a small cache, the full-screen viewer |
| `triage.c`, `state.c` | the triage questions; recently read, bookmarks and text size (`~/.local/state/emuzero/wiki/state`) |
| `wkeys.c`, `wui.c` | the key table; status bar, menus, keys sheet and toasts (Notes' `ui.c` with Wiki's keys) |
| `tools/build-pack.py` | the packs: Markdown conversion, FM 21-76 clean-up (page headers, chapters, headings), the radio table laid out as lists, the word index |
| from `../notes/` | `nplat_*.c`, `keyspec.c`, `fonts.c`, `draw.c`, `textbuf.c`, `doc.c`, `md.c`, `view.c` (with a picture hook that only the Wiki sets), `clip.c`, `store.c`, `search.c` |
