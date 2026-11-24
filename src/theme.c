// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "theme.h"
#include "paths.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct theme theme;

#define UNSET 0xff000000u   /* a colour not given: derived from the main colours afterwards */

static void defaults(struct theme *t)
{
	memset(t, 0, sizeof(*t));
	snprintf(t->id, sizeof(t->id), "minimal");
	snprintf(t->name, sizeof(t->name), "Minimal");
	t->bg = 0x101418; t->fg = 0xf0f0f0; t->dim = 0x8a96a6; t->line = 0x2a3038;
	t->accent = 0x4fa3ff; t->sel_bg = 0x1e2a38;
	t->sel_fg = t->light = t->shadow = UNSET;
	snprintf(t->layout, sizeof(t->layout), "carousel");
	t->chevrons = t->track = 1;
	t->card_radius = 20; t->card_border = 2; t->card_sel_border = 3;
	t->card_bg = t->card_sel_bg = t->card_line = t->card_sel_line = t->name_fg = t->name_sel_fg = UNSET;
	t->bar_h = 18; t->bar_line = 1;
	t->bar_bg = t->bar_fg = t->bar_line_color = t->title_fg = t->badge_bg = t->badge_fg = UNSET;
	snprintf(t->title, sizeof(t->title), "emuZero");
	t->badge_style = BADGE_ROUND;
	t->win_margin = 4; t->win_border = 1; t->win_title_h = 14;
	t->win_bg = t->win_border_color = t->win_title_bg = t->win_title_fg = UNSET;
}

static void derive(struct theme *t)
{
#define D(f, v) if (t->f == UNSET) t->f = (v)
	D(sel_fg, t->fg); D(light, 0xffffff); D(shadow, 0x000000);
	D(card_bg, t->bg); D(card_sel_bg, t->sel_bg); D(card_line, t->line); D(card_sel_line, t->fg);
	D(name_fg, t->dim); D(name_sel_fg, t->fg);
	D(bar_bg, t->bg); D(bar_fg, t->fg); D(bar_line_color, t->line); D(title_fg, t->accent);
	D(badge_bg, t->line); D(badge_fg, t->fg);
	D(win_bg, t->bg); D(win_border_color, t->fg); D(win_title_bg, t->bg); D(win_title_fg, t->fg);
#undef D
}

static char *trim(char *s)
{
	while (isspace((unsigned char)*s)) s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
	return s;
}

static int color(const char *v, uint32_t *out)
{
	if (*v == '#') v++;
	char *end;
	unsigned long c = strtoul(v, &end, 16);
	if (end - v != 6 || *end) return -1;
	*out = (uint32_t)c;
	return 0;
}

static int yes(const char *v)
{
	return !strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcmp(v, "1");
}

static int num(const char *v, int lo, int hi, int def)
{
	char *end;
	long n = strtol(v, &end, 10);
	return *v && !*end && n >= lo && n <= hi ? (int)n : def;
}

/* a file named in theme.conf: absolute, in the theme folder, or (fonts) a system font */
static void file(char *out, size_t n, const char *dir, const char *v, int font)
{
	char p[700];
	out[0] = 0;
	if (v[0] == '/') {
		path_join(p, sizeof(p), v);
		if (!access(p, R_OK)) snprintf(out, n, "%.319s", p);
		return;
	}
	if (strstr(v, "..")) return;
	snprintf(p, sizeof(p), "%s/%s", dir, v);
	if (!access(p, R_OK)) { snprintf(out, n, "%.319s", p); return; }
	if (font) {
		const char *f = path_font_named(v);
		if (f) snprintf(out, n, "%.319s", f);
	}
}

