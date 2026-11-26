// SPDX-License-Identifier: MIT
// Drawing helpers over LVGL's draw API, for the one full-screen object everything is drawn on.
// Text handed to lv_draw_label must stay valid until LVGL has drawn it (the end of this area's
// refresh), so it is copied into an arena that is reset at the next draw event.
#include "notes.h"
#include <string.h>

static lv_layer_t *layer;
static char arena[48 * 1024];
static size_t used;

void draw_begin(lv_layer_t *l) { layer = l; used = 0; }

int d_clip_rows(int y, int h) { return y + h > layer->_clip_area.y1 && y <= layer->_clip_area.y2; }

void d_rect(int x, int y, int w, int h, uint32_t c)
{
	if (w <= 0 || h <= 0) return;
	lv_draw_rect_dsc_t d;
	lv_draw_rect_dsc_init(&d);
	d.bg_color = C(c);
	d.bg_opa = LV_OPA_COVER;
	lv_area_t a = { x, y, x + w - 1, y + h - 1 };
	lv_draw_rect(layer, &d, &a);
}

void d_rrect(int x, int y, int w, int h, int r, uint32_t c)
{
	if (w <= 0 || h <= 0) return;
	lv_draw_rect_dsc_t d;
	lv_draw_rect_dsc_init(&d);
	d.bg_color = C(c);
	d.bg_opa = LV_OPA_COVER;
	d.radius = r;
	lv_area_t a = { x, y, x + w - 1, y + h - 1 };
	lv_draw_rect(layer, &d, &a);
}

void d_frame(int x, int y, int w, int h, int bw, uint32_t c)
{
	lv_draw_rect_dsc_t d;
	lv_draw_rect_dsc_init(&d);
	d.bg_opa = LV_OPA_TRANSP;
	d.border_color = C(c);
	d.border_width = bw;
	d.border_opa = LV_OPA_COVER;
	lv_area_t a = { x, y, x + w - 1, y + h - 1 };
	lv_draw_rect(layer, &d, &a);
}

void d_line(int x1, int y1, int x2, int y2, int width, uint32_t c)
{
	lv_draw_line_dsc_t d;
	lv_draw_line_dsc_init(&d);
	d.color = C(c);
	d.width = width;
	d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
	d.round_start = d.round_end = 1;
	lv_draw_line(layer, &d);
}

static int label(int f, int x, int y, uint32_t c, const char *s, size_t n, int decor)
{
	if (!n) return 0;
	int w = text_w(f, s, n);
	lv_draw_label_dsc_t d;
	lv_draw_label_dsc_init(&d);
	d.font = font(f);
	d.color = C(c);
	d.flag = LV_TEXT_FLAG_EXPAND;
	d.decor = (lv_text_decor_t)decor;
	if (used + n + 1 <= sizeof(arena)) {
		memcpy(arena + used, s, n);
		arena[used + n] = 0;
		d.text = arena + used;
		used += n + 1;
	} else {   /* arena full: LVGL copies it */
		char tmp[1024];
		size_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
		memcpy(tmp, s, k);
		tmp[k] = 0;
		d.text = tmp;
		d.text_local = 1;
	}
	lv_area_t a = { x, y, x + w + 2, y + font_lh(f) - 1 };
	lv_draw_label(layer, &d, &a);
	return w;
}

int d_text(int f, int x, int y, uint32_t c, const char *s, size_t n) { return label(f, x, y, c, s, n, LV_TEXT_DECOR_NONE); }

int d_text_deco(int f, int x, int y, uint32_t c, const char *s, size_t n, int strike, int underline)
{
	return label(f, x, y, c, s, n, (strike ? LV_TEXT_DECOR_STRIKETHROUGH : 0) | (underline ? LV_TEXT_DECOR_UNDERLINE : 0));
}

int d_text_fit(int f, int x, int y, int maxw, uint32_t c, const char *s)
{
	size_t n = strlen(s);
	int w = text_w(f, s, n);
	if (w <= maxw) return d_text(f, x, y, c, s, n);
	int ell = text_w(f, "…", 3);
	size_t i = 0, fit = 0;
	int acc = 0;
	while (i < n) {
		size_t j = i;
		uint32_t ch = u8_dec(s, n, &j);
		int gw = glyph_w(f, ch, 0);
		if (acc + gw + ell > maxw) break;
		acc += gw;
		i = fit = j;
	}
	char buf[512];
	if (fit > sizeof(buf) - 4) fit = u8_whole(s, sizeof(buf) - 4);
	memcpy(buf, s, fit);
	memcpy(buf + fit, "…", 3);
	return d_text(f, x, y, c, buf, fit + 3);
}

/* a checkbox: an outline, or filled with a tick */
void d_check(int x, int y, int sz, int ticked)
{
	if (ticked) {
		d_rrect(x, y, sz, sz, 2, pal.accent);
		int a = sz / 5;
		d_line(x + a, y + sz / 2, x + sz * 2 / 5, y + sz - a - 1, sz >= 12 ? 2 : 1, pal.bg);
		d_line(x + sz * 2 / 5, y + sz - a - 1, x + sz - a, y + a, sz >= 12 ? 2 : 1, pal.bg);
	} else {
		d_frame(x, y, sz, sz, 1, pal.dim);
	}
}

/* a picture already in the display's format (emuZero Wiki); LVGL reads it during this refresh */
void d_image(const lv_image_dsc_t *img, int x, int y)
{
	lv_draw_image_dsc_t d;
	lv_draw_image_dsc_init(&d);
	d.src = img;
	lv_area_t a = { x, y, x + (int)img->header.w - 1, y + (int)img->header.h - 1 };
	lv_draw_image(layer, &d, &a);
}
