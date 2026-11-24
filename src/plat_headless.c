// SPDX-License-Identifier: MIT
// Headless build: draws into memory, plays a key script, writes the final screen to a PNG.
// Used for checks on a PC without a window system:
//   emuzero-launcher --keys right,right,enter --shot home.png
#include "platform.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint32_t keys[256];
static int nkeys, next, pressed, quit;

const char *plat_name(void) { return "headless"; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
	(void)a; (void)px;     /* direct mode: LVGL draws straight into fb */
	lv_display_flush_ready(d);
}

lv_display_t *plat_display(const struct plat_opts *o)
{
	(void)o;
	lv_display_t *d = lv_display_create(SCREEN_W, SCREEN_H);
	lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
	lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
	lv_display_set_flush_cb(d, flush);
	return d;
}

static uint32_t key_code(const char *s)
{
	static const struct { const char *name; uint32_t key; } names[] = {
		{ "up", LV_KEY_UP }, { "down", LV_KEY_DOWN }, { "left", LV_KEY_LEFT }, { "right", LV_KEY_RIGHT },
		{ "enter", LV_KEY_ENTER }, { "esc", LV_KEY_ESC }, { "del", LV_KEY_DEL }, { "backspace", LV_KEY_BACKSPACE },
		{ "tab", LV_KEY_NEXT }, { "home", LV_KEY_HOME }, { "end", LV_KEY_END }, { "space", ' ' }, { "comma", ',' },
	};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(s, names[i].name)) return names[i].key;
	return s[0] && !s[1] ? (uint32_t)(unsigned char)s[0] : 0;
}

/* each script key is one press and one release */
static void read_keys(lv_indev_t *in, lv_indev_data_t *data)
{
	(void)in;
	if (next >= nkeys) { data->state = LV_INDEV_STATE_RELEASED; return; }
	data->key = keys[next];
	data->state = pressed ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
	if (pressed) next++;
	pressed = !pressed;
}

lv_indev_t *plat_keypad(const struct plat_opts *o)
{
	if (o->keys) {
		char buf[1024], *save = NULL;
		snprintf(buf, sizeof(buf), "%s", o->keys);
		for (char *t = strtok_r(buf, ",", &save); t && nkeys < 256; t = strtok_r(NULL, ",", &save)) {
			uint32_t k = key_code(t);
			if (!k) { fprintf(stderr, "emuzero-launcher: unknown key '%s'\n", t); exit(2); }
			keys[nkeys++] = k;
		}
	}
	lv_indev_t *in = lv_indev_create();
	lv_indev_set_type(in, LV_INDEV_TYPE_KEYPAD);
	lv_indev_set_read_cb(in, read_keys);
	return in;
}

void plat_quit(void) { quit = 1; }

static int write_png(const char *path)
{
	static uint8_t rgb[SCREEN_W * SCREEN_H * 3];
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
		uint16_t p = fb[i];
		uint8_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
		rgb[i * 3] = (r << 3) | (r >> 2);
		rgb[i * 3 + 1] = (g << 2) | (g >> 4);
		rgb[i * 3 + 2] = (b << 3) | (b >> 2);
	}
	png_image im = { .version = PNG_IMAGE_VERSION, .width = SCREEN_W, .height = SCREEN_H, .format = PNG_FORMAT_RGB };
	if (!png_image_write_to_file(&im, path, 0, rgb, 0, NULL)) { fprintf(stderr, "emuzero-launcher: %s: %s\n", path, im.message); return 1; }
	return 0;
}

int plat_run(const struct plat_opts *o)
{
	/* play the script (one indev read per 33 ms), then let the last change draw */
	while (!quit && (next < nkeys || pressed)) { lv_timer_handler(); usleep(2000); }
	lv_timer_handler();
	lv_refr_now(NULL);
	return o->shot ? write_png(o->shot) : 0;
}

uint32_t plat_exec_end;

int plat_exec(const struct plat_app *a)
{
	printf("launch: %s%s\n", a->exec, a->terminal ? " (terminal)" : "");
	fflush(stdout);
	plat_exec_end = lv_tick_get();
	return 0;
}

void plat_sleep(void) {}
