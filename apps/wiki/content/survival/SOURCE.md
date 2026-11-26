# Where this content comes from

Copied unchanged from the CYD Survival Wiki, <https://github.com/everyday3d/esp32-cyd-survival-wiki>,
commit `06256dc7dbf22f11b262cba440bea5ec56f81d04` (2026-11-10), under its MIT licence (`LICENSE`
here, Everyday3D / BoxerDawg 3D):

| Here | There |
|---|---|
| `articles/*.md` (118 guides) | `sdcard/wiki/articles/` |
| `categories.tsv` (15 topics) | `sdcard/wiki/categories.tsv` |
| `img/*.jpg` (22 pictures) | `sdcard/wiki/img/` |
| `triage.h` (16 "What is happening?" screens) | `firmware/src/triage.h` |
| `fm21-76/*.txt` | `sdcard/library/fm21-76/`: FM 21-76, US Army Survival Manual (1992), text taken from the PDF; a US government publication, public domain. It stops at chapter 23 (no appendices). |

Not taken: the maps (handled separately), the field-guide book (the same guides again), their
search index, the Thai translation, and the firmware.

`apps/wiki/tools/build-pack.py survival` turns this folder into `survival.wpack`: the guides'
links and pictures rewritten for the pack, the radio table laid out as lists, FM 21-76 cleaned
(page headers, chapters, headings) and split into its 23 chapters, and the CYD's `x:lost` triage
answer (its own maps screen) pointed at the STOP guide.

As the project says: an informational reference guide, not a substitute for professional medical
advice, certified wilderness survival training, or emergency services.
