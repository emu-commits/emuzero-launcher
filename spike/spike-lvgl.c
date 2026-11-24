// SPDX-License-Identifier: MIT
// Renderer spike A: the same screen as spike-hand, drawn with LVGL 9.2 (fbdev display driver,
// evdev keypad, FreeType text with the same DejaVu font, libpng icons scaled by LVGL).
//
//   spike-lvgl [--mode default|tuned] [--kbd /dev/input/eventN] [--idle SECS] [--bench N]
//
// default: the usual LVGL loop (lv_timer_handler + sleep; display and input timers every 33 ms).
// tuned:   input in event mode (poll() on the keyboard), the display refresh timer paused until
//          something is invalidated, so the process sleeps until a key or the clock changes.
#include "common.h"
#include <fcntl.h>
#include <poll.h>
#include "lvgl/lvgl.h"

#define FONT "/usr/share/fonts/dejavu/DejaVuSans.ttf"
#define FONTB "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"
static uint32_t tick(void) { return (uint32_t)now_ms(); }

static lv_obj_t *lbl_time, *lbl_batt, *lbl_name, *lbl_idx, *slot[3]; static int sel;
static lv_font_t *f11, *f13, *f16, *f28, *f20b;
static const uint32_t pal[] = { 0x3d7be0, 0xe0703d, 0x3db07a, 0xb05ad0, 0xd0b03d, 0x3db0c0 };

/* slot k (0 left, 1 centre, 2 right) shows app i: its PNG scaled, or a coloured tile with its initial */
static void fill_slot(int k, int i)
{
	int size = k == 1 ? 64 : 40, cx = k == 0 ? 70 : k == 1 ? 160 : 250;
	lv_obj_clean(slot[k]);
	lv_obj_set_size(slot[k], size, size);
	lv_obj_set_pos(slot[k], cx - size / 2, 64 - size / 2);
	lv_obj_set_style_opa(slot[k], k == 1 ? LV_OPA_COVER : 110, 0);
	if (apps[i].icon[0]) {
		char src[300]; snprintf(src, sizeof(src), "A:%s", apps[i].icon);
		lv_obj_set_style_bg_opa(slot[k], LV_OPA_TRANSP, 0);
		lv_obj_t *img = lv_image_create(slot[k]);
		lv_image_set_src(img, src);
		lv_image_set_scale(img, size * 256 / 100);
		lv_obj_center(img);
	} else {
		unsigned h = 0; for (const char *s = apps[i].name; *s; s++) h = h * 31 + *s;
		char l[2] = { apps[i].name[0], 0 };
		lv_obj_set_style_bg_opa(slot[k], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(slot[k], lv_color_hex(pal[h % 6]), 0);
		lv_obj_set_style_radius(slot[k], size / 5, 0);
		lv_obj_t *t = lv_label_create(slot[k]);
		lv_label_set_text(t, l);
		lv_obj_set_style_text_font(t, k == 1 ? f28 : f20b, 0);
		lv_obj_set_style_text_color(t, lv_color_white(), 0);
		lv_obj_center(t);
	}
}
static void show(void)
{
	char b[16];
	if (!napps) return;
	fill_slot(0, (sel + napps - 1) % napps); fill_slot(1, sel); fill_slot(2, (sel + 1) % napps);
	lv_label_set_text(lbl_name, apps[sel].name);
	snprintf(b, sizeof(b), "%d / %d", sel + 1, napps); lv_label_set_text(lbl_idx, b);
}
static void status(void) { char t[16], b[16]; clock_text(t, sizeof(t)); lv_label_set_text(lbl_time, t); snprintf(b, sizeof(b), "%d%%", battery_pct()); lv_label_set_text(lbl_batt, b); }
static void clock_cb(lv_timer_t *t) { status(); lv_timer_set_period(t, ms_to_next_minute()); }

static int quit;
static void key_cb(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);
	if (k == LV_KEY_RIGHT || k == LV_KEY_DOWN) { sel = (sel + 1) % napps; show(); }
	if (k == LV_KEY_LEFT || k == LV_KEY_UP) { sel = (sel + napps - 1) % napps; show(); }
	if (k == LV_KEY_ESC) quit = 1;
}
/* tuned mode: run the refresh timer only while something is invalid */
static lv_timer_t *refr;
static void on_invalidate(lv_event_t *e) { (void)e; lv_timer_resume(refr); }
static void on_refr_ready(lv_event_t *e) { (void)e; lv_timer_pause(refr); }

static lv_obj_t *label(lv_obj_t *p, lv_font_t *f, uint32_t c) { lv_obj_t *l = lv_label_create(p); lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, lv_color_hex(c), 0); return l; }

