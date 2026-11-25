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
static lv_obj_t *scr, *area, *toast, *status_l, *status_r;
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
uint32_t ui_page_bg(void) { return theme.win ? theme.win_bg : theme.bg; }

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
lv_obj_t *ui_bevel(lv_obj_t *o, int style, int w)
{
	if (style == BEVEL_NONE || w <= 0) return NULL;
	uint32_t tl = style == BEVEL_RAISED ? theme.light : theme.shadow, br = style == BEVEL_RAISED ? theme.shadow : theme.light;
	lv_obj_update_layout(o);   /* its size, below, may still be pending */
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
	return in;
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

/* the picture in the home window's title bar: [window] title_icon names an app id */
static const struct app *title_icon(void)
{
	static struct app a;
	char p[512], f[80];
	memset(&a, 0, sizeof(a));
	if (theme.win_title_pic[0]) {   /* a picture of the theme's own */
		snprintf(a.id, sizeof(a.id), "title-icon");
		snprintf(a.icon, sizeof(a.icon), "%s", theme.win_title_pic);
		return &a;
	}
	if (!theme.win_title_icon[0]) return NULL;
	snprintf(a.id, sizeof(a.id), "%s", theme.win_title_icon);
	snprintf(f, sizeof(f), "/usr/share/emuzero/icons/%.60s.png", theme.win_title_icon);
	if (!access(path_join(p, sizeof(p), f), R_OK)) snprintf(a.icon, sizeof(a.icon), "%s", p);
	return icon_file(&a) ? &a : NULL;
}

/* a title-bar button s x s at x: the close cross, or the minimise or maximise sign */
enum { BTN_CLOSE, BTN_MAX, BTN_MIN };
static void title_button(lv_obj_t *t, int x, int s, int kind)
{
	uint32_t bg = kind == BTN_CLOSE ? theme.close_bg : theme.min_max_bg, fg = theme.close_fg;
	lv_obj_t *c = box(t, x, (theme.win_title_h - s) / 2, s, s, bg);
	int in = 0;   /* the frame's width: what the signs sit inside */
	if (theme.win_bevel) { ui_bevel(c, BEVEL_RAISED, 1); in = 1; }
	else {
		lv_obj_set_style_radius(c, theme.win_radius ? 3 : 0, 0);
		lv_obj_set_style_border_width(c, 1, 0);
		lv_obj_set_style_border_color(c, lv_color_hex(fg), 0);
		in = 1;
	}
	int a = s - 2 * in;   /* inner size */
	if (kind == BTN_CLOSE) {
		lv_obj_t *x = ui_label(c, ui_font(s - 2 > 8 ? s - 2 : 8, 1), fg, "\u00d7");
		lv_obj_align(x, LV_ALIGN_CENTER, 0, -1);
	} else if (kind == BTN_MIN) {
		int w = a / 2 + 1;
		box(c, (a - w) / 2 - 1, a - 4, w, 2, fg);
	} else {
		int w = a - 5;
		lv_obj_t *f = box(c, (a - w) / 2, (a - w) / 2, w, w, UINT32_MAX);
		lv_obj_set_style_border_width(f, 1, 0);
		lv_obj_set_style_border_color(f, lv_color_hex(fg), 0);
		box(f, 0, 0, w - 2, 1, fg);   /* the thick top edge */
	}
}

/* a row of sunken panels for the status bar; returns the label in it */
static lv_obj_t *status_panel(lv_obj_t *p, int x, int y, int w, int h)
{
	lv_obj_t *o = box(p, x, y, w, h, UINT32_MAX);
	ui_bevel(o, BEVEL_SUNKEN, 1);
	lv_obj_t *l = ui_label(o, ui_font(10, 0), theme.fg, "");
	lv_obj_set_width(l, w - 8);
	lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
	lv_obj_align(l, LV_ALIGN_LEFT_MID, 3, 0);
	return l;
}

void ui_window(lv_obj_t *parent, int x, int y, int w, int h, const char *title, const struct app *icon, int folder, struct ui_win *out)
{
	int b = theme.win_bevel ? 2 : theme.win_border;
	lv_obj_t *win = box(parent, x, y, w, h, theme.win_bg);
	if (theme.win_radius) {   /* rounded, and what lies inside cut to the same corners */
		lv_obj_set_style_radius(win, theme.win_radius, 0);
		lv_obj_set_style_clip_corner(win, true, 0);
	}
	if (theme.win_bevel) ui_bevel(win, theme.win_bevel, 2);
	else if (b) { lv_obj_set_style_border_width(win, b, 0); lv_obj_set_style_border_color(win, lv_color_hex(theme.win_border_color), 0); }
	int iw = w - 2 * b, ih = h - 2 * b, th = theme.win_title_h, y0 = 0;
	if (th) {
		lv_obj_t *t = box(win, 0, 0, iw, th, theme.win_title_bg);
		if (theme.win_title_grad != COLOR_UNSET) {
			lv_obj_set_style_bg_grad_color(t, lv_color_hex(theme.win_title_grad), 0);
			lv_obj_set_style_bg_grad_dir(t, theme.win_grad_hor ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
		}
		if (theme.win_stripes)   /* lines across the title bar, every other row */
			for (int y = 2; y < th - 2; y += 2) box(t, 2, y, iw - 4, 1, theme.win_title_fg);
		int right = iw;   /* where the title text must stop */
		if (theme.win_close == CLOSE_RIGHT && th >= 8) {
			int s = th - 4, gap = theme.win_bevel ? 0 : 2;
			right = iw - s - 2;
			title_button(t, right, s, BTN_CLOSE);
			if (theme.win_min_max && folder) {   /* a dialog has the close button alone */
				right -= s + 2;
				title_button(t, right, s, BTN_MAX);
				right -= s + gap;
				title_button(t, right, s, BTN_MIN);
			}
		} else if (theme.win_close == CLOSE_LEFT) {
			lv_obj_t *c = box(t, 6, (th - 9) / 2, 9, 9, theme.win_title_bg);
			lv_obj_set_style_border_width(c, 1, 0);
			lv_obj_set_style_border_color(c, lv_color_hex(theme.win_title_fg), 0);
			lv_obj_set_style_outline_width(c, 1, 0);
			lv_obj_set_style_outline_color(c, lv_color_hex(theme.win_title_bg), 0);
		}
		int tx = 4;   /* the label's left edge; its padding puts the text 5 further */
		if (icon && th >= 12) {   /* the window's picture before its title */
			int s = th - 4;
			lv_obj_t *ic = icon_create(t, icon, s);
			lv_obj_set_pos(ic, 3, 2);
			tx = s;
		}
		if (title && title[0]) {
			lv_obj_t *l = ui_label(t, ui_font(th >= 14 ? 11 : 10, 1), theme.win_title_fg, title);
			lv_obj_set_style_bg_opa(l, theme.win_title_grad == COLOR_UNSET ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
			lv_obj_set_style_bg_color(l, lv_color_hex(theme.win_title_bg), 0);
			lv_obj_set_style_pad_hor(l, 5, 0);
			lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
			lv_obj_update_layout(l);
			int max = right - tx - 4;
			if (lv_obj_get_width(l) > max) lv_obj_set_width(l, max);
			lv_obj_align(l, theme.win_stripes ? LV_ALIGN_CENTER : LV_ALIGN_LEFT_MID, theme.win_stripes ? 0 : tx, 0);
		}
		if (!theme.win_bevel) box(win, 0, th, iw, 1, theme.win_border_color);   /* line under the title */
		y0 = th + 1;
	}
	out->status_l = out->status_r = NULL;
	int y1 = ih;   /* the bottom of what the contents may use */
	if (folder && theme.win_menu[0]) {   /* menu bar: the words, first letters underlined */
		int mh = 13, mx = 6;
		char words[sizeof(theme.win_menu)], *save;
		snprintf(words, sizeof(words), "%s", theme.win_menu);
		lv_font_t *f = ui_font(11, 0);
		for (char *wd = strtok_r(words, " ", &save); wd; wd = strtok_r(NULL, " ", &save)) {
			lv_obj_t *l = ui_label(win, f, theme.fg, wd);
			lv_obj_set_pos(l, mx, y0);
			if (theme.win_menu_underline) {
				int u = lv_text_get_width(wd, 1, f, 0);
				box(win, mx, y0 + lv_font_get_line_height(f) - 1, u, 1, theme.fg);
			}
			mx += lv_text_get_width(wd, strlen(wd), f, 0) + 9;
		}
		y0 += mh;
		if (!theme.win_bevel) { box(win, 0, y0, iw, 1, theme.line); y0 += 1; }
		y0 += 1;
	}
	if (folder && theme.win_status[0]) {   /* status bar: the selected app, the number of apps */
		int sh = 14, rw = 82;
		y1 -= sh;
		out->status_l = status_panel(win, 0, y1 + 1, iw - rw - 2, sh - 1);
		out->status_r = status_panel(win, iw - rw, y1 + 1, rw, sh - 1);
		y1 -= 1;
	}
	lv_obj_t *body = win;
	int bx = 0, cw = iw, ch = y1 - y0;
	if (folder && theme.win_client != COLOR_UNSET) {   /* the apps' own area, in a frame of its own */
		lv_obj_t *c = box(win, 0, y0, iw, ch, theme.win_client);
		int cb = 0;
		if (theme.win_client_bevel) { ui_bevel(c, theme.win_client_bevel, 2); cb = 2; }
		else if (theme.win_client_line != COLOR_UNSET) {
			lv_obj_set_style_border_width(c, 1, 0);
			lv_obj_set_style_border_color(c, lv_color_hex(theme.win_client_line), 0);
			cb = 1;
		}
		body = c;
		y0 = 0;
		cw = iw - 2 * cb;
		ch -= 2 * cb;
	}
	out->body = box(body, bx, y0, cw, ch, UINT32_MAX);
	out->frame = win;
	out->w = cw;
	out->h = ch;
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
	status_l = status_r = NULL;
	if (theme.win) {
		struct ui_win w;
		int mx = theme.win_margin_x, my = theme.win_margin_y;
		ui_window(scr, mx, top + my, SCREEN_W - 2 * mx, r.h - 2 * my, theme.win_title, title_icon(), 1, &w);
		parent = w.body;
		status_l = w.status_l;
		status_r = w.status_r;
		r = (struct ui_rect){ 0, 0, w.w, w.h };
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

/* the window's status bar: the selected app's description, the number of apps */
static void status_show(void)
{
	char s[64];
	if (status_l) lv_label_set_text(status_l, !napps ? "" : apps[sel].comment[0] ? apps[sel].comment : apps[sel].name);
	if (status_r) { snprintf(s, sizeof(s), theme.win_status, napps); lv_label_set_text(status_r, s); }
}

static void show_sel(void)
{
	layout->show(sel);
	status_show();
}

static void show_layout(void)
{
	lv_obj_clean(area);
	lv_obj_update_layout(area);   /* layouts size themselves from the area */
	layout->create(area);
	show_sel();
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
			if (n != sel) { sel = n; show_sel(); }
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
