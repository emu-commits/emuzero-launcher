// SPDX-License-Identifier: MIT
// Grid: pages of 2 rows x 5 apps, each an icon with its name under it. Arrows move between
// apps; Left/Right past the edge of a page flips to the previous/next page (the last page
// wraps to the first); Down past the bottom row and Up past the top row go on to the next or
// previous page in the same column, as a scrolled window would (no wrap). Dots at the bottom,
// or a scrollbar when the theme has one, show the page.
#include "layout.h"
#include "apps.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"
#include <string.h>

#define COLS 5
#define ROWS 2
#define PER (COLS * ROWS)
#define TOP 4
static int CELL_W, CELL_H, ICON;   /* from the area's size: 64 x 67 with a 40 px icon full screen */

static lv_obj_t *area, *page_box, *hilite, *dots, *scroll;
static lv_obj_t *names[PER], *icons[PER];
static int name_x[PER];   /* the middle of each name */
static int shown_page = -1, SB, SB_H;   /* SB: the scrollbar's width, 0 without one */

static void create(lv_obj_t *a)
{
	area = a;
	SB = theme.win && theme.win_scrollbar ? 11 : 0;   /* a folder window scrolls; dots need 12 px below */
	CELL_W = (lv_obj_get_width(a) - SB) / COLS;
	CELL_H = (lv_obj_get_height(a) - TOP - (SB ? 2 : 12)) / ROWS;
	if (CELL_H > 64) CELL_H = 64;
	ICON = CELL_H - 22 < 40 ? CELL_H - 22 : 40;
	if (ICON > CELL_W - 16) ICON = CELL_W - 16;
	hilite = lv_obj_create(area);
	lv_obj_remove_style_all(hilite);
	lv_obj_set_size(hilite, CELL_W - 4, CELL_H);
	lv_obj_set_style_radius(hilite, theme.card_radius ? 8 : 0, 0);
	lv_obj_set_style_bg_opa(hilite, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(hilite, lv_color_hex(theme.sel_bg), 0);
	lv_obj_set_style_border_width(hilite, 1, 0);
	lv_obj_set_style_border_color(hilite, lv_color_hex(theme.accent), 0);
	if (theme.glow) {
		lv_obj_set_style_shadow_width(hilite, 10, 0);
		lv_obj_set_style_shadow_color(hilite, lv_color_hex(theme.accent), 0);
		lv_obj_set_style_shadow_opa(hilite, LV_OPA_70, 0);
	}

	page_box = lv_obj_create(area);
	lv_obj_remove_style_all(page_box);
	lv_obj_remove_flag(page_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(page_box, COLS * CELL_W, ROWS * CELL_H + TOP);

	dots = lv_obj_create(area);
	lv_obj_remove_style_all(dots);
	lv_obj_remove_flag(dots, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(dots, LV_SIZE_CONTENT, 6);
	lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_column(dots, 6, 0);
	lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -5);
	scroll = NULL;
	if (SB) {
		scroll = lv_obj_create(area);
		lv_obj_remove_style_all(scroll);
		lv_obj_remove_flag(scroll, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
		SB_H = lv_obj_get_height(a);
		lv_obj_set_size(scroll, SB, SB_H);
		lv_obj_align(scroll, LV_ALIGN_TOP_RIGHT, 0, 0);
	}
	if (theme.sel_name) lv_obj_add_flag(hilite, LV_OBJ_FLAG_HIDDEN);
	shown_page = -1;
}

/* a part of the scrollbar: a raised button (in a window with bevels) or a rounded one */
static lv_obj_t *sb_part(int y, int h, uint32_t fill)
{
	lv_obj_t *o = lv_obj_create(scroll);
	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(o, 0, y);
	lv_obj_set_size(o, SB, h);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
	if (theme.win_bevel) ui_bevel(o, BEVEL_RAISED, 1);
	else {
		lv_obj_set_style_radius(o, theme.scroll_radius, 0);
		lv_obj_set_style_border_width(o, 1, 0);
		lv_obj_set_style_border_color(o, lv_color_hex(theme.scroll_line), 0);
	}
	return o;
}

/* a small triangle pointing up or down, drawn a row at a time */
static void sb_arrow(lv_obj_t *b, int up)
{
	int mid = SB / 2 - 1;   /* inside the 1-px frame */
	for (int r = 0; r < 3; r++) {
		lv_obj_t *l = lv_obj_create(b);
		lv_obj_remove_style_all(l);
		int w = 1 + 2 * (up ? r : 2 - r);
		lv_obj_set_pos(l, mid - w / 2, SB / 2 - 3 + r);
		lv_obj_set_size(l, w, 1);
		lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(l, lv_color_hex(theme.scroll_arrow), 0);
	}
}

/* the scrollbar: arrows at the ends, the thumb one page long at the page shown */
static void draw_scroll(int page, int pages)
{
	lv_obj_clean(scroll);
	if (pages < 2) return;
	int h = SB_H, track = h - 2 * SB;
	lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(scroll, lv_color_hex(theme.scroll_track), 0);
	sb_arrow(sb_part(0, SB, theme.scroll_thumb), 1);
	sb_arrow(sb_part(h - SB, SB, theme.scroll_thumb), 0);
	int tl = track / pages < 8 ? 8 : track / pages;
	sb_part(SB + (track - tl) * page / (pages - 1), tl, theme.scroll_thumb);
}

static void build_page(int page)
{
	lv_obj_clean(page_box);
	for (int k = 0; k < PER; k++) {
		int i = page * PER + k;
		names[k] = icons[k] = NULL;
		if (i >= napps) continue;
		int x = (k % COLS) * CELL_W, y = TOP + (k / COLS) * CELL_H;
		lv_obj_t *ic = icon_create(page_box, &apps[i], ICON);
		lv_obj_set_pos(ic, x + (CELL_W - ICON) / 2, y + 5);
		icons[k] = ic;
		name_x[k] = x + CELL_W / 2;
		lv_font_t *f = ui_font(11, 0);
		lv_obj_t *l = ui_label(page_box, f, theme.name_fg, apps[i].name);
		lv_obj_set_size(l, CELL_W - 6, lv_font_get_line_height(f));   /* one line, cut with dots */
		lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_pos(l, x + 3, y + ICON + 8);
		names[k] = l;
	}
	lv_obj_clean(dots);
	int pages = (napps + PER - 1) / PER;
	if (scroll) { draw_scroll(page, pages); pages = 0; }
	for (int p = 0; pages > 1 && p < pages; p++) {
		lv_obj_t *d = lv_obj_create(dots);
		lv_obj_remove_style_all(d);
		lv_obj_set_size(d, p == page ? 14 : 6, 6);
		lv_obj_set_style_radius(d, theme.card_radius ? 3 : 0, 0);
		lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(d, lv_color_hex(p == page ? theme.accent : theme.line), 0);
	}
	shown_page = page;
}

static void show(int sel)
{
	if (!napps) {
		lv_obj_clean(page_box);
		lv_obj_clean(dots);
		lv_obj_add_flag(hilite, LV_OBJ_FLAG_HIDDEN);
		lv_obj_center(ui_label(page_box, ui_font(16, 1), theme.fg, "No apps installed"));
		shown_page = -1;
		return;
	}
	int page = sel / PER, k = sel % PER;
	if (page != shown_page) build_page(page);
	for (int j = 0; j < PER; j++)
		if (names[j]) lv_obj_set_style_text_color(names[j], lv_color_hex(j == k ? theme.sel_fg : theme.name_fg), 0);
	if (theme.sel_name) {   /* as in a folder: the name on the selected colour, the icon tinted with it */
		for (int j = 0; j < PER; j++) {
			if (!names[j]) continue;
			int on = j == k, w = CELL_W - 6;
			if (on) {
				const char *t = apps[page * PER + j].name;   /* not the label's text: dots may stand in it */
				int tw = lv_text_get_width(t, strlen(t), lv_obj_get_style_text_font(names[j], 0), 0) + 4;
				if (tw < w) w = tw;
			}
			lv_obj_set_width(names[j], w);
			lv_obj_set_x(names[j], name_x[j] - w / 2);
			lv_obj_set_style_bg_opa(names[j], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
			lv_obj_set_style_bg_color(names[j], lv_color_hex(theme.sel_bg), 0);
			if (lv_obj_check_type(icons[j], &lv_image_class)) {
				lv_obj_set_style_image_recolor(icons[j], lv_color_hex(theme.sel_bg), 0);
				lv_obj_set_style_image_recolor_opa(icons[j], on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
			}
		}
		return;
	}
	lv_obj_remove_flag(hilite, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(hilite, (k % COLS) * CELL_W + 2, TOP + (k / COLS) * CELL_H);
}

static int move(uint32_t key, int sel, int n)
{
	int pages = (n + PER - 1) / PER, page = sel / PER, row = sel % PER / COLS, col = sel % COLS;
	int last = n - 1;
	switch (key) {
	case LV_KEY_RIGHT:
		if (col < COLS - 1 && sel < last) return sel + 1;
		page = (page + 1) % pages;   /* flip forward, same row, first column */
		sel = page * PER + row * COLS;
		return sel > last ? page * PER : sel;
	case LV_KEY_LEFT:
		if (col > 0) return sel - 1;
		page = (page + pages - 1) % pages;   /* flip back, same row, last column */
		sel = page * PER + row * COLS + COLS - 1;
		return sel > last ? last : sel;
	case LV_KEY_UP:
		if (row > 0) return sel - COLS;
		/* past the top row: the previous page's bottom row, same column (no wrap) */
		return page > 0 ? (page - 1) * PER + (ROWS - 1) * COLS + col : sel;
	case LV_KEY_DOWN:
		if (row < ROWS - 1 && sel + COLS <= last) return sel + COLS;
		if (row < ROWS - 1 && page * PER + COLS <= last) return last;   /* short second row */
		/* past the bottom row: the next page's top row, same column, as a scrolled window
		 * would (no wrap; a short page gives its last app) */
		if (page < pages - 1) return (page + 1) * PER + col > last ? last : (page + 1) * PER + col;
		return sel;
	}
	return sel;
}

const struct layout layout_grid = { "grid", create, show, move };
