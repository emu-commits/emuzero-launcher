// SPDX-License-Identifier: MIT
// The Theme page: a panel along the bottom with two rows, the theme and the home layout. Each
// change is drawn at once on the home screen above it. Up/Down choose the row, Left/Right
// change it; Enter keeps the choice (saved for the next start), Esc goes back to how it was.
#include "themepage.h"
#include "layout.h"
#include "platform.h"
#include "statusbar.h"
#include "theme.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define MAX_THEMES 32

static lv_obj_t *scr, *panel;
static void (*on_done)(void);
static char ids[MAX_THEMES][32];
static int nthemes, ti, li, row;
static struct settings before;
/* home layouts: the theme's own choice, then each layout */
static const char *const layout_names[] = { "", "carousel", "grid" };
#define NLAYOUTS 3

static const char *layout_label(int i)
{
	static char s[48];
	if (i == 1) return "Carousel";
	if (i == 2) return "Grid";
	snprintf(s, sizeof(s), "Theme's own (%s)", !strcmp(theme.layout, "grid") ? "grid" : "carousel");
	return s;
}

static lv_obj_t *row_obj(lv_obj_t *parent, int x, int y, int w, int on, const char *key, const char *value)
{
	int start = theme.menu_start;
	lv_obj_t *r = lv_obj_create(parent);
	lv_obj_remove_style_all(r);
	lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(r, w, 19);
	lv_obj_set_pos(r, x, y);
	lv_obj_set_style_radius(r, theme.card_radius && !start ? 5 : 0, 0);
	if (on) {
		lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(r, lv_color_hex(theme.sel_bg), 0);
		lv_obj_set_style_border_width(r, start ? 0 : 1, 0);
		lv_obj_set_style_border_color(r, lv_color_hex(theme.accent), 0);
	}
	lv_obj_t *k = ui_label(r, ui_font(12, !start), on ? theme.sel_fg : start ? theme.menu_fg : theme.dim, key);
	lv_obj_align(k, LV_ALIGN_LEFT_MID, 6, 0);
	char v[96];
	snprintf(v, sizeof(v), "‹ %s ›", value);
	lv_obj_t *l = ui_label(r, ui_font(12, on && !start), on ? theme.sel_fg : start ? theme.menu_fg : theme.fg, v);
	lv_obj_align(l, LV_ALIGN_RIGHT_MID, -6, 0);
	return r;
}

