// SPDX-License-Identifier: MIT
// The status bar and the overlays: action menu, pickers, yes/no question, keys sheet, toasts.
// Notes' ui.c with Wiki's key table and screens: the same functions (notes.h), so the shared
// modules and both apps look alike.
#include "wiki.h"
#include "status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

unsigned ui_mods;

/* ---- status bar ---- */

static struct status stat_now;
static uint32_t stat_ms;

void bar_draw(const char *title, const char *state, uint32_t state_color)
{
	d_rect(0, 0, SCREEN_W, BAR_H, pal.bar_bg);
	if (pal.bar_line != pal.bar_bg) d_rect(0, BAR_H - 1, SCREEN_W, 1, pal.bar_line);
	uint32_t t = now_ms();
	if (!stat_ms || t - stat_ms > 30000) { status_read(&stat_now); stat_ms = t ? t : 1; }
	/* right to left: clock, battery, layer badges, save state */
	int x = SCREEN_W - 4;
	char clk[8];
	time_t now = time(NULL);
	strftime(clk, sizeof(clk), "%H:%M", localtime(&now));
	x -= text_w(F_UI, clk, strlen(clk));
	d_text(F_UI, x, 2, pal.bar_fg, clk, strlen(clk));
	if (stat_now.battery >= 0) {
		char b[16];
		snprintf(b, sizeof(b), "%d%%", stat_now.battery);
		int bw = text_w(F_UISMALL, b, strlen(b));
		x -= bw + 8;
		d_text(F_UISMALL, x, 3, stat_now.battery <= 15 && !stat_now.charging ? pal.warn : pal.bar_fg, b, strlen(b));
		if (stat_now.charging == 1) { x -= 8; d_text(F_UISMALL, x, 3, pal.bar_fg, "⚡", 3); }
	}
	static const struct { unsigned m; const char *s; } badges[] = { { M_SYM, "Sym" }, { M_FN, "Fn" }, { M_SHIFT, "⇧" } };
	for (int i = 0; i < 3; i++) {
		if (!(ui_mods & badges[i].m)) continue;
		int bw = text_w(F_UISMALL, badges[i].s, strlen(badges[i].s)) + 6;
		x -= bw + 4;
		d_rrect(x, 2, bw, BAR_H - 5, 3, pal.accent);
		d_text(F_UISMALL, x + 3, 3, pal.bg, badges[i].s, strlen(badges[i].s));
	}
	if (state && *state) {
		int sw = text_w(F_UISMALL, state, strlen(state));
		x -= sw + 8;
		d_text(F_UISMALL, x, 3, state_color, state, strlen(state));
	}
	d_text_fit(F_UIBOLD, 5, 2, x - 12, pal.title_fg, title);
}

/* ---- overlays ---- */

enum { OV_NONE, OV_MENU, OV_CONFIRM, OV_HELP };
static int ov;
static char ov_title[96];
static struct mitem *items;
static int nitems, sel, top;
static void (*menu_done)(int id, int index);
static void (*confirm_done)(int yes);
static char confirm_yes[24];
static int help_ctx, help_top, help_n;
static char help_lines[80][2][48];
void redraw(void);

static char toast_msg[160];
static lv_timer_t *toast_timer;

static void toast_hide(lv_timer_t *t) { lv_timer_pause(t); toast_msg[0] = 0; redraw(); }

void toast(const char *msg)
{
	snprintf(toast_msg, sizeof(toast_msg), "%s", msg);
	if (!toast_timer) toast_timer = lv_timer_create(toast_hide, 2600, NULL);
	lv_timer_reset(toast_timer);
	lv_timer_resume(toast_timer);
	redraw();
}

int overlay_active(void) { return ov != OV_NONE; }

