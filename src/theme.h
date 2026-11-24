// SPDX-License-Identifier: MIT
// Themes. A theme is a folder with a theme.conf (and any fonts or pictures it names), in
// ~/.local/share/emuzero/themes/NAME or /usr/share/emuzero/themes/NAME; the first one found
// wins, so a theme can be added or changed without touching the program. theme.conf holds
// "key = value" lines under [sections]; every key is optional and falls back to the values of
// the built-in minimal theme. See themes/README.md for the keys.
#pragma once
#include <stdint.h>

enum badge_style { BADGE_ROUND, BADGE_SQUARE, BADGE_PLAIN, BADGE_SUNKEN };
enum bevel { BEVEL_NONE, BEVEL_RAISED, BEVEL_SUNKEN };

struct theme {
	char id[32], name[48];        /* folder name; name shown in the Theme page */
	char dir[320];                /* the theme's folder ("" for the built-in theme) */

	/* [colors] */
	uint32_t bg, fg, dim, line, accent, sel_bg, sel_fg, light, shadow;

	/* [fonts]: font files, absolute (found in the theme folder or the system font folder) */
	char font[2][320];            /* regular, bold */

	/* [home] */
	char layout[16];              /* carousel or grid, unless the settings choose */
	char wallpaper[320];          /* picture behind the apps, or "" */
	int chevrons;                 /* carousel: arrows at the sides */
	int track;                    /* carousel: position bar */
	int card_radius;              /* per cent of a card's size */
	int card_border, card_sel_border;
	uint32_t card_bg, card_sel_bg, card_line, card_sel_line, name_fg, name_sel_fg;

	/* [bar] status bar */
	int bar_bottom, bar_h, bar_line;
	uint32_t bar_bg, bar_fg, bar_line_color, title_fg;
	char title[32];
	int badge_style;
	uint32_t badge_bg, badge_fg;

	/* [window] an optional window frame around the apps */
	int win;                      /* 0: none */
	char win_title[32];
	int win_margin, win_border, win_bevel, win_title_h, win_stripes, win_close;
	uint32_t win_bg, win_border_color, win_title_bg, win_title_fg;
};

extern struct theme theme;

/* load theme id (falls back to the built-in minimal theme); returns 0 when it was found */
int theme_load(const char *id);
/* the themes that can be chosen, by folder name, sorted; returns how many (at most max) */
int theme_list(char ids[][32], int max);
/* the launcher's own settings (~/.config/emuzero/settings): theme and home layout */
struct settings { char theme[32], layout[16]; };
void settings_load(struct settings *s);
int settings_save(const struct settings *s);
