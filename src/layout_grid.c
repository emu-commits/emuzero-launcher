// SPDX-License-Identifier: MIT
// Grid: pages of 2 rows x 5 apps, each an icon with its name under it. Arrows move between
// apps; Left/Right past the edge of a page flips to the previous/next page (the last page
// wraps to the first). Dots at the bottom show the page.
#include "layout.h"
#include "apps.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

#define COLS 5
#define ROWS 2
#define PER (COLS * ROWS)
#define TOP 4
static int CELL_W, CELL_H, ICON;   /* from the area's size: 64 x 67 with a 40 px icon full screen */

static lv_obj_t *area, *page_box, *hilite, *dots;
static lv_obj_t *names[PER];
static int shown_page = -1;

static void create(lv_obj_t *a)
{
	area = a;
	CELL_W = lv_obj_get_width(a) / COLS;
	CELL_H = (lv_obj_get_height(a) - TOP - 12) / ROWS;
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
	shown_page = -1;
}

static void build_page(int page)
{
	lv_obj_clean(page_box);
	for (int k = 0; k < PER; k++) {
		int i = page * PER + k;
		names[k] = NULL;
		if (i >= napps) continue;
		int x = (k % COLS) * CELL_W, y = TOP + (k / COLS) * CELL_H;
		lv_obj_t *ic = icon_create(page_box, &apps[i], ICON);
		lv_obj_set_pos(ic, x + (CELL_W - ICON) / 2, y + 5);
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
		return row > 0 ? sel - COLS : sel;
	case LV_KEY_DOWN:
		if (row < ROWS - 1 && sel + COLS <= last) return sel + COLS;
		if (row < ROWS - 1 && page * PER + COLS <= last) return last;   /* short second row */
		return sel;
	}
	return sel;
}

const struct layout layout_grid = { "grid", create, show, move };