int main(int argc, char **argv)
{
	const char *kbd = "/dev/input/by-path/platform-3f804000.i2c-event"; int idle_s = 0, bench = 0, tuned = 0;
	for (int i = 1; i < argc - 1; i++) {
		if (!strcmp(argv[i], "--kbd")) kbd = argv[++i];
		else if (!strcmp(argv[i], "--idle")) idle_s = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--bench")) bench = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--mode")) tuned = !strcmp(argv[++i], "tuned");
	}
	double t_main = since_exec_ms(), t0 = now_ms();
	lv_init();
	lv_tick_set_cb(tick);
	lv_display_t *disp = lv_linux_fbdev_create();
	lv_linux_fbdev_set_file(disp, "/dev/fb0");
	lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
	f11 = lv_freetype_font_create(FONT, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 11, LV_FREETYPE_FONT_STYLE_NORMAL);
	f13 = lv_freetype_font_create(FONT, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 13, LV_FREETYPE_FONT_STYLE_NORMAL);
	f16 = lv_freetype_font_create(FONT, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16, LV_FREETYPE_FONT_STYLE_NORMAL);
	f28 = lv_freetype_font_create(FONTB, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 28, LV_FREETYPE_FONT_STYLE_NORMAL);
	f20b = lv_freetype_font_create(FONTB, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16, LV_FREETYPE_FONT_STYLE_NORMAL);
	if (!f11 || !f13 || !f16 || !f28 || !f20b) { fprintf(stderr, "font\n"); return 1; }
	double t_init = now_ms();
	scan_apps();
	double t_scan = now_ms();

	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	lbl_time = label(scr, f13, 0xe8e8e8); lv_obj_set_pos(lbl_time, 6, 1);
	lbl_batt = label(scr, f13, 0xe8e8e8); lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -6, 1);
	lv_obj_t *line = lv_obj_create(scr); lv_obj_set_size(line, 320, 1); lv_obj_set_pos(line, 0, 18);
	lv_obj_set_style_bg_color(line, lv_color_hex(0x2a3038), 0); lv_obj_set_style_border_width(line, 0, 0); lv_obj_set_style_radius(line, 0, 0);
	lv_obj_t *card = lv_obj_create(scr); lv_obj_set_size(card, 80, 80); lv_obj_set_pos(card, 120, 24);
	lv_obj_set_style_bg_color(card, lv_color_hex(0x2a3442), 0); lv_obj_set_style_border_width(card, 0, 0); lv_obj_set_style_radius(card, 14, 0);
	for (int k = 0; k < 3; k++) {
		slot[k] = lv_obj_create(scr);
		lv_obj_set_style_border_width(slot[k], 0, 0); lv_obj_set_style_pad_all(slot[k], 0, 0);
		lv_obj_remove_flag(slot[k], LV_OBJ_FLAG_SCROLLABLE);
	}
	lbl_name = label(scr, f16, 0xffffff); lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 113);
	lbl_idx = label(scr, f11, 0x8a96a6); lv_obj_align(lbl_idx, LV_ALIGN_TOP_MID, 0, 147);
	status(); show();
	lv_refr_now(disp);
	double t_frame = now_ms();
	printf("lvgl first frame: %.1f ms since exec (loader+libs %.1f, init+fonts %.1f, scan %.1f, build+draw %.1f), %d apps\n",
	       since_exec_ms(), t_main, t_init - t0, t_scan - t_init, t_frame - t_scan, napps);
	fflush(stdout);

	if (bench) {
		double b0 = now_ms();
		for (int i = 0; i < bench; i++) { sel = (sel + 1) % napps; show(); lv_refr_now(disp); }
		printf("lvgl redraw: %.2f ms a frame (%d frames)\n", (now_ms() - b0) / bench, bench);
		sel = 0; show(); lv_refr_now(disp);
	}

	lv_indev_t *kb = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, kbd);
	lv_group_t *g = lv_group_create(); lv_obj_t *focus = lv_obj_create(scr); lv_obj_add_flag(focus, LV_OBJ_FLAG_HIDDEN);
	lv_group_add_obj(g, focus); lv_obj_add_event_cb(focus, key_cb, LV_EVENT_KEY, NULL);
	if (kb) lv_indev_set_group(kb, g);
	lv_timer_create(clock_cb, ms_to_next_minute(), NULL);
	int kfd = -1;
	if (tuned) {
		if (kb) lv_indev_set_mode(kb, LV_INDEV_MODE_EVENT);
		refr = lv_display_get_refr_timer(disp);
		lv_display_add_event_cb(disp, on_invalidate, LV_EVENT_INVALIDATE_AREA, NULL);
		lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
		lv_timer_pause(refr);
		kfd = open(kbd, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}

	idle_begin();
	double end = idle_s ? now_ms() + idle_s * 1000.0 : 0;
	while (!quit) {
		uint32_t d = lv_timer_handler();
		if (end) { double left = end - now_ms(); if (left <= 0) break; if (d == LV_NO_TIMER_READY || d > left) d = (uint32_t)left + 1; }
		if (!tuned) { usleep((d == LV_NO_TIMER_READY ? 1000 : d) * 1000); continue; }
		struct pollfd p = { kfd, POLLIN, 0 };
		if (poll(&p, kfd >= 0, d == LV_NO_TIMER_READY ? -1 : (int)d) > 0 && kb) lv_indev_read(kb);
	}
	idle_end(tuned ? "lvgl-tuned" : "lvgl-default");
	return 0;
}
