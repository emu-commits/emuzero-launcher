// SPDX-License-Identifier: MIT
// Headless build: draws into memory and plays a script, for checks without a screen.
//   emuzero-notes --keys "ctrl+n,H,i,enter" --shot out.png
//   emuzero-notes --script test.keys
// Script lines (# starts a comment):
//   type TEXT          type each character of TEXT ("\n" types Enter, "\t" Tab)
//   key SPEC [N]       press a key (see keyspec.c), N times
//   wait MS            let MS milliseconds of (virtual) time pass: autosave, toasts, blinking
//   shot FILE.png      write the screen now
//   sh COMMAND         run a shell command (to change a file under the app, in tests)
//   quit               stop here
// Time is virtual: it only moves on, 20 ms per key and by `wait`, so runs are repeatable.
#include "nplat.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint32_t vt = 1000;
static void (*key_fn)(struct nkey);

const char *nplat_name(void) { return "headless"; }
uint32_t nplat_tick(void) { return vt; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
	(void)a; (void)px;
	lv_display_flush_ready(d);
}

lv_display_t *nplat_display(const struct nplat_opts *o)
{
	(void)o;
	lv_display_t *d = lv_display_create(SCREEN_W, SCREEN_H);
	lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
	lv_display_set_flush_cb(d, flush);
	return d;
}

void nplat_keys(const struct nplat_opts *o, void (*key_cb)(struct nkey), void (*mods_cb)(unsigned))
{
	(void)o; (void)mods_cb;
	key_fn = key_cb;
}

static int write_png(const char *path)
{
	static uint8_t rgb[SCREEN_W * SCREEN_H * 3];
	lv_refr_now(NULL);
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
		uint16_t p = fb[i];
		uint8_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
		rgb[i * 3] = (r << 3) | (r >> 2);
		rgb[i * 3 + 1] = (g << 2) | (g >> 4);
		rgb[i * 3 + 2] = (b << 3) | (b >> 2);
	}
	png_image im = { .version = PNG_IMAGE_VERSION, .width = SCREEN_W, .height = SCREEN_H, .format = PNG_FORMAT_RGB };
	if (!png_image_write_to_file(&im, path, 0, rgb, 0, NULL)) { fprintf(stderr, "emuzero-notes: %s: %s\n", path, im.message); return 1; }
	return 0;
}

static void pass(uint32_t ms)
{
	uint32_t end = vt + ms;
	while (vt < end) {
		uint32_t step = end - vt < 10 ? end - vt : 10;
		vt += step;
		lv_timer_handler();
	}
}

static int (*quit_fn)(void);

static int press(const char *spec)
{
	struct nkey k;
	if (!nplat_parse_key(spec, &k)) { fprintf(stderr, "emuzero-notes: unknown key '%s'\n", spec); return -1; }
	key_fn(k);
	pass(20);
	return 0;
}

static int type_text(const char *s)
{
	while (*s) {
		struct nkey k = { 0, 0 };
		const unsigned char *u = (const unsigned char *)s;
		int n = 1;
		if (s[0] == '\\' && s[1] == 'n') { k.k = NK_ENTER; n = 2; }
		else if (s[0] == '\\' && s[1] == 't') { k.k = NK_TAB; n = 2; }
		else if (u[0] < 0x80) k.k = u[0];
		else if ((u[0] & 0xe0) == 0xc0 && u[1]) { k.k = (uint32_t)(u[0] & 0x1f) << 6 | (u[1] & 0x3f); n = 2; }
		else if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2]) { k.k = (uint32_t)(u[0] & 0x0f) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f); n = 3; }
		else if ((u[0] & 0xf8) == 0xf0 && u[1] && u[2] && u[3]) { k.k = (uint32_t)(u[0] & 0x07) << 18 | (uint32_t)(u[1] & 0x3f) << 12 | (uint32_t)(u[2] & 0x3f) << 6 | (u[3] & 0x3f); n = 4; }
		if (k.k >= 'A' && k.k <= 'Z') k.mods = M_SHIFT;
		s += n;
		key_fn(k);
		pass(20);
		if (quit_fn()) return 1;
	}
	return 0;
}

static int run_script(FILE *f)
{
	char line[8192];
	int ln = 0;
	while (fgets(line, sizeof(line), f)) {
		ln++;
		line[strcspn(line, "\n")] = 0;
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (!*s || *s == '#') continue;
		char *arg = strchr(s, ' ');
		if (arg) *arg++ = 0; else arg = s + strlen(s);
		if (!strcmp(s, "type")) { if (type_text(arg)) return 0; }
		else if (!strcmp(s, "key")) {
			char spec[64]; int n = 1;
			if (sscanf(arg, "%63s %d", spec, &n) < 1) { fprintf(stderr, "script line %d: key what?\n", ln); return 2; }
			for (int i = 0; i < n; i++) { if (press(spec)) return 2; if (quit_fn()) return 0; }
		}
		else if (!strcmp(s, "wait")) pass((uint32_t)atoi(arg));
		else if (!strcmp(s, "shot")) { if (write_png(arg)) return 1; }
		else if (!strcmp(s, "sh")) { if (system(arg)) {} }
		else if (!strcmp(s, "quit")) return 0;
		else { fprintf(stderr, "script line %d: unknown '%s'\n", ln, s); return 2; }
		if (quit_fn()) return 0;
	}
	return 0;
}

int nplat_run(const struct nplat_opts *o, int (*should_quit)(void))
{
	quit_fn = should_quit;
	lv_refr_now(NULL);
	int r = 0;
	if (o->keys) {
		char *buf = strdup(o->keys), *save = NULL;
		for (char *t = strtok_r(buf, ",", &save); t && !r && !should_quit(); t = strtok_r(NULL, ",", &save))
			if (press(t)) r = 2;
		free(buf);
	}
	if (!r && o->script) {
		FILE *f = !strcmp(o->script, "-") ? stdin : fopen(o->script, "r");
		if (!f) { perror(o->script); return 2; }
		r = run_script(f);
		if (f != stdin) fclose(f);
	}
	pass(20);
	if (!r && o->shot) r = write_png(o->shot);
	return r;
}
