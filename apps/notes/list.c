// SPDX-License-Identifier: MIT
// The notes list, search first: typing goes into the search box and only matching notes stay.
// With no search it is grouped like iPhone Notes: Pinned, Today, Previous 7 Days, Previous 30
// Days, then months.
#include "notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { R_HEAD, R_NOTE, R_NEW };
struct row { int kind, note; char head[40]; int hit; };

static struct row *rows;
static int nrows, crows, sel, top;
static char query[128];
static struct hit *hits;
static int nhits, chits;
static char want_file[256];   /* select this note when the list is next built */

static struct { int kind; char file[256], trash[256]; } last_act;   /* for Ctrl+Z */

#define SEARCH_H 20
#define NOTE_H 31
#define HEAD_H 15

static void add_row(int kind, int note, const char *head, int hit)
{
	if (nrows == crows) { crows = crows ? crows * 2 : 64; rows = realloc(rows, (size_t)crows * sizeof(*rows)); }
	struct row *r = &rows[nrows++];
	r->kind = kind; r->note = note; r->hit = hit;
	snprintf(r->head, sizeof(r->head), "%s", head ? head : "");
}

static int by_time(const void *a, const void *b)
{
	const struct note *x = &notes[*(const int *)a], *y = &notes[*(const int *)b];
	return y->mtime > x->mtime ? 1 : y->mtime < x->mtime ? -1 : strcmp(x->file, y->file);
}

static void group_of(time_t t, char *out, size_t n)
{
	time_t now = time(NULL);
	struct tm a, b;
	localtime_r(&now, &a);
	localtime_r(&t, &b);
	struct tm mid = a;
	mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
	time_t today = mktime(&mid);
	if (t >= today) snprintf(out, n, "Today");
	else if (t >= today - 7 * 86400) snprintf(out, n, "Previous 7 Days");
	else if (t >= today - 30 * 86400) snprintf(out, n, "Previous 30 Days");
	else if (a.tm_year == b.tm_year) strftime(out, n, "%B", &b);
	else strftime(out, n, "%Y", &b);
}

static void date_of(time_t t, char *out, size_t n)
{
	time_t now = time(NULL);
	struct tm a, b;
	localtime_r(&now, &a);
	localtime_r(&t, &b);
	if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) strftime(out, n, "%H:%M", &b);
	else if (now - t < 6 * 86400 && now >= t) strftime(out, n, "%a", &b);
	else if (a.tm_year == b.tm_year) strftime(out, n, "%e %b", &b);
	else strftime(out, n, "%e %b %Y", &b);
	if (out[0] == ' ') memmove(out, out + 1, strlen(out));
}

static void build(void)
{
	char keep[256] = "";
	if (want_file[0]) { snprintf(keep, sizeof(keep), "%s", want_file); want_file[0] = 0; }
	else if (sel >= 0 && sel < nrows && rows[sel].kind == R_NOTE && rows[sel].note < nnotes) snprintf(keep, sizeof(keep), "%s", notes[rows[sel].note].file);
	int keep_new = sel >= 0 && sel < nrows && rows[sel].kind == R_NEW;
	nrows = 0;
	if (query[0]) {
		if (chits < nnotes + 1) { chits = nnotes + 16; hits = realloc(hits, (size_t)chits * sizeof(*hits)); }
		nhits = search_run(query, hits, nnotes);
		for (int i = 0; i < nhits; i++) add_row(R_NOTE, hits[i].note, NULL, i);
		add_row(R_NEW, -1, NULL, -1);
	} else {
		int *ord = malloc(sizeof(int) * (size_t)(nnotes + 1));
		for (int i = 0; i < nnotes; i++) ord[i] = i;
		qsort(ord, (size_t)nnotes, sizeof(int), by_time);
		int any_pin = 0;
		for (int i = 0; i < nnotes; i++) if (is_pinned(notes[ord[i]].file)) { if (!any_pin++) add_row(R_HEAD, -1, "Pinned", -1); add_row(R_NOTE, ord[i], NULL, -1); }
		char last[40] = "";
		for (int i = 0; i < nnotes; i++) {
			if (is_pinned(notes[ord[i]].file)) continue;
			char g[40];
			group_of(notes[ord[i]].mtime, g, sizeof(g));
			if (strcmp(g, last)) { add_row(R_HEAD, -1, g, -1); snprintf(last, sizeof(last), "%s", g); }
			add_row(R_NOTE, ord[i], NULL, -1);
		}
		free(ord);
	}
	/* the selection: the same note as before, else the first note */
	sel = -1;
	for (int i = 0; i < nrows && keep[0]; i++)
		if (rows[i].kind == R_NOTE && !strcmp(notes[rows[i].note].file, keep)) { sel = i; break; }
	if (sel < 0 && keep_new) for (int i = 0; i < nrows; i++) if (rows[i].kind == R_NEW) sel = i;
	if (sel < 0) for (int i = 0; i < nrows; i++) if (rows[i].kind != R_HEAD) { sel = i; break; }
	if (top > sel && sel >= 0) top = sel;
}

