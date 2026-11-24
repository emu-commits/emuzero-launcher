// SPDX-License-Identifier: MIT
// The home screen: status bar on top, the installed apps below in the chosen layout.
#include "ui.h"
#include "apps.h"
#include "layout.h"
#include "paths.h"
#include "platform.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BAR_H 18   /* status bar height; a 1-pixel line under it */

struct theme theme = {
	.bg = 0x101418, .fg = 0xf0f0f0, .dim = 0x8a96a6, .line = 0x2a3038,
	.accent = 0x4fa3ff, .sel_bg = 0x1e2a38, .layout = "carousel",
};

const struct layout *const layouts[] = { &layout_carousel, &layout_grid, NULL };

const struct layout *layout_find(const char *name)
{
	for (int i = 0; layouts[i]; i++)
		if (!strcmp(layouts[i]->name, name)) return layouts[i];
	return NULL;
}

/* ---- fonts ---- */

static const char *font_file[2];
static struct { int px, bold; lv_font_t *f; } fonts[16];
static int nfonts;

int ui_fonts(void)
{
	font_file[0] = path_font(0);
	font_file[1] = path_font(1);
	if (!font_file[0] || !font_file[1]) { fprintf(stderr, "emuzero-launcher: DejaVu fonts not found\n"); return -1; }
	lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
	return ui_font(13, 0) && ui_font(16, 1) ? 0 : -1;
}

lv_font_t *ui_font(int px, int bold)
{
	bold = !!bold;
	for (int i = 0; i < nfonts; i++)
		if (fonts[i].px == px && fonts[i].bold == bold) return fonts[i].f;
	lv_font_t *f = lv_freetype_font_create(font_file[bold], LV_FREETYPE_FONT_RENDER_MODE_BITMAP, px,
	                                       LV_FREETYPE_FONT_STYLE_NORMAL);
	if (!f) return nfonts ? fonts[0].f : NULL;
	if (nfonts < (int)(sizeof(fonts) / sizeof(fonts[0]))) fonts[nfonts++] = (typeof(fonts[0])){ px, bold, f };
	return f;
}

lv_obj_t *ui_label(lv_obj_t *p, lv_font_t *f, uint32_t c, const char *text)
{
	lv_obj_t *l = lv_label_create(p);
	lv_obj_set_style_text_font(l, f, 0);
	lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
	lv_label_set_text(l, text);
	return l;
}

/* ---- status bar ---- */

static lv_obj_t *lbl_time;

static int ms_to_next_minute(void)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return (int)(60000 - (t.tv_sec % 60) * 1000 - t.tv_nsec / 1000000) + 5;
}

static void clock_cb(lv_timer_t *t)
{
	char buf[16];
	time_t now = time(NULL);
	strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
	lv_label_set_text(lbl_time, buf);
	if (t) lv_timer_set_period(t, ms_to_next_minute());
}

static void status_bar(lv_obj_t *scr)
{
	lbl_time = ui_label(scr, ui_font(13, 0), theme.fg, "");
	lv_obj_set_pos(lbl_time, 6, 1);
	clock_cb(NULL);
	lv_timer_create(clock_cb, ms_to_next_minute(), NULL);

	lv_obj_t *line = lv_obj_create(scr);
	lv_obj_remove_style_all(line);
	lv_obj_set_size(line, SCREEN_W, 1);
	lv_obj_set_pos(line, 0, BAR_H);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(line, lv_color_hex(theme.line), 0);
}

/* ---- home ---- */

static const struct layout *layout;
static lv_obj_t *area;
static int sel;

int ui_set_layout(const char *name)
{
	const struct layout *l = layout_find(name);
	if (!l) return -1;
	theme.layout = l->name;
	return 0;
}

static void show_layout(void)
{
	lv_obj_clean(area);
	layout->create(area);
	layout->show(sel);
}

static void launch(const struct app *a)
{
	/* starting apps is row 5 (through nz-console / the session); for now say what would run */
	fprintf(stderr, "emuzero-launcher: launch %s: %s%s\n", a->id, a->exec, a->terminal ? " (terminal)" : "");
}

static void key_cb(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);
	switch (k) {
	case LV_KEY_UP: case LV_KEY_DOWN: case LV_KEY_LEFT: case LV_KEY_RIGHT:
		if (napps) {
			int n = layout->move(k, sel, napps);
			if (n != sel) { sel = n; layout->show(sel); }
		}
		break;
	case LV_KEY_ENTER:
		if (napps) launch(&apps[sel]);
		break;
	case LV_KEY_ESC:
#if EMUZERO_PLATFORM != EMUZERO_FBDEV
		plat_quit();   /* the device launcher never quits on a key */
#endif
		break;
	}
}

void ui_create(lv_group_t *g)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_hex(theme.bg), 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	status_bar(scr);

	area = lv_obj_create(scr);
	lv_obj_remove_style_all(area);
	lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(area, 0, BAR_H + 1);
	lv_obj_set_size(area, SCREEN_W, SCREEN_H - BAR_H - 1);

	apps_scan();
	layout = layout_find(theme.layout);
	if (!layout) layout = layouts[0];
	show_layout();

	/* keys go to the screen itself (a hidden object would never take the group's focus) */
	lv_obj_add_event_cb(scr, key_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(g, scr);
}
