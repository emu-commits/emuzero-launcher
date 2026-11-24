// SPDX-License-Identifier: MIT
// Placeholder screen for the build setup (Phase 2 row 2). The home screen replaces it in row 3.
#include "ui.h"
#include "paths.h"
#include "platform.h"
#include <stdio.h>
#include <time.h>

#ifndef EMUZERO_VERSION
#define EMUZERO_VERSION "dev"
#endif

static lv_font_t *f13, *f16b;
static lv_obj_t *lbl_time, *lbl_key;

int ui_fonts(void)
{
	const char *reg = path_font(0), *bold = path_font(1);
	if (!reg || !bold) { fprintf(stderr, "emuzero-launcher: DejaVu fonts not found\n"); return -1; }
	lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
	f13 = lv_freetype_font_create(reg, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 13, LV_FREETYPE_FONT_STYLE_NORMAL);
	f16b = lv_freetype_font_create(bold, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16, LV_FREETYPE_FONT_STYLE_NORMAL);
	return f13 && f16b ? 0 : -1;
}

static lv_obj_t *label(lv_obj_t *p, lv_font_t *f, uint32_t c, const char *text)
{
	lv_obj_t *l = lv_label_create(p);
	lv_obj_set_style_text_font(l, f, 0);
	lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
	lv_label_set_text(l, text);
	return l;
}

static int ms_to_next_minute(void)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return (int)(60000 - (t.tv_sec % 60) * 1000 - t.tv_nsec / 1000000) + 5;
}

static void clock_cb(lv_timer_t *t)
{
	char buf[16]; time_t now = time(NULL);
	strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
	lv_label_set_text(lbl_time, buf);
	if (t) lv_timer_set_period(t, ms_to_next_minute());
}

static void key_cb(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);
	char buf[32];
	switch (k) {
	case LV_KEY_UP: lv_label_set_text(lbl_key, "Last key: Up"); break;
	case LV_KEY_DOWN: lv_label_set_text(lbl_key, "Last key: Down"); break;
	case LV_KEY_LEFT: lv_label_set_text(lbl_key, "Last key: Left"); break;
	case LV_KEY_RIGHT: lv_label_set_text(lbl_key, "Last key: Right"); break;
	case LV_KEY_ENTER: lv_label_set_text(lbl_key, "Last key: Enter"); break;
	case LV_KEY_ESC:
		lv_label_set_text(lbl_key, "Last key: Esc");
#if EMUZERO_PLATFORM != EMUZERO_FBDEV
		plat_quit();   /* the device launcher never quits on a key */
#endif
		break;
	default:
		snprintf(buf, sizeof(buf), "Last key: %c", k >= 32 && k < 127 ? (int)k : '?');
		lv_label_set_text(lbl_key, buf);
	}
}

void ui_create(lv_group_t *g)
{
	char buf[96];
	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	lbl_time = label(scr, f13, 0xe8e8e8, "");
	lv_obj_set_pos(lbl_time, 6, 1);
	clock_cb(NULL);
	lv_timer_create(clock_cb, ms_to_next_minute(), NULL);

	lv_obj_t *line = lv_obj_create(scr);
	lv_obj_set_size(line, SCREEN_W, 1);
	lv_obj_set_pos(line, 0, 18);
	lv_obj_set_style_bg_color(line, lv_color_hex(0x2a3038), 0);
	lv_obj_set_style_border_width(line, 0, 0);
	lv_obj_set_style_radius(line, 0, 0);

	lv_obj_align(label(scr, f16b, 0xffffff, "emuZero"), LV_ALIGN_TOP_MID, 0, 50);
	snprintf(buf, sizeof(buf), "launcher %s (%s)", EMUZERO_VERSION, plat_name());
	lv_obj_align(label(scr, f13, 0x8a96a6, buf), LV_ALIGN_TOP_MID, 0, 76);
	lbl_key = label(scr, f13, 0x8a96a6, "Press a key");
	lv_obj_align(lbl_key, LV_ALIGN_TOP_MID, 0, 120);

	/* keys go to the screen itself (a hidden object would never take the group's focus) */
	lv_obj_add_event_cb(scr, key_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(g, scr);
}