static void set(struct theme *t, const char *sec, const char *k, const char *v, int line)
{
	struct { const char *sec, *key; uint32_t *c; } colors[] = {
		{ "colors", "background", &t->bg }, { "colors", "text", &t->fg }, { "colors", "dim", &t->dim },
		{ "colors", "line", &t->line }, { "colors", "accent", &t->accent }, { "colors", "selected", &t->sel_bg },
		{ "colors", "selected_text", &t->sel_fg }, { "colors", "light", &t->light }, { "colors", "shadow", &t->shadow },
		{ "home", "card", &t->card_bg }, { "home", "card_selected", &t->card_sel_bg },
		{ "home", "card_line", &t->card_line }, { "home", "card_selected_line", &t->card_sel_line },
		{ "home", "name", &t->name_fg }, { "home", "name_selected", &t->name_sel_fg },
		{ "bar", "background", &t->bar_bg }, { "bar", "text", &t->bar_fg }, { "bar", "line_color", &t->bar_line_color },
		{ "bar", "title_color", &t->title_fg }, { "bar", "badge", &t->badge_bg }, { "bar", "badge_text", &t->badge_fg },
		{ "window", "background", &t->win_bg }, { "window", "border_color", &t->win_border_color },
		{ "window", "title_background", &t->win_title_bg }, { "window", "title_color", &t->win_title_fg },
	};
	struct { const char *sec, *key; int *i, lo, hi; } nums[] = {
		{ "home", "card_radius", &t->card_radius, 0, 50 }, { "home", "card_border", &t->card_border, 0, 6 },
		{ "home", "card_selected_border", &t->card_sel_border, 0, 6 },
		{ "bar", "height", &t->bar_h, 0, 30 },
		{ "window", "margin", &t->win_margin, 0, 20 }, { "window", "border", &t->win_border, 0, 4 },
		{ "window", "title_height", &t->win_title_h, 0, 24 },
	};
	struct { const char *sec, *key; int *b; } flags[] = {
		{ "home", "chevrons", &t->chevrons }, { "home", "track", &t->track }, { "bar", "line", &t->bar_line },
		{ "window", "show", &t->win }, { "window", "stripes", &t->win_stripes }, { "window", "close_box", &t->win_close },
	};
	for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
		if (!strcmp(sec, colors[i].sec) && !strcmp(k, colors[i].key)) {
			if (color(v, colors[i].c)) fprintf(stderr, "emuzero-launcher: theme %s line %d: %s is not a colour (#rrggbb)\n", t->id, line, v);
			return;
		}
	for (size_t i = 0; i < sizeof(nums) / sizeof(nums[0]); i++)
		if (!strcmp(sec, nums[i].sec) && !strcmp(k, nums[i].key)) { *nums[i].i = num(v, nums[i].lo, nums[i].hi, *nums[i].i); return; }
	for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
		if (!strcmp(sec, flags[i].sec) && !strcmp(k, flags[i].key)) { *flags[i].b = yes(v); return; }
	if (!*sec && !strcmp(k, "name")) snprintf(t->name, sizeof(t->name), "%s", v);
	else if (!strcmp(sec, "fonts") && !strcmp(k, "regular")) file(t->font[0], sizeof(t->font[0]), t->dir, v, 1);
	else if (!strcmp(sec, "fonts") && !strcmp(k, "bold")) file(t->font[1], sizeof(t->font[1]), t->dir, v, 1);
	else if (!strcmp(sec, "home") && !strcmp(k, "layout")) snprintf(t->layout, sizeof(t->layout), "%s", v);
	else if (!strcmp(sec, "home") && !strcmp(k, "wallpaper")) file(t->wallpaper, sizeof(t->wallpaper), t->dir, v, 0);
	else if (!strcmp(sec, "bar") && !strcmp(k, "position")) t->bar_bottom = !strcmp(v, "bottom");
	else if (!strcmp(sec, "bar") && !strcmp(k, "title")) snprintf(t->title, sizeof(t->title), "%s", v);
	else if (!strcmp(sec, "bar") && !strcmp(k, "badges"))
		t->badge_style = !strcmp(v, "square") ? BADGE_SQUARE : !strcmp(v, "plain") ? BADGE_PLAIN : !strcmp(v, "sunken") ? BADGE_SUNKEN : BADGE_ROUND;
	else if (!strcmp(sec, "window") && !strcmp(k, "title")) snprintf(t->win_title, sizeof(t->win_title), "%s", v);
	else if (!strcmp(sec, "window") && !strcmp(k, "bevel"))
		t->win_bevel = !strcmp(v, "raised") ? BEVEL_RAISED : !strcmp(v, "sunken") ? BEVEL_SUNKEN : BEVEL_NONE;
	else fprintf(stderr, "emuzero-launcher: theme %s line %d: unknown key %s%s%s\n", t->id, line, sec, *sec ? "." : "", k);
}

/* the folders themes come from, the user's first */
static int theme_dirs(char dirs[2][320])
{
	int n = 0;
	const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
	if (x && *x) snprintf(dirs[n++], 320, "%s/emuzero/themes", x);
	else if (h && *h) snprintf(dirs[n++], 320, "%s/.local/share/emuzero/themes", h);
	path_join(dirs[n++], 320, "/usr/share/emuzero/themes");
	return n;
}

