// SPDX-License-Identifier: MIT
// Carousel, in the manner of M5's launcher: three framed cards, the selected one larger in
// the middle, each app's name under its card, arrows at the screen edges and a position bar
// at the bottom. Left/Right (and Up/Down) step through the apps; the ends wrap around.
#include "layout.h"
#include "apps.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

/* geometry from the area's size (a theme's window frame makes it smaller): the selected card
 * as large as fits up to 96 px, its neighbours at 3/4; then the name, then the position bar */
static int W, H, S, CY, NAME_Y, TRACK_Y, DX;
static struct { int dx, card, icon, border; } slot[3];
#define TRACK_W 150

static lv_obj_t *area, *card[3], *name[3], *chev[2], *track, *knob;

static void geometry(lv_obj_t *a)
{
	W = lv_obj_get_width(a);
	H = lv_obj_get_height(a);
	int avail = H - (theme.track ? 15 : 2) - 24 - 12;
	S = avail < 96 ? avail : 96;
	if (S < 40) S = 40;
	CY = 6 + S / 2;
	NAME_Y = CY + S / 2 + 6;
	TRACK_Y = H - 15;
	DX = W / 3 < 106 ? W / 3 : 106;
	int side = S * 74 / 96;
	slot[0] = (typeof(slot[0])){ -DX, side, S * 52 / 96, theme.card_border };
	slot[1] = (typeof(slot[1])){ 0, S, S * 70 / 96, theme.card_sel_border };
	slot[2] = (typeof(slot[2])){ DX, side, S * 52 / 96, theme.card_border };
}

static lv_obj_t *box(lv_obj_t *parent)
{
	lv_obj_t *o = lv_obj_create(parent);
	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	return o;
}

static void hide(lv_obj_t *o, int hidden)
{
	if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
	else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void create(lv_obj_t *a)
{
	area = a;
	geometry(a);
	int cx = W / 2;
	for (int i = 0; i < 3; i++) {
		int sel = i == 1;
		card[i] = box(area);
		lv_obj_set_size(card[i], slot[i].card, slot[i].card);
		lv_obj_set_pos(card[i], cx + slot[i].dx - slot[i].card / 2, CY - slot[i].card / 2);
		lv_obj_set_style_radius(card[i], slot[i].card * theme.card_radius / 100, 0);
		lv_obj_set_style_bg_opa(card[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(card[i], lv_color_hex(sel ? theme.card_sel_bg : theme.card_bg), 0);
		lv_obj_set_style_border_width(card[i], slot[i].border, 0);
		lv_obj_set_style_border_color(card[i], lv_color_hex(sel ? theme.card_sel_line : theme.card_line), 0);

		lv_font_t *f = ui_font(sel ? 15 : 13, 1);
		int nw = sel ? (DX + 6 < 112 ? DX + 6 : 112) : (DX - 22 < 84 ? DX - 22 : 84);
		name[i] = ui_label(area, f, sel ? theme.name_sel_fg : theme.name_fg, "");
		lv_obj_set_size(name[i], nw, lv_font_get_line_height(f));
		lv_label_set_long_mode(name[i], LV_LABEL_LONG_DOT);
		lv_obj_set_style_text_align(name[i], LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_pos(name[i], cx + slot[i].dx - nw / 2, NAME_Y + (sel ? 0 : 2));
	}
	for (int i = 0; i < 2; i++) {
		chev[i] = ui_label(area, ui_font(24, 1), theme.dim, i ? "\u203a" : "\u2039");   /* single angle quotes */
		lv_obj_align(chev[i], i ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, i ? -2 : 2, CY - 16);
	}
	track = box(area);
	lv_obj_set_size(track, TRACK_W, 4);
	lv_obj_set_pos(track, cx - TRACK_W / 2, TRACK_Y);
	lv_obj_set_style_radius(track, 2, 0);
	lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(track, lv_color_hex(theme.line), 0);
	knob = box(area);
	lv_obj_set_size(knob, 10, 10);
	lv_obj_set_style_radius(knob, theme.card_radius ? 5 : 0, 0);
	lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(knob, lv_color_hex(theme.accent), 0);
}

static void show(int sel)
{
	/* with one or two apps, show each once (the second on the right) */
	for (int i = 0; i < 3; i++) {
		int off = i - 1, idx = napps ? (sel + off + napps) % napps : 0;
		int on = napps > 0 && (off == 0 || (off > 0 && napps >= 2) || (off < 0 && napps >= 3));
		lv_obj_clean(card[i]);
		if (on) lv_obj_center(icon_create(card[i], &apps[idx], slot[i].icon));
		hide(card[i], !on && off != 0);
		lv_label_set_text(name[i], on ? apps[idx].name : off ? "" : "No apps installed");
	}
	int many = napps > 1;
	for (int i = 0; i < 2; i++) hide(chev[i], !many || !theme.chevrons);
	hide(track, !many || !theme.track);
	hide(knob, !many || !theme.track);
	if (many) lv_obj_set_pos(knob, W / 2 - TRACK_W / 2 + (TRACK_W - 10) * sel / (napps - 1), TRACK_Y - 3);
}

static int move(uint32_t key, int sel, int n)
{
	if (key == LV_KEY_LEFT || key == LV_KEY_UP) return (sel + n - 1) % n;
	if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) return (sel + 1) % n;
	return sel;
}

const struct layout layout_carousel = { "carousel", create, show, move };
