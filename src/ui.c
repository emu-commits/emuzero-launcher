// SPDX-License-Identifier: MIT
// The home screen: the status bar, the installed apps in the chosen layout, and the theme's
// chrome around them (wallpaper, an optional window frame).
//
// Keys: arrows (or F/X/Z/C, which carry the arrow legends on the Cardputer keyboard) move;
// Enter starts the app; O opens Options; T opens Theme; I or Del opens App info; Esc quits
// (PC builds only).
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
#include "themepage.h"
#include "icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

const struct layout *const layouts[] = { &layout_carousel, &layout_grid, NULL };

const struct layout *layout_find(const char *name)
{
	for (int i = 0; layouts[i]; i++)
		if (!strcmp(layouts[i]->name, name)) return layouts[i];
	return NULL;
}

/* ---- fonts ---- */

static struct settings cfg;
static const char *force_layout, *force_theme;   /* --layout, --theme */

/* the theme's font files; sizes are made on first use and kept until the theme changes */
static struct { int px, bold; lv_font_t *f; } fonts[24];
static int nfonts;

int ui_fonts(void)
{
	/* the theme first: it names the fonts */
	settings_load(&cfg);
	if (force_theme) snprintf(cfg.theme, sizeof(cfg.theme), "%s", force_theme);
	if (theme_load(cfg.theme)) fprintf(stderr, "emuzero-launcher: theme %s not found, using the built-in one\n", cfg.theme);

	lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
	if (!theme.font[0][0] || !theme.font[1][0]) { fprintf(stderr, "emuzero-launcher: no fonts found (DejaVu)\n"); return -1; }
	return ui_font(13, 0) ? 0 : -1;
}

static void fonts_drop(void)
{
	for (int i = 0; i < nfonts; i++) lv_freetype_font_delete(fonts[i].f);
	nfonts = 0;
}

