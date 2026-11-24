// SPDX-License-Identifier: MIT
// App info: what an app is and where it came from, and the actions Back, Update and Remove.
//
// Everything comes from NimbleZero's own tools, run as separate programs:
//   the package     dpkg-query -S <entry file>   (no package: one of NimbleZero's own apps)
//   version, size   dpkg-query -W                (status "hold": part of the system)
//   newer version   the Store's index, /data/cache/nz-store/catalogue.tsv
//   remove, update  sudo -n /usr/sbin/nz-admin store remove|install <package>
// The same rules as NimbleZero's Store and its Del key: Back is selected when the page opens,
// so Enter twice never changes anything; Remove asks first with "No" selected and names the
// other apps the same package brings; NimbleZero's own apps and held packages are refused.
#include "appinfo.h"
#include "icons.h"
#include "paths.h"
#include "platform.h"
#include "statusbar.h"
#include "sys.h"
#include "theme.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN "/usr/sbin/nz-admin"
#define CATALOGUE "/data/cache/nz-store/catalogue.tsv"

enum action { BACK, UPDATE, REMOVE, KEEP, YES, OK };
enum page { INFO, CONFIRM, RESULT };

static lv_obj_t *panel;
static void (*on_done)(int changed);
static struct app app;
static char pkg[64], ver[64], newer[64], others[160], note[160], result[200];
static long size_kb;
static int removable, changed;
static enum action acts[3];
static int nacts, cur;

int appinfo_active(void) { return panel != NULL; }

/* ---- facts ---- */

static void trim(char *s) { s[strcspn(s, "\n")] = 0; }

