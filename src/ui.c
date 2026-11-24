// SPDX-License-Identifier: MIT
// The home screen: status bar on top, the installed apps below in the chosen layout.
//
// Keys: arrows (or F/X/Z/C, which carry the arrow legends on the Cardputer keyboard) move;
// Enter starts the app; O opens Options; I or Del opens App info; Esc quits (PC builds only).
// After the screen-off time from Options (/data/etc/nz/screen-off, default 120 s, 0 = never)
// without a key the screen goes dark; the next key only wakes it.
#include "ui.h"
#include "appinfo.h"
#include "apps.h"
#include "layout.h"
#include "paths.h"
#include "platform.h"
#include "statusbar.h"
#include "sys.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/* ---- home ---- */

static const struct layout *layout;
static lv_obj_t *scr, *area, *toast;
static lv_timer_t *idle, *toast_timer;
static int sel, idle_s;

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

/* The screen-off time from Options, read again after every app (Options may change it).
 * nz-admin writes the file as root with mode 600 when it runs under sudo, so when the file is
 * there but unreadable, ask nz-admin itself (about 0.13 s on the device, so it runs after the
 * screen is drawn). */
static int screen_off_setting(void)
{
	char p[512], out[1024];
	const char *f = "/data/etc/nz/screen-off";
	long v = sys_read_long(f, -1);
	if (v >= 0) return (int)v;
	if (access(path_join(p, sizeof(p), f), F_OK)) return 120;   /* never set: the default */
	char *argv[] = { "sudo", "-n", "/usr/sbin/nz-admin", "status", NULL };
	if (sys_run(argv, out, sizeof(out)) == 0) {
		char *k = strstr(out, "screen-off=");
		if (k) return atoi(k + 11);
	}
	return 120;
}

static void read_settings(void)
{
	idle_s = screen_off_setting();
	if (idle_s > 0) { lv_timer_set_period(idle, (uint32_t)idle_s * 1000); lv_timer_reset(idle); lv_timer_resume(idle); }
	else lv_timer_pause(idle);
	tzset();   /* Options may have changed the time zone */
}

static void first_settings(lv_timer_t *t) { (void)t; read_settings(); }

static void idle_cb(lv_timer_t *t)
{
	plat_sleep();
	statusbar_update();
	lv_timer_reset(t);
}

static void toast_hide(lv_timer_t *t) { (void)t; lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN); toast_timer = NULL; }

/* a one-line message over the bottom of the screen for a few seconds */
static void say(const char *msg)
{
	lv_label_set_text(toast, msg);
	lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
	if (toast_timer) lv_timer_delete(toast_timer);
	toast_timer = lv_timer_create(toast_hide, 4000, NULL);
	lv_timer_set_repeat_count(toast_timer, 1);
}

/* the apps may have changed (the Store installs and removes); keep the selection on the same app */
static void reload(const char *keep_id)
{
	apps_scan();
	int i = keep_id ? apps_find(keep_id) : -1;
	sel = i >= 0 ? i : sel < napps ? sel : napps ? napps - 1 : 0;
	show_layout();
	statusbar_update();
}

static void launch(const struct app *a)
{
	char id[sizeof(a->id)], name[sizeof(a->name)], keep[sizeof(a->id)], msg[160];
	snprintf(id, sizeof(id), "%s", a->id);
	snprintf(keep, sizeof(keep), "%s", napps ? apps[sel].id : "");   /* O starts Options without moving */
	snprintf(name, sizeof(name), "%s", a->name);
	struct plat_app run = { a->exec, a->path, a->terminal, idle_s };
	int rc = plat_exec(&run);
	reload(keep);
	lv_refr_now(NULL);
	fprintf(stderr, "emuzero-launcher: %s ended (%d), home again after %u ms\n", id, rc, (unsigned)(lv_tick_get() - plat_exec_end));
	read_settings();
	msg[0] = 0;
	if (rc == 127 || rc < 0) snprintf(msg, sizeof(msg), "%s could not be started", name);
	else if (rc > 128) snprintf(msg, sizeof(msg), "%s was stopped (signal %d)", name, rc - 128);
	else if (rc > 0) snprintf(msg, sizeof(msg), "%s ended with error %d", name, rc);
	if (msg[0]) say(msg);
	if (idle_s > 0) lv_timer_reset(idle);
}

static void info_closed(int changed)
{
	if (changed) reload(napps ? apps[sel].id : NULL);
}

static void key_cb(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);
	if (idle_s > 0) lv_timer_reset(idle);
	statusbar_update();
	if (appinfo_active()) { appinfo_key(k); return; }
	switch (k) {   /* the keys with arrow legends work as arrows without Fn */
	case 'f': k = LV_KEY_UP; break;
	case 'x': k = LV_KEY_DOWN; break;
	case 'z': k = LV_KEY_LEFT; break;
	case 'c': k = LV_KEY_RIGHT; break;
	}
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
	case 'o': {   /* Options, NimbleZero's settings */
		int i = apps_find("nz-options");
		if (i >= 0) launch(&apps[i]);
		break;
	}
	case 'i': case LV_KEY_DEL: case LV_KEY_BACKSPACE:
		if (napps) appinfo_open(scr, &apps[sel], info_closed);
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
	scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, lv_color_hex(theme.bg), 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	statusbar_create(scr);

	area = lv_obj_create(scr);
	lv_obj_remove_style_all(area);
	lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(area, 0, BAR_H + 1);
	lv_obj_set_size(area, SCREEN_W, SCREEN_H - BAR_H - 1);

	toast = ui_label(scr, ui_font(12, 1), theme.fg, "");
	lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(toast, lv_color_hex(theme.sel_bg), 0);
	lv_obj_set_style_border_width(toast, 1, 0);
	lv_obj_set_style_border_color(toast, lv_color_hex(theme.accent), 0);
	lv_obj_set_style_radius(toast, 6, 0);
	lv_obj_set_style_pad_hor(toast, 8, 0);
	lv_obj_set_style_pad_ver(toast, 3, 0);
	lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

	idle = lv_timer_create(idle_cb, 120000, NULL);
	idle_s = 120;
	lv_timer_t *t = lv_timer_create(first_settings, 1, NULL);   /* after the first frame */
	lv_timer_set_repeat_count(t, 1);

	apps_scan();
	layout = layout_find(theme.layout);
	if (!layout) layout = layouts[0];
	show_layout();
	lv_obj_move_foreground(toast);

	/* keys go to the screen itself (a hidden object would never take the group's focus) */
	lv_obj_add_event_cb(scr, key_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(g, scr);
}