static int valid_id(const char *id)
{
	if (!id[0] || id[0] == '.' || strlen(id) >= 32) return 0;
	for (const char *p = id; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return 0;
	return 1;
}

int theme_load(const char *id)
{
	struct theme t;
	defaults(&t);
	char dirs[2][320], conf[700];
	int nd = theme_dirs(dirs), found = 0;
	FILE *f = NULL;
	for (int i = 0; valid_id(id) && i < nd && !f; i++) {
		snprintf(conf, sizeof(conf), "%s/%s/theme.conf", dirs[i], id);
		if ((f = fopen(conf, "r"))) snprintf(t.dir, sizeof(t.dir), "%.200s/%.31s", dirs[i], id);
	}
	if (f) {
		char l[400], sec[16] = "";
		int n = 0;
		found = 1;
		snprintf(t.id, sizeof(t.id), "%s", id);
		snprintf(t.name, sizeof(t.name), "%s", id);
		while (fgets(l, sizeof(l), f)) {
			n++;
			char *s = trim(l);
			if (!*s || *s == '#' || *s == ';') continue;
			if (*s == '[') {
				char *e = strchr(s, ']');
				if (e) { *e = 0; snprintf(sec, sizeof(sec), "%s", trim(s + 1)); }
				continue;
			}
			char *eq = strchr(s, '=');
			if (!eq) continue;
			*eq = 0;
			char *v = trim(eq + 1), *hash = strstr(v, " #");   /* "accent = #4fa3ff  # blue" */
			if (hash) { *hash = 0; v = trim(v); }
			set(&t, sec, trim(s), v, n);
		}
		fclose(f);
	}
	for (int b = 0; b < 2; b++)
		if (!t.font[b][0]) { const char *p = path_font(b); if (p) snprintf(t.font[b], sizeof(t.font[b]), "%s", p); }
	derive(&t);
	theme = t;
	return found ? 0 : -1;
}

static int by_id(const void *a, const void *b) { return strcmp(a, b); }

int theme_list(char ids[][32], int max)
{
	char dirs[2][320];
	int nd = theme_dirs(dirs), n = 0;
	for (int i = 0; i < nd; i++) {
		DIR *d = opendir(dirs[i]);
		if (!d) continue;
		struct dirent *e;
		while ((e = readdir(d)) && n < max) {
			char conf[1024];
			if (!valid_id(e->d_name)) continue;
			snprintf(conf, sizeof(conf), "%s/%.63s/theme.conf", dirs[i], e->d_name);
			if (access(conf, R_OK)) continue;
			int dup = 0;
			for (int k = 0; k < n; k++) dup |= !strcmp(ids[k], e->d_name);
			if (!dup) snprintf(ids[n++], 32, "%.31s", e->d_name);
		}
		closedir(d);
	}
	qsort(ids, n, 32, by_id);
	return n;
}

/* ---- settings ---- */

static void settings_file(char *p, size_t n)
{
	const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
	if (x && *x) snprintf(p, n, "%s/emuzero/settings", x);
	else snprintf(p, n, "%s/.config/emuzero/settings", h && *h ? h : ".");
}

void settings_load(struct settings *s)
{
	char p[400], l[128];
	memset(s, 0, sizeof(*s));
	snprintf(s->theme, sizeof(s->theme), "minimal");
	settings_file(p, sizeof(p));
	FILE *f = fopen(p, "r");
	if (!f) return;
	while (fgets(l, sizeof(l), f)) {
		char *eq = strchr(l, '=');
		if (!eq) continue;
		*eq = 0;
		char *k = trim(l), *v = trim(eq + 1);
		if (!strcmp(k, "theme") && valid_id(v)) snprintf(s->theme, sizeof(s->theme), "%s", v);
		else if (!strcmp(k, "layout")) snprintf(s->layout, sizeof(s->layout), "%s", v);
	}
	fclose(f);
}

static int mkdirs(char *p)   /* every folder above the file p */
{
	for (char *s = p + 1; (s = strchr(s, '/')); s++) {
		*s = 0;
		int r = mkdir(p, 0755);
		*s = '/';
		if (r && errno != EEXIST) return -1;
	}
	return 0;
}

int settings_save(const struct settings *s)
{
	char p[400], tmp[420];
	settings_file(p, sizeof(p));
	if (mkdirs(p)) return -1;
	snprintf(tmp, sizeof(tmp), "%s.new", p);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f, "theme=%s\nlayout=%s\n", s->theme, s->layout);
	if (fclose(f)) return -1;
	return rename(tmp, p);
}
