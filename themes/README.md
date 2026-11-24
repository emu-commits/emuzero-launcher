# emuZero themes

A theme is a folder holding a `theme.conf`, plus any fonts or pictures that file names. The
launcher looks for themes in two places:

- `~/.local/share/emuzero/themes/NAME/`: your own. You can add these without root, and they win over the
  system ones with the same name.
- `/usr/share/emuzero/themes/NAME/`: the ones that come with the launcher.

Choose one on the **Theme** tile (or press **T** on the home screen). The same page picks the
home layout: the theme's own, or Carousel or Grid. The choice is kept in
`~/.config/emuzero/settings`.

To make a theme, copy `minimal/` to a new folder name (letters, digits, `-` and `_`), set
`name =`, and change what you like. A theme needs no code. Every key is optional; anything
left out keeps the value from Minimal. A line that can't be read is reported in
`/tmp/emuzero-launcher.log` and otherwise ignored.

## Keys

Colours are written `#rrggbb`. Yes/no keys take `yes`, `no`, `on`, `off`, `true`, `false`, `1` or `0`.

| Section | Key | Meaning | Minimal |
|---|---|---|---|
| | `name` | name on the Theme page | Minimal |
| `[colors]` | `background` | screen background | `#101418` |
| | `text` | main text | `#f0f0f0` |
| | `dim` | secondary text | `#8a96a6` |
| | `line` | lines and frames | `#2a3038` |
| | `accent` | highlights | `#4fa3ff` |
| | `selected` | fill behind the selected item | `#1e2a38` |
| | `selected_text` | text on `selected` | = `text` |
| | `light`, `shadow` | the two edges of a bevel | white, black |
| `[fonts]` | `regular`, `bold` | font files (`.ttf`, `.otf`): in the theme folder, an absolute path, or a name in `/usr/share/fonts/dejavu` such as `DejaVuSansMono.ttf` | DejaVu Sans |
| `[home]` | `layout` | `carousel` or `grid` | carousel |
| | `wallpaper` | PNG behind everything, scaled to cover 320×170 | none |
| | `card`, `card_selected` | card fill (carousel) | = background, selected |
| | `card_line`, `card_selected_line` | card frame | = line, text |
| | `card_border`, `card_selected_border` | frame width, 0–6 | 2, 3 |
| | `card_radius` | corner rounding, per cent of the card (0 = square) | 20 |
| | `name` | app names | = dim |
| | `name_selected` | the selected app's name under the carousel (in the grid it sits on `selected` and uses `selected_text`) | = text |
| | `chevrons` | ‹ › at the sides of the carousel | yes |
| | `track` | position bar under the carousel | yes |
| `[bar]` | `position` | `top` or `bottom` | top |
| | `height` | 0–30; 0 = no status bar | 18 |
| | `background`, `text` | bar colours | = background, text |
| | `title`, `title_color` | text on the left | emuZero, = accent |
| | `badges` | clock and battery: `round`, `square`, `plain` (text only) or `sunken` | round |
| | `badge`, `badge_text` | clock badge colours | = line, text |
| | `line`, `line_color` | a line between the bar and the apps | yes, = line |
| `[window]` | `show` | draw the apps inside a window | no |
| | `title` | window title | none |
| | `margin` | space around the window, 0–20 | 4 |
| | `border` | frame width, 0–4 | 1 |
| | `bevel` | `raised` or `sunken` 3-D frame instead of `border` (uses light/shadow) | none |
| | `title_height` | title bar height, 0–24 (0 = none) | 14 |
| | `stripes` | lines across the title bar, title centred | no |
| | `close_box` | a small box at the left of the title bar | no |
| | `background`, `border_color` | window colours | = background, text |
| | `title_background`, `title_color` | title bar colours | = background, text |

The battery badge keeps its own colours (green, yellow at 35 % and below, red at 15 % and
below). With `badges = plain` those colours go to the text.

## The themes here

- **minimal**: the default. Dark, with rounded cards in a carousel.
- **paper**: a sample showing what a folder alone can change: black on white, square corners,
  a grid inside a window with a striped title bar.