void menu_open(const char *title, struct mitem *it, int n, int s, void (*done)(int id, int index))
{
	static struct mitem copy[64];
	if (n > 64) n = 64;
	memcpy(copy, it, sizeof(*it) * (size_t)n);
	items = copy; nitems = n; sel = s < n ? s : 0; top = 0;
	snprintf(ov_title, sizeof(ov_title), "%s", title);
	menu_done = done;
	ov = OV_MENU;
	while (sel < nitems - 1 && items[sel].off) sel++;
	redraw();
}

void confirm_open(const char *msg, const char *yes, void (*done)(int yes))
{
	snprintf(ov_title, sizeof(ov_title), "%s", msg);
	snprintf(confirm_yes, sizeof(confirm_yes), "%s", yes);
	confirm_done = done;
	sel = 0;   /* No first */
	ov = OV_CONFIRM;
	redraw();
}

void help_open(int ctx)
{
	help_ctx = ctx;
	help_top = 0;
	help_n = 0;
	int seen[W_COUNT] = { 0 };
	for (const struct wbind *b = wbinds; b->label && help_n < 80; b++) {
		if (!(b->ctx & ctx) || !b->label[0]) continue;
		int c = b->cmd;
		if (seen[c]) {   /* a second key for the same thing: "Fn+F / F" */
			char *k = help_lines[seen[c] - 1][0];
			if (strlen(k) + strlen(b->label) + 4 < sizeof(help_lines[0][0]) && !strstr(k, b->label)) {
				strcat(k, " / ");
				strcat(k, b->label);
			}
			continue;
		}
		seen[c] = help_n + 1;
		snprintf(help_lines[help_n][0], sizeof(help_lines[0][0]), "%s", b->label);
		snprintf(help_lines[help_n][1], sizeof(help_lines[0][1]), "%s", wcmd_name(c));
		help_n++;
	}
	if (ctx == X_HOME && help_n < 79) {
		snprintf(help_lines[help_n][0], sizeof(help_lines[0][0]), "Type");
		snprintf(help_lines[help_n++][1], sizeof(help_lines[0][1]), "Search titles as you type");
	}
	if (ctx == X_TRIAGE && help_n < 79) {
		snprintf(help_lines[help_n][0], sizeof(help_lines[0][0]), "1 to 5");
		snprintf(help_lines[help_n++][1], sizeof(help_lines[0][1]), "Choose that answer");
	}
	ov = OV_HELP;
	redraw();
}

static int menu_rows(void) { return (SCREEN_H - 44) / 15; }

