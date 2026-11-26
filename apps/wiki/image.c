// SPDX-License-Identifier: MIT
// Pictures: the guides' JPEGs, decoded with LVGL's own TJpgDec (third_party/lvgl/src/libs/tjpgd,
// compiled into this program only), scaled down by averaging, kept as RGB565 in a small cache.
// In an article a picture is fitted to the text width (at most PIC_MAX_H high); Enter on it shows
// it on the whole screen, where + / Enter switches between fitted and actual size and the arrows
// move around.
#include "wiki.h"
#include "lvgl/src/libs/tjpgd/tjpgd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIC_MAX_H 112
#define NCACHE 8

struct pic {
	int pack; char name[64];
	int w, h;                  /* as stored */
	int tw, th;                /* as cached */
	uint16_t *px;
	lv_image_dsc_t dsc;
	uint32_t used;
};
static struct pic cache[NCACHE];
static struct { int pack; char name[64]; int w, h; } sizes[64];
static int nsizes;
static uint32_t tick;

/* ---- decoding ---- */

struct src { const uint8_t *d; size_t n, at; uint8_t *rgb; int w; };

static size_t in_cb(JDEC *jd, uint8_t *buf, size_t len)
{
	struct src *s = jd->device;
	if (len > s->n - s->at) len = s->n - s->at;
	if (buf) memcpy(buf, s->d + s->at, len);
	s->at += len;
	return len;
}

static int out_cb(JDEC *jd, void *bitmap, JRECT *r)
{
	struct src *s = jd->device;
	const uint8_t *b = bitmap;
	int rw = r->right - r->left + 1;
	for (int y = r->top; y <= r->bottom; y++)
		memcpy(s->rgb + ((size_t)y * (size_t)s->w + r->left) * 3, b + (size_t)(y - r->top) * (size_t)rw * 3, (size_t)rw * 3);
	return 1;
}

static int jpeg_open(const uint8_t *d, size_t n, JDEC *jd, void *pool, struct src *s)
{
	memset(s, 0, sizeof(*s));
	s->d = d; s->n = n;
	return jd_prepare(jd, in_cb, pool, 8192, s) == JDR_OK ? 0 : -1;
}

/* RGB888, malloc'd */
static uint8_t *decode(int p, const char *name, int *w, int *h)
{
	uint8_t *d;
	size_t n;
	if (pack_image(p, name, &d, &n)) return NULL;
	static uint8_t pool[8192] __attribute__((aligned(8)));
	JDEC jd;
	struct src s;
	uint8_t *rgb = NULL;
	if (!jpeg_open(d, n, &jd, pool, &s)) {
		s.w = jd.width;
		rgb = malloc((size_t)jd.width * jd.height * 3);
		s.rgb = rgb;
		if (rgb && jd_decomp(&jd, out_cb, 0) == JDR_OK) { *w = jd.width; *h = jd.height; }
		else { free(rgb); rgb = NULL; }
	}
	free(d);
	return rgb;
}

int pic_size(int p, const char *name, int *w, int *h)
{
	for (int i = 0; i < nsizes; i++)
		if (sizes[i].pack == p && !strcmp(sizes[i].name, name)) { *w = sizes[i].w; *h = sizes[i].h; return *w ? 0 : -1; }
	uint8_t *d;
	size_t n;
	*w = *h = 0;
	if (!pack_image(p, name, &d, &n)) {
		static uint8_t pool[8192] __attribute__((aligned(8)));
		JDEC jd;
		struct src s;
		if (!jpeg_open(d, n, &jd, pool, &s)) { *w = jd.width; *h = jd.height; }
		free(d);
	}
	if (nsizes < 64) { sizes[nsizes].pack = p; snprintf(sizes[nsizes].name, 64, "%s", name); sizes[nsizes].w = *w; sizes[nsizes].h = *h; nsizes++; }
	return *w ? 0 : -1;
}

/* rgb (w x h) averaged down (or repeated up) to tw x th, RGB565 */
static uint16_t *scale(const uint8_t *rgb, int w, int h, int tw, int th)
{
	uint16_t *o = malloc((size_t)tw * th * 2);
	if (!o) return NULL;
	for (int y = 0; y < th; y++) {
		int y0 = y * h / th, y1 = (y + 1) * h / th;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < tw; x++) {
			int x0 = x * w / tw, x1 = (x + 1) * w / tw;
			if (x1 <= x0) x1 = x0 + 1;
			unsigned r = 0, g = 0, b = 0, c = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++) {
					const uint8_t *q = rgb + ((size_t)yy * w + xx) * 3;
					r += q[0]; g += q[1]; b += q[2]; c++;
				}
			r /= c; g /= c; b /= c;
			o[(size_t)y * tw + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
		}
	}
	return o;
}

static void drop(struct pic *c)
{
	if (c->px) { lv_image_cache_drop(&c->dsc); lv_image_header_cache_drop(&c->dsc); }
	free(c->px);
	memset(c, 0, sizeof(*c));
}

void pics_clear(void) { for (int i = 0; i < NCACHE; i++) drop(&cache[i]); }

