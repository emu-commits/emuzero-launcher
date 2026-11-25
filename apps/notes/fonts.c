// SPDX-License-Identifier: MIT
// Fonts and colours: the launcher's theme (chosen on its Theme page) gives the colours and the
// regular and bold faces; italic and monospace come from DejaVu, which the image has. Three
// text sizes for the note itself; the bars and menus keep one size.
#include "notes.h"
#include "theme.h"
#include "paths.h"
#include <stdio.h>
#include <string.h>

struct palette pal;
int text_size = 1;

static const lv_font_t *fonts[F_COUNT];
static int lh[F_COUNT], asc[F_COUNT];
static struct { char file[320]; int px; lv_font_t *f; } made[32];
static int nmade;

static lv_font_t *get(const char *file, int px)
{
	for (int i = 0; i < nmade; i++)
		if (made[i].px == px && !strcmp(made[i].file, file)) return made[i].f;
	lv_font_t *f = lv_freetype_font_create(file, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, (uint32_t)px, LV_FREETYPE_FONT_STYLE_NORMAL);
	if (!f) return NULL;
	if (nmade < (int)(sizeof(made) / sizeof(made[0]))) {
		snprintf(made[nmade].file, sizeof(made[nmade].file), "%s", file);
		made[nmade].px = px;
		made[nmade++].f = f;
	}
	return f;
}

static char files[4][320];   /* regular, bold, italic, bold italic, mono below */
static char mono_file[320];

static void set(int which, const char *file, int px)
{
	const lv_font_t *f = get(file, px);
	if (!f) f = get(files[0], px);
	fonts[which] = f;
	lh[which] = f ? lv_font_get_line_height(f) : px + 2;
	asc[which] = f ? lv_font_get_line_height(f) - f->base_line : px;
}

void fonts_set_size(int size)
{
	static const int body[3] = { 12, 14, 17 };
	text_size = size < 0 ? 0 : size > 2 ? 2 : size;
	int px = body[text_size];
	set(F_REG, files[0], px);
	set(F_BOLD, files[1], px);
	set(F_ITAL, files[2], px);
	set(F_BOLDITAL, files[3], px);
	set(F_MONO, mono_file, px - 1);
	set(F_H1, files[1], px + 6);
	set(F_H2, files[1], px + 3);
	set(F_H3, files[1], px + 1);
}

static uint32_t mix(uint32_t a, uint32_t b, int pct)   /* pct of b over a */
{
	uint32_t r = 0;
	for (int s = 0; s < 24; s += 8) {
		int ca = (int)(a >> s & 255), cb = (int)(b >> s & 255);
		r |= (uint32_t)(ca + (cb - ca) * pct / 100) << s;
	}
	return r;
}

int fonts_init(const char *theme_id)
{
	struct settings s;
	settings_load(&s);
	theme_load(theme_id ? theme_id : s.theme);
	pal.bg = theme.win ? theme.win_bg : theme.bg;
	pal.fg = theme.fg; pal.dim = theme.dim; pal.line = theme.line; pal.accent = theme.accent;
	pal.sel_bg = theme.sel_bg; pal.sel_fg = theme.sel_fg;
	if (theme.win && theme.win_client != COLOR_UNSET && theme.win_client != COLOR_NONE) pal.bg = theme.win_client;
	pal.bar_bg = theme.bar_bg; pal.bar_fg = theme.bar_fg; pal.bar_line = theme.bar_line_color; pal.title_fg = theme.title_fg;
	if (theme.title_bg != COLOR_UNSET && theme.title_bg != COLOR_NONE) pal.title_fg = theme.title_fg;
	pal.code_bg = mix(pal.bg, pal.fg, 9);
	pal.mark_bg = mix(pal.bg, 0xffd23f, 45);
	pal.warn = 0xe5484d;
	pal.ok = mix(pal.dim, 0x46a758, 50);
	pal.shadow_c = mix(pal.bg, 0x000000, 45);
	/* a selection colour too close to the background (some themes) would not show */
	if (theme.sel_bg == pal.bg) pal.sel_bg = mix(pal.bg, pal.accent, 30);

	lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
	snprintf(files[0], sizeof(files[0]), "%s", theme.font[0][0] ? theme.font[0] : (path_font(0) ? path_font(0) : ""));
	snprintf(files[1], sizeof(files[1]), "%s", theme.font[1][0] ? theme.font[1] : (path_font(1) ? path_font(1) : ""));
	const char *f;
	snprintf(files[2], sizeof(files[2]), "%s", (f = path_font_named("DejaVuSans-Oblique.ttf")) ? f : files[0]);
	snprintf(files[3], sizeof(files[3]), "%s", (f = path_font_named("DejaVuSans-BoldOblique.ttf")) ? f : files[1]);
	snprintf(mono_file, sizeof(mono_file), "%s", (f = path_font_named("DejaVuSansMono.ttf")) ? f : files[0]);
	if (!files[0][0] || !get(files[0], 14)) { fprintf(stderr, "emuzero-notes: no fonts found (DejaVu)\n"); return -1; }
	set(F_UI, files[0], 11);
	set(F_UIBOLD, files[1], 11);
	set(F_UISMALL, files[0], 10);
	fonts_set_size(text_size);
	return 0;
}

const lv_font_t *font(int f) { return fonts[f]; }
int font_lh(int f) { return lh[f]; }
int font_asc(int f) { return asc[f]; }
lv_color_t C(uint32_t rgb) { return lv_color_hex(rgb); }

int glyph_w(int f, uint32_t c, uint32_t next)
{
	if (c == '\t') c = ' ';
	return lv_font_get_glyph_width(fonts[f], c, next);
}

int text_w(int f, const char *s, size_t n)
{
	int w = 0;
	size_t i = 0;
	while (i < n) {
		uint32_t c = u8_dec(s, n, &i);
		size_t j = i;
		uint32_t nx = i < n ? u8_dec(s, n, &j) : 0;
		w += glyph_w(f, c, nx);
	}
	return w;
}