static int row_h(int i) { return rows[i].kind == R_HEAD ? HEAD_H : rows[i].kind == R_NEW ? 22 : NOTE_H; }
static int area_y(void) { return BAR_H + SEARCH_H; }
static int area_h(void) { return SCREEN_H - area_y(); }

static void show_sel(void)
{
	if (sel < 0) { top = 0; return; }
	/* a group heading right above the selection shows with it */
	int want_top = sel > 0 && rows[sel - 1].kind == R_HEAD ? sel - 1 : sel;
	if (want_top < top) top = want_top;
	for (;;) {
		int h = 0;
		for (int i = top; i <= sel; i++) h += row_h(i);
		if (h <= area_h() || top >= sel) break;
		top++;
	}
	if (top < 0) top = 0;
}

void list_select_file(const char *file)
{
	snprintf(want_file, sizeof(want_file), "%s", file);
	build();
	show_sel();
	redraw();
}

void list_show(void)
{
	screen = SCR_LIST;
	store_scan();
	build();
	show_sel();
	redraw();
}

static void move(int d)
{
	if (sel < 0) return;
	int s = sel;
	for (;;) {
		s += d;
		if (s < 0 || s >= nrows) return;
		if (rows[s].kind != R_HEAD) break;
	}
	sel = s;
	show_sel();
}

static void open_sel(void)
{
	if (sel < 0) { ed_new(query); query[0] = 0; return; }
	if (rows[sel].kind == R_NEW) { ed_new(query); query[0] = 0; return; }
	if (rows[sel].kind != R_NOTE) return;
	char f[256];
	snprintf(f, sizeof(f), "%s", notes[rows[sel].note].file);
	ed_open(f, MODE_VIEW, 0, 0, 0, 0, -1);
	/* a search opens the note at its first match, highlighted */
	if (query[0] && ed_is_open()) { extern void ed_find_preset(const char *q); ed_find_preset(query); }
}

static const char *sel_file(void) { return sel >= 0 && rows[sel].kind == R_NOTE ? notes[rows[sel].note].file : NULL; }

static void trash_sel(void)
{
	const char *f = sel_file();
	if (!f) return;
	last_act.kind = 1;
	snprintf(last_act.file, sizeof(last_act.file), "%s", f);
	if (store_trash(f)) { toast("Could not delete it"); return; }
	snprintf(last_act.trash, sizeof(last_act.trash), "%s", st.trash[st.ntrash - 1].file);
	store_scan();
	build();
	show_sel();
	toast("Moved to Recently Deleted (Ctrl+Z undoes)");
}

static void undo_act(void)
{
	if (last_act.kind == 1) {
		for (int i = st.ntrash - 1; i >= 0; i--)
			if (!strcmp(st.trash[i].file, last_act.trash)) {
				char f[256];
				if (!store_restore(i, f, sizeof(f))) { list_select_file(f); toast("Restored"); }
				break;
			}
	} else if (last_act.kind == 2) {
		set_pinned(last_act.file, !is_pinned(last_act.file));
		state_save();
		build();
		toast("Undone");
	} else toast("Nothing to undo");
	last_act.kind = 0;
}