static lv_obj_t *band(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, uint32_t grad, int hor)
{
	lv_obj_t *o = lv_obj_create(p);
	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
	if (grad != COLOR_UNSET) {
		lv_obj_set_style_bg_grad_color(o, lv_color_hex(grad), 0);
		lv_obj_set_style_bg_grad_dir(o, hor ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
	}
	return o;
}

/* [menu] style = start: a menu standing on the bar's start button (or hanging from it, with
 * the bar at the top): a band down the left side or across the top with a name on it, the two
 * rows, a line, and the keys at the bottom */
static void draw_start(void)
{
	char tname[48];
	int bh = statusbar_height(), b = theme.menu_bevel ? 2 : theme.menu_border;
	int hh = theme.menu_header[0] ? 22 : 0, sep = theme.menu_separator ? 6 : 2, fh = 17;
	int w = theme.menu_w, h = 2 * b + hh + 2 + 2 * 20 + sep + fh;
	int bw = theme.menu_banner[0] ? 21 : 0, iw = w - 2 * b;
	panel = band(scr, 0, theme.bar_bottom ? SCREEN_H - bh - h : bh, w, h, theme.menu_bg, COLOR_UNSET, 0);
	if (theme.menu_radius) {
		lv_obj_set_style_radius(panel, theme.menu_radius, 0);
		lv_obj_set_style_clip_corner(panel, true, 0);
	}
	if (theme.menu_bevel) ui_bevel(panel, theme.menu_bevel, 2);
	else if (b) {
		lv_obj_set_style_border_width(panel, b, 0);
		lv_obj_set_style_border_color(panel, lv_color_hex(theme.menu_border_color), 0);
	}
	if (hh) {
		lv_obj_t *t = band(panel, 0, 0, iw, hh, theme.header_bg, theme.header_grad, 0);
		lv_obj_t *l = ui_label(t, ui_font(13, 1), theme.header_fg, theme.menu_header);
		lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
	}
	if (bw) {   /* the name reads upwards from the bottom */
		int bandh = h - 2 * b - hh;
		lv_obj_t *t = band(panel, 0, hh, bw, bandh, theme.banner_bg, theme.banner_grad, 0);
		lv_font_t *f = NULL;
		int tw = 0, n = strlen(theme.menu_banner);
		for (int px = 15; px >= 9; px--) {   /* the largest size that fits the band's length */
			f = ui_font(px, 1);
			if ((tw = lv_text_get_width(theme.menu_banner, n, f, 0)) <= bandh - 8) break;
		}
		lv_obj_t *l = ui_label(t, f, theme.banner_fg, theme.menu_banner);
		int lh = lv_font_get_line_height(f);
		lv_obj_set_size(l, tw, lh);
		lv_obj_set_style_transform_pivot_x(l, 0, 0);
		lv_obj_set_style_transform_pivot_y(l, 0, 0);
		lv_obj_set_style_transform_rotation(l, -900, 0);
		lv_obj_set_pos(l, (bw - lh) / 2, bandh - 4);
	}
	int x = bw + 1, rw = iw - bw - 2, y = hh + 2;
	snprintf(tname, sizeof(tname), "%s", nthemes ? theme.name : "Minimal (built in)");
	row_obj(panel, x, y, rw, row == 0, "Theme", tname);
	row_obj(panel, x, y + 20, rw, row == 1, "Home", layout_label(li));
	y += 40;
	if (theme.menu_separator) {   /* an etched line: shade over light, or one line without bevels */
		lv_obj_t *l1 = band(panel, x + 2, y + 2, rw - 4, 1, theme.menu_bevel ? theme.shadow : theme.line, COLOR_UNSET, 0);
		if (theme.menu_bevel) band(panel, x + 2, y + 3, rw - 4, 1, theme.light, COLOR_UNSET, 0);
		(void)l1;
	}
	y += sep;
	int fx = theme.footer_bg == theme.menu_bg ? x : 0, fw = theme.footer_bg == theme.menu_bg ? rw : iw;
	lv_obj_t *ft = band(panel, fx, y, fw, h - 2 * b - y, theme.footer_bg, theme.footer_grad, 0);
	lv_obj_t *hint = ui_label(ft, ui_font(11, 0), theme.footer_fg, "Enter: keep    Esc: cancel");
	lv_obj_align(hint, fx ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, fx ? 6 : -8, 0);
	statusbar_pressed(1);
}

static void draw(void)
{
	struct ui_rect pr = ui_page_rect();
	int h = 64;
	if (panel) lv_obj_delete(panel);
	panel = NULL;
	if (theme.menu_start && statusbar_height()) { draw_start(); return; }
	panel = lv_obj_create(scr);
	lv_obj_remove_style_all(panel);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(panel, pr.x, pr.y + pr.h - h);
	lv_obj_set_size(panel, pr.w, h);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(panel, lv_color_hex(ui_page_bg()), 0);
	lv_obj_set_style_border_width(panel, 1, 0);
	lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(theme.line), 0);

	char tname[48];
	snprintf(tname, sizeof(tname), "%s", nthemes ? theme.name : "Minimal (built in)");
	row_obj(panel, 6, 4, SCREEN_W - 12, row == 0, "Theme", tname);
	row_obj(panel, 6, 24, SCREEN_W - 12, row == 1, "Home", layout_label(li));
	lv_obj_t *hint = ui_label(panel, ui_font(10, 0), theme.dim, "Enter: keep    Esc: cancel");
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void apply(void)
{
	panel = NULL;   /* ui_apply clears the whole screen, this panel too */
	ui_apply(nthemes ? ids[ti] : "minimal", layout_names[li]);
	draw();
}

static void close_page(int keep)
{
	if (keep) ui_save();
	else ui_apply(before.theme, before.layout);   /* clears the screen, panel included */
	if (keep && panel) { lv_obj_delete(panel); statusbar_pressed(0); }
	panel = NULL;
	if (on_done) on_done();
}

int themepage_active(void) { return panel != NULL; }

void themepage_open(lv_obj_t *screen, void (*done)(void))
{
	scr = screen;
	on_done = done;
	before = *ui_settings();
	nthemes = theme_list(ids, MAX_THEMES);
	ti = 0;
	for (int i = 0; i < nthemes; i++)
		if (!strcmp(ids[i], before.theme)) ti = i;
	li = 0;
	for (int i = 1; i < NLAYOUTS; i++)
		if (!strcmp(layout_names[i], before.layout)) li = i;
	row = 0;
	draw();
}

void themepage_key(uint32_t k)
{
	switch (k) {
	case LV_KEY_UP: case 'f':
	case LV_KEY_DOWN: case 'x':
		row = !row;
		draw();
		break;
	case LV_KEY_LEFT: case 'z':
	case LV_KEY_RIGHT: case 'c': {
		int d = k == LV_KEY_LEFT || k == 'z' ? -1 : 1;
		if (row == 0) { if (nthemes < 2) break; ti = (ti + d + nthemes) % nthemes; }
		else li = (li + d + NLAYOUTS) % NLAYOUTS;
		apply();
		break;
	}
	case LV_KEY_ENTER:
		close_page(1);
		break;
	case LV_KEY_ESC: case LV_KEY_BACKSPACE:
		close_page(0);
		break;
	}
}
