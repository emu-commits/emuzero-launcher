# emuZero themes

A theme is a folder holding a `theme.conf`, plus any fonts or pictures that file names, and
optionally an `icons/` folder. The
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

Colours are written `#rrggbb`; where the table says so, `none` draws nothing there. Yes/no keys take `yes`, `no`, `on`, `off`, `true`, `false`, `1` or `0`.

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
| | `glow` | a soft light around the selected card (in `card_selected_line`) or grid cell (in `accent`) | no |
| | `icon_colors` | 2 to 8 colours, dark to light: every icon is drawn in these only, by brightness; the lightest is kept for white parts | none (own colours) |
| | `selection` | grid: `cell` (the whole cell on `selected`) or `name` (as in a folder: only the name on `selected`, the icon tinted with it) | cell |
| | `chevrons` | ‹ › at the sides of the carousel | yes |
| | `track` | position bar under the carousel | yes |
| `[bar]` | `position` | `top` or `bottom` | top |
| | `height` | 0–30; 0 = no status bar | 18 |
| | `background`, `text` | bar colours; `background = none` lets the wallpaper show | = background, text |
| | `gradient` | a second bar colour, fading top to bottom | none |
| | `title`, `title_color` | text on the left (`title =` for none) | emuZero, = accent |
| | `title_background` | draws the title as a button, like a start button (`none`: a frame only) | none |
| | `title_gradient`, `title_radius`, `title_bevel` | the button's second colour (top to bottom), corner rounding 0–15, `raised` or `sunken` | none, 0, none |
| | `badges` | clock and battery: `round`, `square`, `plain` (text only) or `sunken` | round |
| | `badge`, `badge_text` | clock badge colours | = line, text |
| | `line`, `line_color` | a line between the bar and the apps | yes, = line |
| `[window]` | `show` | draw the apps inside a window | no |
| | `title` | window title | none |
| | `margin` | space around the window, 0–20 | 4 |
| | `margin_x`, `margin_y` | the same at the sides / top and bottom, 0–40 | = margin |
| | `radius` | corner rounding, 0–12 | 0 |
| | `border` | frame width, 0–4 | 1 |
| | `bevel` | `raised` or `sunken` 3-D frame instead of `border` (uses light/shadow) | none |
| | `title_height` | title bar height, 0–24 (0 = none) | 14 |
| | `stripes` | lines across the title bar, title centred | no |
| | `close_box` | `left` (or `yes`): a small box at the left of the title bar; `right`: a button with a cross at the right | no |
| | `close_background`, `close_color` | the right-hand button's fill and cross | = title_background, title_color |
| | `background`, `border_color` | window colours (App info and the Theme page use `background` and the main text colours too) | = background, text |
| | `title_background`, `title_color` | title bar colours | = background, text |
| | `title_gradient`, `title_gradient_direction` | a second title bar colour, `vertical` (top to bottom) or `horizontal` (left to right) | none, vertical |
| | `title_icon` | a picture before the title: a PNG in the theme folder (`folder.png`, drawn at the title bar's height less 4) or an app id whose icon to use | none |
| | `min_max`, `min_max_background` | minimise and maximise buttons beside the right-hand close button (home window only), and their fill | no, = close_background |
| | `menu`, `menu_underline` | a menu bar under the title: the words, and whether each first letter is underlined | none, no |
| | `client`, `client_bevel`, `client_line` | the apps' own area inside the window: its colour (`#rrggbb`; unset: none), a `sunken` or `raised` 2-px frame, or else a 1-px frame in this colour | none, none, none |
| | `status` | a status bar along the bottom: the selected app's description, and this text with `%d` for the number of apps (`%d object(s)`) | none |
| | `scrollbar` | grid: a scrollbar at the right for the pages instead of dots | no |
| | `scroll_track`, `scroll_thumb`, `scroll_line`, `scroll_arrow`, `scroll_radius` | its colours (`scroll_line` frames the buttons in a window without `bevel`; with one they are raised) and corner rounding 0–6 | = background, background, line, text, 0 |
| | `dialogs` | App info as a dialog window over the home screen: the app's icon and the title in the title bar, the buttons at the right | no |
| `[menu]` | `style` | the Theme page: `panel` (along the bottom) or `start` (a menu on the bar's title button, which shows pushed in) | panel |
| | `width` | the start menu's width, 120–320 | 200 |
| | `background`, `text` | its colours | = window background, text |
| | `bevel`, `border`, `border_color`, `radius` | its frame: `raised` or `sunken`, else a border 0–4 wide; corner rounding 0–12 | none, 0, = line, 0 |
| | `banner`, `banner_background`, `banner_gradient`, `banner_color` | a name reading upwards in a band down the left side | none, = accent, none, = selected_text |
| | `header`, `header_background`, `header_gradient`, `header_color` | a name in a band across the top | none, = accent, none, = selected_text |
| | `separator` | a line above the keys row (etched with `bevel`) | no |
| | `footer_background`, `footer_gradient`, `footer_color` | the keys row at the bottom; a background of its own runs the full width | = background, none, = dim |
| `[button]` | `background`, `gradient` | App info's buttons in colours of their own (unset: the theme's selected colours) | none |
| | `color`, `border_color`, `radius`, `bevel` | their text, frame (or a `raised` bevel), rounding 0–12 | = text, = line, 0, none |
| | `focus` | the focused button: dots inside it (with `bevel`, which also adds a `border_color` edge) or a ring | = accent |

The battery badge keeps its own colours (green, yellow at 35 % and below, red at 15 % and
below). With `badges = plain` those colours go to the text.

## Icons

A theme can give apps pictures of its own: `icons/ID.png` in the theme folder, where ID is the
app's id (its `.desktop` file name without `.desktop`, for example `nz-terminal`, `nz-options`,
`camera_app`; the Theme tile is `emuzero-theme`). An app without one in the theme keeps its own
picture, else emuZero's from `/usr/share/emuzero/icons/` (see `icons/README.md`), else a tile
with its first letter. Pictures are PNG, square looks best, up to 1024 pixels; they are scaled
down to the size each layout draws.

## The themes here

- **minimal**: the default. Dark, with rounded cards in a carousel.
- **paper**: a sample showing what a folder alone can change: black on white, square corners,
  a grid inside a window with a striped title bar.
- **desktop95**: the grey 3-D desktop of the mid-90s: teal, the apps in a folder window (navy
  title bar, menu bar, white sunken contents, scrollbar, status bar), a Start button and a
  sunken clock on a grey taskbar; the Theme page is a start menu with a grey band, App info a
  dialog with grey buttons.
- **desktop2001**: the glossy blue desktop of the early 2000s: sky and a green hill, the apps in
  a rounded folder window (blue title bar, blue and red buttons, menu bar, white contents, soft
  blue scrollbar, status bar), a green start button; the Theme page is a start menu with blue
  bands top and bottom, App info a dialog.
- **superhandheld**: a handheld game on a TV, inside a console border: four colours on a cream
  screen, the icons drawn in the same four.
- **fieldindex**: a red pocket field guide: a big blue lens and three lamps on the lid, a hinge,
  a mint screen in a grey bezel.
- **cyberdeck**: neon on black: a night city over a glowing floor grid, scanlines, frames that
  light up, icons in magenta and cyan.

The wallpapers are drawn by `tools/draw-wallpapers.py` (`python3 tools/draw-wallpapers.py
themes`), the title-bar folders by `tools/draw-title-icons.py`; they are our own drawings, and the themes use no fonts, logos or names of the
products they call to mind.