/* ---- Recently Deleted ---- */

static void trash_done(int id, int idx);
static void purge_done(int yes);
static int purge_idx = -1;

static void trash_open(void)
{
	static struct mitem it[64];
	int n = 0;
	time_t now = time(NULL);
	for (int i = st.ntrash - 1; i >= 0 && n < 64; i--) {
		struct mitem *m = &it[n];
		memset(m, 0, sizeof(*m));
		char p[800], *s, err[64];
		size_t len;
		snprintf(p, sizeof(p), "%s/.Trash/%s", notes_dir, st.trash[i].file);
		char title[128] = "", snip[8];
		if (!store_read_path(p, &s, &len, err, sizeof(err))) { store_note_title(s, len, title, sizeof(title), snip, sizeof(snip)); free(s); }
		snprintf(m->label, sizeof(m->label), "%s", title[0] ? title : st.trash[i].file);
		long left = 30 - (long)((now - st.trash[i].when) / 86400);
		snprintf(m->hint, sizeof(m->hint), "%ld day%s", left < 0 ? 0 : left, left == 1 ? "" : "s");
		m->id = i;
		n++;
	}
	if (!n) { toast("Recently Deleted is empty"); return; }
	menu_open("Recently Deleted", it, n, 0, trash_done);
	toast("Enter restores \u00b7 Fn+Backspace deletes for good");
}

static void trash_done(int id, int idx)
{
	(void)idx;
	if (id == -1) return;
	if (id <= -2) {   /* Delete on an entry: delete it for good, after asking */
		purge_idx = -2 - id;
		confirm_open("Delete it now? This can't be undone.", "Delete", purge_done);
		return;
	}
	char f[256];
	if (store_restore(id, f, sizeof(f))) { toast("Could not restore it"); return; }
	store_scan();
	list_select_file(f);
	toast("Restored");
}

static void purge_done(int yes)
{
	if (yes && purge_idx >= 0) { store_purge(purge_idx); toast("Deleted"); }
	purge_idx = -1;
	trash_open();
}

/* ---- the action menu ---- */

static void menu_done(int id, int idx) { (void)idx; if (id > 0) app_cmd(id); }

static void list_menu(void)
{
	struct mitem it[24];
	int n = 0;
	const char *f = sel_file();
	for (const uint8_t *c = cmd_menu(CTX_LIST); *c && n < 24; c++) {
		struct mitem *m = &it[n++];
		memset(m, 0, sizeof(*m));
		m->id = *c;
		snprintf(m->label, sizeof(m->label), "%s", cmd_name(*c));
		snprintf(m->hint, sizeof(m->hint), "%s", cmd_keys(CTX_LIST, *c));
		if (*c == C_SIZE) snprintf(m->label, sizeof(m->label), "Text size: %s", text_size == 0 ? "Small" : text_size == 1 ? "Medium" : "Large");
		if (*c == C_AUTO_TICKED) snprintf(m->label, sizeof(m->label), "Ticked go to bottom: %s", st.ticked_bottom ? "On" : "Off");
		if (*c == C_PIN && f) snprintf(m->label, sizeof(m->label), "%s", is_pinned(f) ? "Unpin" : "Pin");
		if ((*c == C_OPEN || *c == C_PIN || *c == C_DUPLICATE || *c == C_DELETE_NOTE) && !f) m->off = 1;
		if (*c == C_TRASH) snprintf(m->label, sizeof(m->label), "Recently Deleted (%d)", st.ntrash);
	}
	menu_open("Notes", it, n, 0, menu_done);
}

