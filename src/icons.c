// SPDX-License-Identifier: MIT
#include "icons.h"
#include "ui.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_MAX 256
static struct { char path[512]; int size; lv_draw_buf_t *buf; } cache[CACHE_MAX];
static int ncache;

/* Decode a PNG and fit it into size x size (centred, aspect kept) as ARGB8888. Pixels are
 * averaged over the source area they cover, weighted by alpha so edges do not darken. */
static lv_draw_buf_t *load_scaled(const char *path, int size)
{
	png_image im = { .version = PNG_IMAGE_VERSION };
	if (!png_image_begin_read_from_file(&im, path)) return NULL;
	im.format = PNG_FORMAT_BGRA;   /* LVGL's ARGB8888 byte order */
	if (im.width == 0 || im.height == 0 || im.width > 1024 || im.height > 1024) { png_image_free(&im); return NULL; }
	uint8_t *src = malloc(PNG_IMAGE_SIZE(im));
	if (!src || !png_image_finish_read(&im, NULL, src, 0, NULL)) { free(src); png_image_free(&im); return NULL; }

	int sw = im.width, sh = im.height, big = sw > sh ? sw : sh;
	int dw = sw * size / big, dh = sh * size / big;
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	lv_draw_buf_t *buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, 0);
	if (!buf) { free(src); return NULL; }
	lv_draw_buf_clear(buf, NULL);
	int ox = (size - dw) / 2, oy = (size - dh) / 2;
	for (int y = 0; y < dh; y++) {
		int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
		if (y1 <= y0) y1 = y0 + 1;
		uint8_t *out = buf->data + (size_t)(oy + y) * buf->header.stride + (size_t)ox * 4;
		for (int x = 0; x < dw; x++, out += 4) {
			int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
			if (x1 <= x0) x1 = x0 + 1;
			uint32_t b = 0, g = 0, r = 0, a = 0, n = 0;
			for (int sy = y0; sy < y1; sy++)
				for (int sx = x0; sx < x1; sx++) {
					const uint8_t *p = src + ((size_t)sy * sw + sx) * 4;
					b += p[0] * p[3]; g += p[1] * p[3]; r += p[2] * p[3]; a += p[3]; n++;
				}
			if (a) { out[0] = b / a; out[1] = g / a; out[2] = r / a; }
			out[3] = a / n;
		}
	}
	free(src);
	return buf;
}

static lv_draw_buf_t *cached(const char *path, int size)
{
	for (int i = 0; i < ncache; i++)
		if (cache[i].size == size && !strcmp(cache[i].path, path)) return cache[i].buf;
	lv_draw_buf_t *buf = load_scaled(path, size);
	if (ncache < CACHE_MAX) {   /* failures are cached too, so a broken file is read once */
		snprintf(cache[ncache].path, sizeof(cache[ncache].path), "%s", path);
		cache[ncache].size = size;
		cache[ncache++].buf = buf;
	}
	return buf;
}

/* A picture filling w x h (scaled to cover, centred, the rest cut off), for wallpapers. One is
 * kept; loading another frees it. */
lv_draw_buf_t *picture_load(const char *path, int w, int h)
{
	static lv_draw_buf_t *last;
	if (last) { lv_draw_buf_destroy(last); last = NULL; }
	png_image im = { .version = PNG_IMAGE_VERSION };
	if (!png_image_begin_read_from_file(&im, path)) return NULL;
	im.format = PNG_FORMAT_BGRA;
	if (im.width == 0 || im.height == 0 || im.width > 2048 || im.height > 2048) { png_image_free(&im); return NULL; }
	uint8_t *src = malloc(PNG_IMAGE_SIZE(im));
	if (!src || !png_image_finish_read(&im, NULL, src, 0, NULL)) { free(src); png_image_free(&im); return NULL; }
	int sw = im.width, sh = im.height;
	/* the source rectangle with the screen's shape */
	int cw = sw, ch = sw * h / w;
	if (ch > sh) { ch = sh; cw = sh * w / h; }
	int cx = (sw - cw) / 2, cy = (sh - ch) / 2;
	lv_draw_buf_t *buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
	if (buf)
		for (int y = 0; y < h; y++) {
			uint8_t *out = buf->data + (size_t)y * buf->header.stride;
			const uint8_t *row = src + ((size_t)(cy + y * ch / h) * sw) * 4;
			for (int x = 0; x < w; x++, out += 4) memcpy(out, row + (size_t)(cx + x * cw / w) * 4, 4);
		}
	free(src);
	return last = buf;
}

void icon_cache_clear(void)
{
	for (int i = 0; i < ncache; i++)
		if (cache[i].buf) lv_draw_buf_destroy(cache[i].buf);
	ncache = 0;
}

/* muted tile colours; an app keeps its colour because it comes from the id */
static const uint32_t tile_colors[] = { 0x3d6fb6, 0x2f8f83, 0x7a5cb8, 0xb5603a, 0x5b8a3c, 0xa8487a, 0x4a7f9e, 0x9a7b2f };

static uint32_t tile_color(const char *id)
{
	uint32_t h = 2166136261u;
	for (const char *p = id; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
	return tile_colors[h % (sizeof(tile_colors) / sizeof(tile_colors[0]))];
}

static lv_obj_t *tile(lv_obj_t *parent, const struct app *a, int size)
{
	lv_obj_t *t = lv_obj_create(parent);
	lv_obj_remove_style_all(t);
	lv_obj_set_size(t, size, size);
	lv_obj_set_style_radius(t, size / 5, 0);
	lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(t, lv_color_hex(tile_color(a->id)), 0);
	lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

	/* first letter (first UTF-8 character) of the name */
	char first[5] = { 0 };
	const unsigned char *n = (const unsigned char *)a->name;
	int len = n[0] < 0x80 ? 1 : n[0] < 0xe0 ? 2 : n[0] < 0xf0 ? 3 : 4;
	memcpy(first, n, len);
	if (len == 1 && first[0] >= 'a' && first[0] <= 'z') first[0] -= 32;
	lv_obj_t *l = lv_label_create(t);
	lv_obj_set_style_text_font(l, ui_font(size * 11 / 20, 1), 0);
	lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
	lv_label_set_text(l, first);
	lv_obj_center(l);
	return t;
}

lv_obj_t *icon_create(lv_obj_t *parent, const struct app *a, int size)
{
	lv_draw_buf_t *buf = a->icon[0] ? cached(a->icon, size) : NULL;
	if (!buf) return tile(parent, a, size);
	lv_obj_t *img = lv_image_create(parent);
	lv_image_set_src(img, buf);
	lv_obj_set_size(img, size, size);
	return img;
}
