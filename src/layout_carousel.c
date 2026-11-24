// SPDX-License-Identifier: MIT
// Carousel, in the manner of M5's launcher: three framed cards, the selected one larger in
// the middle, each app's name under its card, arrows at the screen edges and a position bar
// at the bottom. Left/Right (and Up/Down) step through the apps; the ends wrap around.
#include "layout.h"
#include "apps.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

/* card slots: left neighbour, selected, right neighbour */
static const struct { int dx, card, icon, border; } slot[3] = {
	{ -106, 74, 52, 2 }, { 0, 96, 70, 3 }, { 106, 74, 52, 2 },
};
#define CARD_CY 54     /* card centre line inside the area */
#define NAME_Y 108
#define TRACK_W 150
#define TRACK_Y 136

static lv_obj_t *area, *card[3], *name[3], *chev[2], *track, *knob;

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
	for (int i = 0; i < 3; i++) {
		int sel = i == 1;
		card[i] = box(area);
		lv_obj_set_size(card[i], slot[i].card, slot[i].card);
		lv_obj_set_pos(card[i], 160 + slot[i].dx - slot[i].card / 2, CARD_CY - slot[i].card / 2);
		lv_obj_set_style_radius(card[i], slot[i].card / 5, 0);
		lv_obj_set_style_bg_opa(card[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(card[i], lv_color_hex(sel ? theme.sel_bg : theme.bg), 0);
		lv_obj_set_style_border_width(card[i], slot[i].border, 0);
		lv_obj_set_style_border_color(card[i], lv_color_hex(sel ? theme.fg : theme.line), 0);

		lv_font_t *f = ui_font(sel ? 15 : 13, 1);
		name[i] = ui_label(area, f, sel ? theme.fg : theme.dim, "");
		lv_obj_set_size(name[i], sel ? 112 : 84, lv_font_get_line_height(f));
		lv_label_set_long_mode(name[i], LV_LABEL_LONG_DOT);
		lv_obj_set_style_text_align(name[i], LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_pos(name[i], 160 + slot[i].dx - (sel ? 56 : 42), NAME_Y + (sel ? 0 : 2));
	}
	for (int i = 0; i < 2; i++) {
		chev[i] = ui_label(area, ui_font(24, 1), theme.dim, i ? "\u203a" : "\u2039");   /* single angle quotes */
		lv_obj_align(chev[i], i ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, i ? -2 : 2, CARD_CY - 16);
	}
	track = box(area);
	lv_obj_set_size(track, TRACK_W, 4);
	lv_obj_set_pos(track, 160 - TRACK_W / 2, TRACK_Y);
	lv_obj_set_style_radius(track, 2, 0);
	lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(track, lv_color_hex(theme.line), 0);
	knob = box(area);
	lv_obj_set_size(knob, 10, 10);
	lv_obj_set_style_radius(knob, 5, 0);
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
	for (int i = 0; i < 2; i++) hide(chev[i], !many);
	hide(track, !many);
	hide(knob, !many);
	if (many) lv_obj_set_pos(knob, 160 - TRACK_W / 2 + (TRACK_W - 10) * sel / (napps - 1), TRACK_Y - 3);
}

static int move(uint32_t key, int sel, int n)
{
	if (key == LV_KEY_LEFT || key == LV_KEY_UP) return (sel + n - 1) % n;
	if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) return (sel + 1) % n;
	return sel;
}

const struct layout layout_carousel = { "carousel", create, show, move };