int list_cmd(int cmd)
{
	switch (cmd) {
	case C_UP: move(-1); break;
	case C_DOWN: move(1); break;
	case C_PGUP: for (int i = 0; i < 4; i++) move(-1); break;
	case C_PGDN: for (int i = 0; i < 4; i++) move(1); break;
	case C_TOP: sel = -1; for (int i = 0; i < nrows; i++) if (rows[i].kind != R_HEAD) { sel = i; break; } top = 0; show_sel(); break;
	case C_BOTTOM: sel = nrows - 1; show_sel(); break;
	case C_OPEN: open_sel(); break;
	case C_NEW: { char q[128]; snprintf(q, sizeof(q), "%s", query); query[0] = 0; ed_new(q); break; }
	case C_BACK:
		if (query[0]) { query[0] = 0; build(); top = 0; show_sel(); }
		else app_quit();
		break;
	case C_DELETE_NOTE: trash_sel(); break;
	case C_PIN: {
		const char *f = sel_file();
		if (!f) break;
		set_pinned(f, !is_pinned(f));
		last_act.kind = 2;
		snprintf(last_act.file, sizeof(last_act.file), "%s", f);
		state_save();
		build();
		show_sel();
		toast(is_pinned(last_act.file) ? "Pinned" : "Unpinned");
		break;
	}
	case C_UNDO: undo_act(); break;
	case C_DUPLICATE: {
		const char *f = sel_file();
		char *s, name[256], err[128];
		size_t n;
		if (!f || store_read(f, &s, &n)) break;
		store_name_for(s, n, NULL, name, sizeof(name));
		if (store_write(name, s, n, err, sizeof(err))) toast(err);
		else { store_scan(); list_select_file(name); toast("Duplicated"); }
		free(s);
		break;
	}
	case C_TRASH: trash_open(); break;
	case C_MENU: list_menu(); break;
	default: return 0;
	}
	redraw();
	return 1;
}

void list_key(struct nkey k)
{
	int shifted;
	int cmd = cmd_lookup(CTX_LIST, k, &shifted);
	if (cmd) { if (!list_cmd(cmd)) app_cmd(cmd); return; }
	if (k.k == NK_BS) {
		size_t l = strlen(query);
		if (l) { l--; while (l && ((unsigned char)query[l] & 0xc0) == 0x80) l--; query[l] = 0; }
		top = 0; build(); show_sel(); redraw();
		return;
	}
	if (k.k < 0x110000 && k.k >= ' ' && !(k.mods & (M_CTRL | M_ALT))) {
		char u[4];
		int n = u8_enc(k.k, u);
		size_t l = strlen(query);
		if (l + (size_t)n < sizeof(query) && !(l == 0 && k.k == ' ')) { memcpy(query + l, u, (size_t)n); query[l + (size_t)n] = 0; }
		top = 0;
		sel = -1;
		build();
		show_sel();
		redraw();
	}
}

/* draw text with the ranges [hl] on a marked background */
static void text_marked(int f, int x, int y, int maxw, uint32_t c, const char *s, const int (*hl)[2], int nhl)
{
	size_t n = strlen(s);
	int w = 0;
	for (int i = 0; i < nhl; i++) {
		int a = hl[i][0], b = hl[i][1];
		if (a < 0 || b > (int)n || a >= b) continue;
		int x0 = text_w(f, s, (size_t)a), x1 = text_w(f, s, (size_t)b);
		if (x0 >= maxw) continue;
		if (x1 > maxw) x1 = maxw;
		d_rect(x + x0 - 1, y, x1 - x0 + 1, font_lh(f), pal.mark_bg);
	}
	(void)w;
	d_text_fit(f, x, y, maxw, c, s);
}