static struct pic *get(int p, const char *name, int tw, int th)
{
	for (int i = 0; i < NCACHE; i++) {
		struct pic *c = &cache[i];
		if (c->px && c->pack == p && c->tw == tw && c->th == th && !strcmp(c->name, name)) { c->used = ++tick; return c; }
	}
	int w, h;
	uint8_t *rgb = decode(p, name, &w, &h);
	if (!rgb) return NULL;
	struct pic *c = &cache[0];
	for (int i = 1; i < NCACHE; i++) if (cache[i].used < c->used) c = &cache[i];
	drop(c);
	c->px = scale(rgb, w, h, tw, th);
	free(rgb);
	if (!c->px) return NULL;
	c->pack = p; scopy(c->name, sizeof(c->name), name);
	c->w = w; c->h = h; c->tw = tw; c->th = th; c->used = ++tick;
	c->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
	c->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
	c->dsc.header.w = (uint32_t)tw;
	c->dsc.header.h = (uint32_t)th;
	c->dsc.header.stride = (uint32_t)tw * 2;
	c->dsc.data = (const uint8_t *)c->px;
	c->dsc.data_size = (uint32_t)tw * th * 2;
	return c;
}

static void fit(int w, int h, int maxw, int maxh, int *tw, int *th)
{
	*tw = maxw; *th = h * maxw / w;
	if (*th > maxh) { *th = maxh; *tw = w * maxh / h; }
	if (*tw < 1) *tw = 1;
	if (*th < 1) *th = 1;
}

/* draw a picture fitted into the box (full: as large as fits; else at most PIC_MAX_H high) */
void pic_draw_fit(int p, const char *name, int x, int y, int w, int h, int full)
{
	int iw, ih, tw, th;
	if (pic_size(p, name, &iw, &ih)) {
		d_text(F_UI, x, y + 1, pal.dim, "(picture missing)", 17);
		return;
	}
	fit(iw, ih, w, full ? h : (h < PIC_MAX_H ? h : PIC_MAX_H), &tw, &th);
	struct pic *c = get(p, name, tw, th);
	if (c) d_image(&c->dsc, x + (w - tw) / 2, y + (h - th) / 2);
}

/* ---- the hook for the reader (view.c): ![alt](img:NAME) lines ---- */

extern int reader_pack(void);

static void src_name(const char *src, size_t n, char *out, size_t max)
{
	if (n >= 4 && !strncmp(src, "img:", 4)) { src += 4; n -= 4; }
	if (n >= max) n = max - 1;
	memcpy(out, src, n);
	out[n] = 0;
}

static int hook_height(const char *src, size_t n, int w)
{
	char name[64];
	int iw, ih, tw, th;
	src_name(src, n, name, sizeof(name));
	if (pic_size(reader_pack(), name, &iw, &ih)) return font_lh(F_UI) + 2;
	fit(iw, ih, w, PIC_MAX_H, &tw, &th);
	return th;
}

static void hook_draw(const char *src, size_t n, int x, int y, int w, int h)
{
	char name[64];
	src_name(src, n, name, sizeof(name));
	pic_draw_fit(reader_pack(), name, x, y, w, h, 0);
}

const struct view_pic_hook wiki_pics = { hook_height, hook_draw };

/* ---- the picture on the whole screen ---- */

static int zoom, px, py;   /* 0 fitted, 1 actual size; the view's offset at actual size */

void pic_enter(void) { zoom = 0; px = py = 0; }

static void clamp(void)
{
	struct scr *s = cur_scr();
	int iw, ih;
	if (pic_size(s->pack, s->name, &iw, &ih)) return;
	int vw = SCREEN_W, vh = SCREEN_H - BAR_H;
	if (px > iw - vw) px = iw - vw;
	if (py > ih - vh) py = ih - vh;
	if (px < 0) px = 0;
	if (py < 0) py = 0;
}

void pic_key(struct nkey k)
{
	int cmd = wcmd_lookup(X_PIC, k);
	switch (cmd) {
	case W_ZOOM: {
		struct scr *s = cur_scr();
		int iw, ih;
		zoom = !zoom;
		if (zoom && !pic_size(s->pack, s->name, &iw, &ih)) { px = (iw - SCREEN_W) / 2; py = (ih - SCREEN_H + BAR_H) / 2; }
		break;
	}
	case W_UP: py -= 40; break;
	case W_DOWN: py += 40; break;
	case W_LEFT: px -= 60; break;
	case W_RIGHT: px += 60; break;
	default: wiki_cmd(cmd); return;
	}
	clamp();
	trace("picture %s zoom=%d at=%d,%d", cur_scr()->name, zoom, px, py);
	redraw();
}

void pic_draw(void)
{
	struct scr *s = cur_scr();
	d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, 0x000000);
	int iw, ih;
	if (!zoom || pic_size(s->pack, s->name, &iw, &ih)) pic_draw_fit(s->pack, s->name, 0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, 1);
	else {
		struct pic *c = get(s->pack, s->name, iw, ih);
		if (c) d_image(&c->dsc, -px, BAR_H - py);
	}
	bar_draw("Picture", zoom ? "actual size · arrows move" : "+ actual size", pal.dim);
}