lv_font_t *ui_font(int px, int bold)
{
	bold = !!bold;
	for (int i = 0; i < nfonts; i++)
		if (fonts[i].px == px && fonts[i].bold == bold) return fonts[i].f;
	lv_font_t *f = lv_freetype_font_create(theme.font[bold], LV_FREETYPE_FONT_RENDER_MODE_BITMAP, px,
	                                       LV_FREETYPE_FONT_STYLE_NORMAL);
	if (!f && strcmp(theme.font[bold], path_font(bold) ? path_font(bold) : "")) {   /* a theme font that does not load */
		snprintf(theme.font[bold], sizeof(theme.font[bold]), "%s", path_font(bold) ? path_font(bold) : "");
		f = lv_freetype_font_create(theme.font[bold], LV_FREETYPE_FONT_RENDER_MODE_BITMAP, px, LV_FREETYPE_FONT_STYLE_NORMAL);
	}
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
static struct ui_rect page_rect;

int ui_set_layout(const char *name)
{
	if (!layout_find(name)) return -1;
	force_layout = name;
	return 0;
}

void ui_set_theme(const char *id) { force_theme = id; }

struct ui_rect ui_page_rect(void) { return page_rect; }

/* the layout: --layout, else the one chosen in Theme, else the theme's own */
static const struct layout *pick_layout(void)
{
	const struct layout *l = force_layout ? layout_find(force_layout) : NULL;
	if (!l && cfg.layout[0]) l = layout_find(cfg.layout);
	if (!l) l = layout_find(theme.layout);
	return l ? l : layouts[0];
}

/* raised: light along the top and left, shade along the bottom and right; sunken: the reverse.
 * Two nested borders, since an LVGL border has one colour. o must have no padding. */
static void bevel(lv_obj_t *o, int style, int w)
{
	if (style == BEVEL_NONE || w <= 0) return;
	uint32_t tl = style == BEVEL_RAISED ? theme.light : theme.shadow, br = style == BEVEL_RAISED ? theme.shadow : theme.light;
	lv_obj_set_style_border_width(o, w, 0);
	lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
	lv_obj_set_style_border_color(o, lv_color_hex(tl), 0);
	lv_obj_t *in = lv_obj_create(o);
	lv_obj_remove_style_all(in);
	lv_obj_remove_flag(in, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(in, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
	lv_obj_set_size(in, lv_obj_get_width(o), lv_obj_get_height(o));
	lv_obj_set_pos(in, -w, -w);   /* children sit inside the border; this one covers it */
	lv_obj_set_style_border_width(in, w, 0);
	lv_obj_set_style_border_side(in, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
	lv_obj_set_style_border_color(in, lv_color_hex(br), 0);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
	lv_obj_t *o = lv_obj_create(parent);
	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	if (bg != UINT32_MAX) { lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0); }
	return o;
}

/* Everything on the screen, from the theme: background or wallpaper, status bar, the window
 * frame if the theme has one, the area the layout draws in, and the message line. Built again
 * when the theme changes. */
static void build(void)
{
	lv_obj_clean(scr);
	lv_obj_set_style_bg_color(scr, lv_color_hex(theme.bg), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	int bh = statusbar_height(), top = theme.bar_bottom ? 0 : bh;
	page_rect = (struct ui_rect){ 0, top, SCREEN_W, SCREEN_H - bh };

	if (theme.wallpaper[0]) {
		lv_draw_buf_t *wp = picture_load(theme.wallpaper, SCREEN_W, SCREEN_H);
		if (wp) { lv_obj_t *img = lv_image_create(scr); lv_image_set_src(img, wp); lv_obj_set_pos(img, 0, 0); }
	}
	statusbar_create(scr);
	startup_mark("bar");

	struct ui_rect r = page_rect;
	lv_obj_t *parent = scr;
	if (theme.win) {
		int m = theme.win_margin, b = theme.win_bevel ? 2 : theme.win_border;
		lv_obj_t *w = box(scr, m, top + m, SCREEN_W - 2 * m, r.h - 2 * m, theme.win_bg);
		if (theme.win_bevel) bevel(w, theme.win_bevel, 2);
		else if (b) { lv_obj_set_style_border_width(w, b, 0); lv_obj_set_style_border_color(w, lv_color_hex(theme.win_border_color), 0); }
		int iw = SCREEN_W - 2 * m - 2 * b, ih = r.h - 2 * m - 2 * b, th = theme.win_title_h;
		if (th) {
			lv_obj_t *t = box(w, 0, 0, iw, th, theme.win_title_bg);
			if (theme.win_stripes)   /* lines across the title bar, every other row */
				for (int y = 2; y < th - 2; y += 2) box(t, 2, y, iw - 4, 1, theme.win_title_fg);
			if (theme.win_close) {
				lv_obj_t *c = box(t, 6, (th - 9) / 2, 9, 9, theme.win_title_bg);
				lv_obj_set_style_border_width(c, 1, 0);
				lv_obj_set_style_border_color(c, lv_color_hex(theme.win_title_fg), 0);
				lv_obj_set_style_outline_width(c, 1, 0);
				lv_obj_set_style_outline_color(c, lv_color_hex(theme.win_title_bg), 0);
			}
			if (theme.win_title[0]) {
				lv_obj_t *l = ui_label(t, ui_font(th >= 14 ? 11 : 10, 1), theme.win_title_fg, theme.win_title);
				lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
				lv_obj_set_style_bg_color(l, lv_color_hex(theme.win_title_bg), 0);
				lv_obj_set_style_pad_hor(l, 5, 0);
				lv_obj_align(l, theme.win_stripes ? LV_ALIGN_CENTER : LV_ALIGN_LEFT_MID, theme.win_stripes ? 0 : 4, 0);
			}
			if (!theme.win_bevel) box(w, 0, th, iw, 1, theme.win_border_color);   /* line under the title */
		}
		parent = w;
		r = (struct ui_rect){ 0, th ? th + 1 : 0, iw, ih - (th ? th + 1 : 0) };
	}
	area = box(parent, r.x, r.y, r.w, r.h, UINT32_MAX);

	toast = ui_label(scr, ui_font(12, 1), theme.sel_fg, "");
	lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(toast, lv_color_hex(theme.sel_bg), 0);
	lv_obj_set_style_border_width(toast, 1, 0);
	lv_obj_set_style_border_color(toast, lv_color_hex(theme.accent), 0);
	lv_obj_set_style_radius(toast, theme.card_radius ? 6 : 0, 0);
	lv_obj_set_style_pad_hor(toast, 8, 0);
	lv_obj_set_style_pad_ver(toast, 3, 0);
	lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -4 - (theme.bar_bottom ? bh : 0));
	lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
	if (toast_timer) { lv_timer_delete(toast_timer); toast_timer = NULL; }
	layout = pick_layout();
}

static void show_layout(void)
{
	lv_obj_clean(area);
	lv_obj_update_layout(area);   /* layouts size themselves from the area */
	layout->create(area);
	layout->show(sel);
	lv_obj_move_foreground(toast);
}

/* load theme id and draw everything again (the Theme page calls this as you choose) */
void ui_apply(const char *theme_id, const char *layout_name)
{
	snprintf(cfg.theme, sizeof(cfg.theme), "%s", theme_id);
	snprintf(cfg.layout, sizeof(cfg.layout), "%s", layout_name ? layout_name : "");
	force_theme = force_layout = NULL;
	theme_load(cfg.theme);
	fonts_drop();
	icon_cache_clear();
	build();
	show_layout();
}

void ui_save(void)
{
	if (settings_save(&cfg)) fprintf(stderr, "emuzero-launcher: the settings could not be saved\n");
}

const struct settings *ui_settings(void) { return &cfg; }

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

/* NimbleZero's boot report (nz-boottime) reads the uptime of the launcher's first frame from
 * /run/nz/t_launcher_frame: nz-console leaves it empty at boot, and only the first start of a
 * launcher fills it */
static void note_first_frame(void)
{
	int fd = open("/run/nz/t_launcher_frame", O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return;
	struct stat st;
	char up[32];
	if (!fstat(fd, &st) && st.st_size == 0 && sys_read("/proc/uptime", up, sizeof(up)) == 0) {
		up[strcspn(up, " \n")] = 0;
		dprintf(fd, "%s\n", up);
	}
	close(fd);
}

static void first_settings(lv_timer_t *t)
{
	(void)t;
	note_first_frame();
	startup_mark("first frame");
#if EMUZERO_PLATFORM == EMUZERO_FBDEV
	fprintf(stderr, "emuzero-launcher: ms after start: %s\n", startup_marks());
#endif
	read_settings();
}

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

static void theme_closed(void)
{
	lv_obj_move_foreground(toast);
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
	if (themepage_active()) { themepage_key(k); return; }
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
		if (!napps) break;
		if (!strcmp(apps[sel].exec, APP_THEME)) themepage_open(scr, theme_closed);
		else launch(&apps[sel]);
		break;
	case 't':
		themepage_open(scr, theme_closed);
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
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	idle = lv_timer_create(idle_cb, 120000, NULL);
	idle_s = 120;
	lv_timer_t *t = lv_timer_create(first_settings, 1, NULL);   /* after the first frame */
	lv_timer_set_repeat_count(t, 1);

	build();
	apps_scan();
	startup_mark("apps");
	show_layout();

	/* keys go to the screen itself (a hidden object would never take the group's focus) */
	lv_obj_add_event_cb(scr, key_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(g, scr);
}