void list_draw(void)
{
	d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, pal.bg);
	/* the search box */
	int sy = BAR_H + 2;
	d_rrect(4, sy, SCREEN_W - 8, SEARCH_H - 4, 5, pal.code_bg);
	int ix = 10;
	d_frame(ix, sy + 3, 8, 8, 1, pal.dim);           /* a small magnifier */
	d_line(ix + 7, sy + 10, ix + 10, sy + 13, 2, pal.dim);
	int qx = ix + 16, maxw = SCREEN_W - qx - 12;
	if (query[0]) {
		const char *q = query;
		while (*q && text_w(F_UI, q, strlen(q)) > maxw) q++;
		while (((unsigned char)*q & 0xc0) == 0x80) q++;
		int qw = d_text(F_UI, qx, sy + 1, pal.fg, q, strlen(q));
		d_rect(qx + qw + 1, sy + 2, 1, 12, pal.accent);
	} else {
		d_rect(qx, sy + 2, 1, 12, pal.accent);
		d_text(F_UI, qx + 4, sy + 1, pal.dim, "Search, or type a new title", 27);
	}
	/* the rows */
	int y = area_y();
	if (!nnotes && !query[0]) {
		const char *a = "No notes yet", *b = "Ctrl+N or just type a title, then Enter";
		d_text(F_UIBOLD, (SCREEN_W - text_w(F_UIBOLD, a, strlen(a))) / 2, y + 36, pal.fg, a, strlen(a));
		d_text(F_UI, (SCREEN_W - text_w(F_UI, b, strlen(b))) / 2, y + 54, pal.dim, b, strlen(b));
		const char *c = "Fn+H keys  ·  Alt menu";
		d_text(F_UISMALL, (SCREEN_W - text_w(F_UISMALL, c, strlen(c))) / 2, y + 76, pal.dim, c, strlen(c));
	}
	for (int i = top; i < nrows && y < SCREEN_H; i++) {
		struct row *r = &rows[i];
		int h = row_h(i);
		if (r->kind == R_HEAD) {
			d_text(F_UIBOLD, 8, y + 2, pal.accent, r->head, strlen(r->head));
			y += h;
			continue;
		}
		int is_sel = i == sel;
		if (is_sel) d_rrect(3, y + 1, SCREEN_W - 9, h - 2, 4, pal.sel_bg);
		uint32_t fg = is_sel ? pal.sel_fg : pal.fg;
		if (r->kind == R_NEW) {
			char t[180];
			snprintf(t, sizeof(t), "+  New note \"%s\"", query);
			d_text_fit(F_UIBOLD, 10, y + 4, SCREEN_W - 24, is_sel ? pal.sel_fg : pal.accent, t);
			y += h;
			continue;
		}
		struct note *n = &notes[r->note];
		char date[32];
		date_of(n->mtime, date, sizeof(date));
		int dw = text_w(F_UISMALL, date, strlen(date));
		int pin = is_pinned(n->file) && query[0];
		const char *title = n->title[0] ? n->title : "New note";
		if (r->hit >= 0 && hits[r->hit].title_hit) {   /* mark the words in the title too */
			int hl[6][2], nhl = 0;
			char ft[128], q[128];
			size_t tl = strlen(title);
			fold_text(title, tl, ft);
			fold_text(query, strlen(query), q);
			for (char *w = strtok(q, " "); w && nhl < 6; w = strtok(NULL, " ")) {
				char *m = strstr(ft, w);
				if (m) { hl[nhl][0] = (int)(m - ft); hl[nhl][1] = hl[nhl][0] + (int)strlen(w); nhl++; }
			}
			text_marked(F_UIBOLD, 10, y + 2, SCREEN_W - 28 - dw, fg, title, (const int (*)[2])hl, nhl);
		} else d_text_fit(F_UIBOLD, 10 + (pin ? 10 : 0), y + 2, SCREEN_W - 28 - dw - (pin ? 10 : 0), fg, title);
		d_text(F_UISMALL, SCREEN_W - 12 - dw, y + 3, pal.dim, date, strlen(date));
		if (r->hit >= 0) text_marked(F_UI, 10, y + 16, SCREEN_W - 24, pal.dim, hits[r->hit].snip, (const int (*)[2])hits[r->hit].hl, hits[r->hit].nhl);
		else d_text_fit(F_UI, 10, y + 16, SCREEN_W - 24, pal.dim, n->snippet[0] ? n->snippet : "No additional text");
		if (!is_sel && i + 1 < nrows && rows[i + 1].kind == R_NOTE) d_rect(10, y + h - 1, SCREEN_W - 20, 1, pal.line);
		y += h;
	}
	char title[48];
	if (query[0]) snprintf(title, sizeof(title), "Notes · %d found", nhits);
	else snprintf(title, sizeof(title), "Notes · %d", nnotes);
	bar_draw(title, "", pal.dim);
}