int overlay_key(struct nkey k)
{
	if (ov == OV_NONE) return 0;
	uint32_t c = k.k;
	int ctrl = (k.mods & M_CTRL) != 0;
	if (ov == OV_HELP) {
		int rows = (SCREEN_H - BAR_H - 22) / 14;
		if (c == NK_DOWN || (c == 'x' && !ctrl)) help_top++;
		else if (c == NK_UP || (c == 'f' && !ctrl)) help_top--;
		else if (c == NK_PGDN || (c == 'm' && !ctrl) || c == ' ') help_top += rows;
		else if (c == NK_PGUP || (c == 'l' && !ctrl)) help_top -= rows;
		else if (c == NK_ESC || c == NK_ENTER || c == NK_HELP || c == 'q') ov = OV_NONE;
		if (help_top > help_n - rows) help_top = help_n - rows;
		if (help_top < 0) help_top = 0;
		redraw();
		return 1;
	}
	if (ov == OV_CONFIRM) {
		if (c == NK_LEFT || c == NK_RIGHT || c == NK_TAB || c == NK_UP || c == NK_DOWN || ((c == 'z' || c == 'c') && !ctrl)) sel = !sel;
		else if (c == 'y' || c == 'Y') { sel = 1; c = NK_ENTER; }
		else if (c == 'n' || c == 'N') { sel = 0; c = NK_ENTER; }
		if (c == NK_ENTER || c == NK_ESC) {
			int yes = c == NK_ENTER && sel == 1;
			ov = OV_NONE;
			if (confirm_done) confirm_done(yes);
		}
		redraw();
		return 1;
	}
	/* menu */
	int rows = menu_rows();
	int dir = 0;
	if (c == NK_DOWN || c == NK_TAB) dir = (k.mods & M_SHIFT) && c == NK_TAB ? -1 : 1;
	else if (c == NK_UP) dir = -1;
	else if (c == NK_PGDN) { sel += rows; if (sel >= nitems) sel = nitems - 1; }
	else if (c == NK_PGUP) { sel -= rows; if (sel < 0) sel = 0; }
	else if (c < 0x80 && c > ' ' && !ctrl) {   /* a letter: the next item that starts with it */
		char lc = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
		for (int g = 1; g <= nitems; g++) {
			int k2 = (sel + g) % nitems;
			char f = items[k2].label[0];
			if (f >= 'A' && f <= 'Z') f += 32;
			if (f == lc && !items[k2].off) { sel = k2; break; }
		}
	}
	else if (c == NK_HOME) sel = 0;
	else if (c == NK_END) sel = nitems - 1;
	else if (c == NK_ESC || c == NK_ALT_TAP) { ov = OV_NONE; if (menu_done) menu_done(-1, -1); redraw(); return 1; }
	else if (c == NK_ENTER && nitems && !items[sel].off) {
		ov = OV_NONE;
		int id = items[sel].id, idx = sel;
		if (menu_done) menu_done(id, idx);
		redraw();
		return 1;
	} else if (c == NK_DEL && menu_done) {   /* pickers use Delete on an entry (Recently Deleted) */
		int id = items[sel].id;
		ov = OV_NONE;
		menu_done(-2 - id, sel);
		redraw();
		return 1;
	}
	if (dir && nitems) {
		for (int g = 0; g < nitems; g++) {
			sel = (sel + dir + nitems) % nitems;
			if (!items[sel].off) break;
		}
	}
	if (sel < top) top = sel;
	if (sel >= top + rows) top = sel - rows + 1;
	redraw();
	return 1;
}

static void panel(int x, int y, int w, int h)
{
	d_rect(x + 2, y + 2, w, h, pal.shadow_c);
	d_rect(x, y, w, h, pal.bg);
	d_frame(x, y, w, h, 1, pal.accent);
}