static void find_package(void)
{
	char out[512], *argv[] = { "dpkg-query", "-S", app.file, NULL };
	pkg[0] = ver[0] = newer[0] = others[0] = 0;
	size_kb = -1;
	removable = 0;
	snprintf(note, sizeof(note), "Part of NimbleZero; it cannot be removed.");
	if (!app.file[0]) { snprintf(note, sizeof(note), "Part of emuZero; it cannot be removed."); return; }
	/* "camera: /usr/share/APPLaunch/applications/camera_app.desktop" */
	if (sys_run(argv, out, sizeof(out)) != 0) return;
	char *colon = strstr(out, ": ");
	if (!colon || colon - out >= (long)sizeof(pkg) || strchr(out, ' ') < colon) return;
	snprintf(pkg, sizeof(pkg), "%.*s", (int)(colon - out), out);

	char fmt[] = "-f=${Version}\t${Installed-Size}\t${db:Status-Abbrev}", *w[] = { "dpkg-query", "-W", fmt, pkg, NULL };
	if (sys_run(w, out, sizeof(out)) != 0) return;
	char *t1 = strchr(out, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
	if (!t2) return;
	*t1 = *t2 = 0;
	snprintf(ver, sizeof(ver), "%.63s", out);
	size_kb = t1[1] ? atol(t1 + 1) : -1;
	if (t2[1] == 'h') { snprintf(note, sizeof(note), "Part of the system; it cannot be removed."); return; }
	removable = 1;
	note[0] = 0;

	/* other launcher entries from the same package go with it */
	char list[4096], *l[] = { "dpkg-query", "-L", pkg, NULL };
	if (sys_run(l, list, sizeof(list)) == 0)
		for (char *line = strtok(list, "\n"); line; line = strtok(NULL, "\n")) {
			size_t n = strlen(line);
			if (n < 9 || strcmp(line + n - 8, ".desktop") || !strstr(line, "/applications/") || !strcmp(line, app.file)) continue;
			char p[512], row[256], name[64] = "";
			FILE *f = fopen(path_join(p, sizeof(p), line), "r");
			while (f && fgets(row, sizeof(row), f))
				if (!strncmp(row, "Name=", 5)) { snprintf(name, sizeof(name), "%.63s", row + 5); trim(name); break; }
			if (f) fclose(f);
			if (name[0] && strlen(others) + strlen(name) + 3 < sizeof(others))
				snprintf(others + strlen(others), sizeof(others) - strlen(others), "%s%s", others[0] ? ", " : "", name);
		}

	/* the Store's index: name, version, ... separated by the ASCII unit separator */
	char p[512], row[2048];
	FILE *f = fopen(path_join(p, sizeof(p), CATALOGUE), "r");
	while (f && fgets(row, sizeof(row), f)) {
		char *us = strchr(row, '\037');
		if (!us || (size_t)(us - row) != strlen(pkg) || strncmp(row, pkg, us - row)) continue;
		char *end = strchr(us + 1, '\037');
		if (end) *end = 0;
		char *c[] = { "dpkg", "--compare-versions", us + 1, "gt", ver, NULL }, o[64];
		if (sys_run(c, o, sizeof(o)) == 0) snprintf(newer, sizeof(newer), "%s", us + 1);
		break;
	}
	if (f) fclose(f);
}

/* ---- drawing ---- */

static const char *act_text(enum action a)
{
	static char up[80];
	switch (a) {
	case BACK: return "Back";
	case UPDATE: snprintf(up, sizeof(up), "Update to %s", newer); return up;
	case REMOVE: return "Remove";
	case KEEP: return "No, keep it";
	case YES: return "Yes, remove";
	case OK: return "OK";
	}
	return "";
}

static lv_obj_t *buttons_row;

static void draw_buttons(void)
{
	lv_obj_clean(buttons_row);
	for (int i = 0; i < nacts; i++) {
		lv_obj_t *b = ui_label(buttons_row, ui_font(13, 1), i == cur ? theme.sel_fg : theme.dim, act_text(acts[i]));
		lv_obj_set_style_radius(b, theme.card_radius ? 6 : 0, 0);
		lv_obj_set_style_pad_hor(b, 9, 0);
		lv_obj_set_style_pad_ver(b, 4, 0);
		lv_obj_set_style_border_width(b, i == cur ? 2 : 1, 0);
		lv_obj_set_style_border_color(b, lv_color_hex(i == cur ? theme.accent : theme.line), 0);
		lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(b, lv_color_hex(i == cur ? theme.sel_bg : theme.bg), 0);
	}
}

static lv_obj_t *text(lv_obj_t *parent, int px, int bold, uint32_t c, const char *s, int w)
{
	lv_obj_t *l = ui_label(parent, ui_font(px, bold), c, s);
	lv_obj_set_width(l, w);
	lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
	return l;
}

/* a page: the app's icon and a title, body lines, then the action buttons along the bottom */
static void page(const char *title, const char *body, const enum action *a, int n, int sel)
{
	lv_obj_clean(panel);
	lv_obj_t *col = lv_obj_create(panel);
	lv_obj_remove_style_all(col);
	lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(col, SCREEN_W - 16, ui_page_rect().h - 40);
	lv_obj_set_pos(col, 8, 6);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(col, 3, 0);

	lv_obj_t *head = lv_obj_create(col);
	lv_obj_remove_style_all(head);
	lv_obj_set_size(head, LV_PCT(100), 36);
	lv_obj_t *ic = icon_create(head, &app, 32);
	lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
	lv_obj_t *t = ui_label(head, ui_font(16, 1), theme.fg, title);
	lv_obj_set_width(t, SCREEN_W - 16 - 42);
	lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
	lv_obj_set_height(t, lv_font_get_line_height(ui_font(16, 1)));
	lv_obj_align(t, LV_ALIGN_LEFT_MID, 42, 0);

	text(col, 12, 0, theme.dim, body, SCREEN_W - 16);

	buttons_row = lv_obj_create(panel);
	lv_obj_remove_style_all(buttons_row);
	lv_obj_remove_flag(buttons_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(buttons_row, SCREEN_W - 16, 30);
	lv_obj_align(buttons_row, LV_ALIGN_BOTTOM_LEFT, 8, -6);
	lv_obj_set_flex_flow(buttons_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(buttons_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(buttons_row, 8, 0);
	if (n) memcpy(acts, a, sizeof(a[0]) * n);
	nacts = n;
	cur = sel;
	draw_buttons();
}

static void size_text(char *b, size_t n, long kb)
{
	if (kb < 0) snprintf(b, n, "unknown");
	else if (kb < 1000) snprintf(b, n, "%ld KB", kb);
	else snprintf(b, n, "%ld.%ld MB", kb / 1024, kb % 1024 * 10 / 1024);
}

static void info_page(void)
{
	char body[600], sz[32];
	size_text(sz, sizeof(sz), size_kb);
	int o = 0;
	body[0] = 0;
#define LINE(...) (o += snprintf(body + o, sizeof(body) - o, "%s", o ? "\n" : ""), \
                   o += snprintf(body + o, sizeof(body) - o, __VA_ARGS__))
	if (app.comment[0]) LINE("%s", app.comment);
	if (pkg[0]) LINE("Version %s, package %s", ver[0] ? ver : "?", pkg);
	if (pkg[0] && size_kb >= 0) LINE("Size on disk %s", sz);
	if (newer[0]) LINE("The Store has version %s.", newer);
	if (note[0]) LINE("%s", note);
#undef LINE
	enum action a[3] = { BACK };
	int n = 1;
	if (newer[0] && removable) a[n++] = UPDATE;
	if (removable) a[n++] = REMOVE;
	page(app.name, body, a, n, 0);
}

static void confirm_page(void)
{
	char title[96], body[400];
	snprintf(title, sizeof(title), "Remove %s?", app.name);
	int o = snprintf(body, sizeof(body), "%s comes from the package %s %s. It and its files are deleted from the device.", app.name, pkg, ver);
	if (others[0]) snprintf(body + o, sizeof(body) - o, "\nAlso removed with it: %s.", others);
	static const enum action a[] = { KEEP, YES };
	page(title, body, a, 2, 0);
}

/* run nz-admin with "Removing..." on the screen, then show how it went */
static void run_admin(const char *verb, const char *doing, const char *done)
{
	char title[96], out[2048];
	snprintf(title, sizeof(title), "%s %s...", doing, app.name);
	page(title, "This can take a minute.", NULL, 0, 0);
	lv_refr_now(NULL);
	char *argv[] = { "sudo", "-n", ADMIN, "store", (char *)verb, pkg, NULL };
	int rc = sys_run(argv, out, sizeof(out));
	fprintf(stderr, "emuzero-launcher: nz-admin store %s %s: %d\n", verb, pkg, rc);
	if (rc == 0) {
		snprintf(result, sizeof(result), "%s %s.", app.name, done);
		changed = 1;
	} else {
		/* the last line nz-admin printed says why */
		char *end = out + strlen(out);
		while (end > out && (end[-1] == '\n' || end[-1] == ' ')) *--end = 0;
		char *last = strrchr(out, '\n');
		snprintf(result, sizeof(result), "Nothing was changed.\n%s", last ? last + 1 : out[0] ? out : "nz-admin could not be run.");
	}
	static const enum action a[] = { OK };
	snprintf(title, sizeof(title), "%s", app.name);
	page(title, result, a, 1, 0);
	statusbar_update();
}

/* ---- keys ---- */

static void close_info(void)
{
	lv_obj_delete(panel);
	panel = NULL;
	if (on_done) on_done(changed);
}

void appinfo_key(uint32_t k)
{
	if (!panel) return;
	if (k == LV_KEY_LEFT || k == LV_KEY_UP || k == 'z' || k == 'f' || k == LV_KEY_PREV) {
		if (cur > 0) { cur--; draw_buttons(); }
		return;
	}
	if (k == LV_KEY_RIGHT || k == LV_KEY_DOWN || k == 'c' || k == 'x' || k == LV_KEY_NEXT) {
		if (cur < nacts - 1) { cur++; draw_buttons(); }
		return;
	}
	enum action a = k == LV_KEY_ENTER && nacts ? acts[cur] : k == LV_KEY_ESC || k == LV_KEY_BACKSPACE ? BACK : (enum action)-1;
	switch (a) {
	case BACK: case OK: close_info(); break;
	case KEEP: info_page(); break;
	case REMOVE: confirm_page(); break;
	case YES: run_admin("remove", "Removing", "was removed"); break;
	case UPDATE: run_admin("install", "Updating", "was updated"); break;
	default: break;
	}
}

void appinfo_open(lv_obj_t *scr, const struct app *a, void (*done)(int changed))
{
	app = *a;
	on_done = done;
	changed = 0;
	panel = lv_obj_create(scr);
	lv_obj_remove_style_all(panel);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	struct ui_rect r = ui_page_rect();
	lv_obj_set_pos(panel, r.x, r.y);
	lv_obj_set_size(panel, r.w, r.h);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(panel, lv_color_hex(theme.bg), 0);
	find_package();
	fprintf(stderr, "emuzero-launcher: app info %s (package %s%s)\n", app.id, pkg[0] ? pkg : "none", removable ? ", removable" : "");
	info_page();
}
