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
enum close_box { CLOSE_NONE, CLOSE_LEFT, CLOSE_RIGHT };

#define COLOR_UNSET 0xff000000u   /* a colour key not given */
#define COLOR_NONE  0xfe000000u   /* "none": nothing drawn (where a key allows it) */

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
	int glow;                     /* a soft light around the selected card or cell */
	uint32_t icon_colors[8];      /* icons drawn in these colours only, dark to light */
	int n_icon_colors;            /* 0: icons keep their own colours */
	int sel_name;                 /* grid: only the name is highlighted, the icon tinted (as in a folder) */

	/* [bar] status bar */
	int bar_bottom, bar_h, bar_line;
	uint32_t bar_bg, bar_fg, bar_line_color, title_fg;
	uint32_t bar_grad;            /* second colour, top to bottom; UNSET: plain */
	char title[32];
	uint32_t title_bg, title_grad; /* the title as a button; UNSET: plain text */
	int title_bevel, title_radius;
	int badge_style;
	uint32_t badge_bg, badge_fg;

	/* [window] an optional window frame around the apps */
	int win;                      /* 0: none */
	char win_title[32];
	int win_margin, win_margin_x, win_margin_y, win_border, win_bevel, win_title_h, win_stripes, win_close;
	int win_radius, win_grad_hor;
	uint32_t win_bg, win_border_color, win_title_bg, win_title_fg, win_title_grad, close_bg, close_fg;
	/* a folder window: buttons, menu bar, a client area of its own, status bar, scrollbar */
	int win_min_max;              /* minimise and maximise buttons beside the close button */
	uint32_t min_max_bg;
	char win_title_icon[32];      /* an app id whose picture goes before the title */
	char win_title_pic[320];      /* or a picture file of the theme's own */
	char win_menu[64];            /* menu bar words, or "" */
	int win_menu_underline;       /* each word's first letter underlined */
	uint32_t win_client, win_client_line;  /* the apps' own area (UNSET: none), its 1-px frame */
	int win_client_bevel;
	char win_status[32];          /* status bar: "%d object(s)", or "" for none */
	int win_scrollbar;            /* grid: a scrollbar for the pages instead of dots */
	uint32_t scroll_track, scroll_thumb, scroll_arrow, scroll_line;
	int scroll_radius;
	int win_dialogs;              /* App info in a window of its own */

	/* [menu] the Theme page: a panel along the bottom, or a start menu over the bar's button */
	int menu_start, menu_w, menu_bevel, menu_border, menu_radius, menu_separator;
	uint32_t menu_bg, menu_fg, menu_border_color;
	char menu_banner[24], menu_header[24];
	uint32_t banner_bg, banner_grad, banner_fg, header_bg, header_grad, header_fg, footer_bg, footer_grad, footer_fg;

	/* [button] App info's buttons; background UNSET: the theme's selected colours */
	uint32_t btn_bg, btn_grad, btn_fg, btn_line, btn_focus;
	int btn_radius, btn_bevel;
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