void overlay_draw(void)
{
	if (toast_msg[0]) {
		int tw = text_w(F_UI, toast_msg, strlen(toast_msg)) + 16;
		if (tw > SCREEN_W - 16) tw = SCREEN_W - 16;
		int x = (SCREEN_W - tw) / 2, y = SCREEN_H - 24;
		d_rrect(x, y, tw, 19, 5, pal.fg);
		d_text_fit(F_UI, x + 8, y + 3, tw - 16, pal.bg, toast_msg);
	}
	if (ov == OV_MENU) {
		int rows = menu_rows(), n = nitems < rows ? nitems : rows;
		int w = 268, h = 22 + n * 15 + 6, x = (SCREEN_W - w) / 2, y = BAR_H + (SCREEN_H - BAR_H - h) / 2;
		panel(x, y, w, h);
		d_rect(x + 1, y + 1, w - 2, 18, pal.accent);
		d_text_fit(F_UIBOLD, x + 6, y + 3, w - 12, pal.bg, ov_title);
		for (int i = 0; i < n; i++) {
			int k = top + i, ry = y + 22 + i * 15;
			struct mitem *m = &items[k];
			int hw = m->hint[0] ? text_w(F_UISMALL, m->hint, strlen(m->hint)) : 0;
			if (k == sel) d_rect(x + 3, ry - 1, w - 6, 15, pal.sel_bg);
			uint32_t fg = m->off ? (k == sel ? pal.sel_dim : pal.dim) : (k == sel ? pal.sel_fg : pal.fg);
			d_text_fit(F_UI, x + 8, ry, w - 22 - hw, fg, m->label);
			if (hw) d_text(F_UISMALL, x + w - 8 - hw, ry + 1, k == sel ? pal.sel_dim : pal.dim, m->hint, strlen(m->hint));
		}
		if (nitems > rows) {   /* scroll position */
			int th = (rows * 15) * rows / nitems, ty = y + 22 + (rows * 15 - th) * top / (nitems - rows);
			d_rect(x + w - 3, ty, 2, th, pal.dim);
		}
	} else if (ov == OV_CONFIRM) {
		int w = 250, h = 74, x = (SCREEN_W - w) / 2, y = 50;
		panel(x, y, w, h);
		/* the question, wrapped on two lines if it must be */
		const char *q = ov_title;
		size_t ql = strlen(q);
		if (text_w(F_UI, q, ql) <= w - 16) d_text(F_UI, x + 8, y + 10, pal.fg, q, ql);
		else {
			size_t cut = ql;
			while (cut > 0 && (q[cut] != ' ' || text_w(F_UI, q, cut) > w - 16)) cut--;
			if (!cut) cut = u8_whole(q, ql / 2);
			d_text(F_UI, x + 8, y + 6, pal.fg, q, cut);
			d_text_fit(F_UI, x + 8, y + 21, w - 16, pal.fg, q + cut + (q[cut] == ' '));
		}
		const char *lbl[2] = { "No", confirm_yes };
		for (int i = 0; i < 2; i++) {
			int bw = 84, bx = x + w / 2 - bw - 6 + i * (bw + 12), by = y + h - 26;
			d_rrect(bx, by, bw, 19, 4, sel == i ? pal.accent : pal.line);
			int tw = text_w(F_UIBOLD, lbl[i], strlen(lbl[i]));
			d_text(F_UIBOLD, bx + (bw - tw) / 2, by + 3, sel == i ? pal.bg : pal.fg, lbl[i], strlen(lbl[i]));
		}
	} else if (ov == OV_HELP) {
		d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, pal.bg);
		const char *t = help_ctx == X_HOME ? "Keys: search" : help_ctx == X_LIST ? "Keys: lists" : help_ctx == X_READ ? "Keys: reading" :
		                help_ctx == X_TRIAGE ? "Keys: What is happening?" : "Keys: picture";
		d_rect(0, BAR_H, SCREEN_W, 17, pal.accent);
		d_text(F_UIBOLD, 6, BAR_H + 2, pal.bg, t, strlen(t));
		const char *g = "Alt: every command";
		d_text(F_UISMALL, SCREEN_W - 6 - text_w(F_UISMALL, g, strlen(g)), BAR_H + 3, pal.bg, g, strlen(g));
		int rows = (SCREEN_H - BAR_H - 22) / 14;
		for (int i = 0; i < rows && help_top + i < help_n; i++) {
			int y = BAR_H + 20 + i * 14;
			d_text_fit(F_UISMALL, 6, y + 1, 128, pal.accent, help_lines[help_top + i][0]);
			d_text_fit(F_UI, 140, y, SCREEN_W - 146, pal.fg, help_lines[help_top + i][1]);
		}
		if (help_n > rows) {
			int area = rows * 14, th = area * rows / help_n, ty = BAR_H + 20 + (area - th) * help_top / (help_n - rows);
			d_rect(SCREEN_W - 3, ty, 2, th, pal.dim);
		}
	}
}

/* text with the ranges hl on a marked background */
void text_marked(int f, int x, int y, int maxw, uint32_t c, uint32_t mark, const char *s, const int (*hl)[2], int nhl)
{
	size_t n = strlen(s);
	for (int i = 0; i < nhl; i++) {
		int a = hl[i][0], b = hl[i][1];
		if (a < 0 || b > (int)n || a >= b) continue;
		int x0 = text_w(f, s, (size_t)a), x1 = text_w(f, s, (size_t)b);
		if (x0 >= maxw) continue;
		if (x1 > maxw) x1 = maxw;
		d_rect(x + x0 - 1, y, x1 - x0 + 1, font_lh(f), mark);
	}
	d_text_fit(f, x, y, maxw, c, s);
}
