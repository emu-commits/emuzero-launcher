// SPDX-License-Identifier: MIT
// A note on screen: View mode (the Markdown drawn; plain letters move a highlight from line to
// line, Space ticks) and Edit mode (typing, with the cursor). Every change is saved about 1.5 s
// after the last key, when the note is left, and when the app is stopped.
#define _GNU_SOURCE
#include "notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SAVE_DELAY 1500
#define BLINK_FOR 10000

static struct {
	struct doc d;
	int open;
	char file[256];            /* in the notes folder; "" until a new note is first saved */
	char path[700];            /* a file outside the folder (emuzero-notes FILE, the guide) */
	int external, readonly;
	int mode;
	size_t cur, anc;
	int gx;                    /* the column Up/Down keep, -1 for none */
	int top_para, top_row;
	int focus;                 /* View mode's highlighted line */
	int finding;
	char fq[128];
	size_t *marks; int nmarks, cmarks, mark_i;
	int save_err;
	char save_msg[160];
	time_t disk_mtime; off_t disk_size; int on_disk;
	int history_done;
	int crlf, bom;
	lv_timer_t *save_timer, *blink_timer;
	int blink_on;
	uint32_t last_key;
	size_t selstack[8][2]; int nsel;
	char title[128];
} E;

static int text_x(void) { return MARGIN; }
static int text_y(void) { return BAR_H + 3 + (E.finding ? 18 : 0); }
static int text_wd(void) { return SCREEN_W - 2 * MARGIN - 3; }
static int text_h(void) { return SCREEN_H - text_y() - 1; }

int ed_is_open(void) { return E.open; }

static struct linfo pli(int i)
{
	size_t n;
	const char *s = doc_para_text(&E.d, i, &n);
	struct linfo li;
	line_info(s, n, E.d.p[i].code, &li);
	return li;
}

static size_t pend(int i) { return E.d.p[i].start + E.d.p[i].len; }
static int has_sel(void) { return E.cur != E.anc; }
static size_t sel0(void) { return E.cur < E.anc ? E.cur : E.anc; }
static size_t sel1(void) { return E.cur < E.anc ? E.anc : E.cur; }
static size_t len(void) { return tb_len(&E.d.tb); }

/* ---- saving ---- */

static void update_title(void)
{
	size_t n = len() < 2048 ? len() : 2048;
	char *s = tb_copy(&E.d.tb, 0, n), snip[8];
	store_note_title(s, n, E.title, sizeof(E.title), snip, sizeof(snip));
	free(s);
}

static void save_cb(lv_timer_t *t) { lv_timer_pause(t); ed_save(); redraw(); }

static int blank_text(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\n' && s[i] != '\t' && s[i] != '\r') return 0;
	return 1;
}

static char *with_endings(const char *s, size_t n, size_t *out)
{
	size_t extra = E.bom ? 3 : 0;
	if (E.crlf) for (size_t i = 0; i < n; i++) extra += s[i] == '\n';
	char *b = malloc(n + extra + 1), *o = b;
	if (E.bom) { memcpy(o, "\xef\xbb\xbf", 3); o += 3; }
	for (size_t i = 0; i < n; i++) { if (E.crlf && s[i] == '\n') *o++ = '\r'; *o++ = s[i]; }
	*out = (size_t)(o - b);
	return b;
}

int ed_save(void)
{
	if (!E.open || !E.d.dirty || E.readonly) return 0;
	size_t n;
	char *s = doc_text(&E.d, &n);
	if (!E.external && !E.file[0] && blank_text(s, n)) { free(s); E.d.dirty = 0; return 0; }   /* nothing typed yet */
	char path[800], err[128];
	if (E.external) snprintf(path, sizeof(path), "%s", E.path);
	else {
		if (!E.file[0]) store_name_for(s, n, NULL, E.file, sizeof(E.file));
		path_note(path, sizeof(path), E.file);
	}
	/* changed by something else since we read it (SSH, a PC): keep that copy too */
	time_t mt; off_t sz;
	if (E.on_disk && !store_stat(path, &mt, &sz) && (mt != E.disk_mtime || sz != E.disk_size)) {
		if (!E.external) {
			char other[256], stem[220], probe[300];
			snprintf(stem, sizeof(stem), "%.*s", (int)(strlen(E.file) - 3), E.file);
			for (int k = 1; k < 100; k++) {
				if (k == 1) snprintf(other, sizeof(other), "%s (other copy).md", stem);
				else snprintf(other, sizeof(other), "%s (other copy %d).md", stem, k);
				path_note(probe, sizeof(probe), other);
				if (access(probe, F_OK)) break;
			}
			if (!store_rename(E.file, other)) {
				char m[320];
				snprintf(m, sizeof(m), "Changed elsewhere: kept that as \"%s\"", other);
				toast(m);
			}
		}
	}
	if (!E.history_done && E.on_disk && !E.external) { store_history(E.file); E.history_done = 1; }
	size_t on;
	char *b = with_endings(s, n, &on);
	free(s);
	int r = store_write_path(path, b, on, err, sizeof(err));
	free(b);
	if (r) {
		E.save_err = 1;
		snprintf(E.save_msg, sizeof(E.save_msg), "Not saved: %s", err);
		return -1;
	}
	E.save_err = 0;
	E.d.dirty = 0;
	E.on_disk = 1;
	store_stat(path, &E.disk_mtime, &E.disk_size);
	return 0;
}

static void edited(void)
{
	E.gx = -1;
	if (!E.save_timer) E.save_timer = lv_timer_create(save_cb, SAVE_DELAY, NULL);
	lv_timer_reset(E.save_timer);
	lv_timer_resume(E.save_timer);
	update_title();
}

/* ---- opening and closing ---- */

static void blink_cb(lv_timer_t *t)
{
	if (now_ms() - E.last_key > BLINK_FOR) { E.blink_on = 1; lv_timer_pause(t); }
	else E.blink_on = !E.blink_on;
	redraw();
}

static void wake_cursor(void)
{
	E.last_key = now_ms();
	E.blink_on = 1;
	if (!E.blink_timer) E.blink_timer = lv_timer_create(blink_cb, 530, NULL);
	lv_timer_reset(E.blink_timer);
	lv_timer_resume(E.blink_timer);
}

static void reset_state(void)
{
	if (E.save_timer) lv_timer_pause(E.save_timer);
	doc_free(&E.d);
	free(E.marks);
	lv_timer_t *st1 = E.save_timer, *bt = E.blink_timer;
	memset(&E, 0, sizeof(E));
	E.save_timer = st1; E.blink_timer = bt;
	E.gx = -1;
	E.focus = -1;
	doc_init(&E.d);
}

static void load_text(char *s, size_t n)
{
	/* a byte-order mark and CRLF line ends are kept for the save, not shown */
	char *p = s;
	if (n >= 3 && !memcmp(p, "\xef\xbb\xbf", 3)) { E.bom = 1; p += 3; n -= 3; }
	if (memchr(p, '\r', n)) {
		size_t o = 0;
		for (size_t i = 0; i < n; i++) {
			if (p[i] == '\r' && i + 1 < n && p[i + 1] == '\n') { E.crlf = 1; continue; }
			p[o++] = p[i];
		}
		n = o;
	}
	doc_load(&E.d, p, n);
}

static void clamp_view(void)
{
	if (E.cur > len()) E.cur = len();
	if (E.anc > len()) E.anc = E.cur;
	if (E.top_para >= E.d.np) { E.top_para = E.d.np - 1; E.top_row = 0; }
	if (E.top_para < 0) E.top_para = 0;
	if (E.focus >= E.d.np) E.focus = E.d.np - 1;
}

static void ensure_visible(void);

void ed_open(const char *file, int mode, size_t cur, size_t anc, int top_para, int top_row, int focus)
{
	reset_state();
	char *s, path[800], err[128];
	size_t n;
	path_note(path, sizeof(path), file);
	if (store_read_path(path, &s, &n, err, sizeof(err))) {
		char m[420];
		snprintf(m, sizeof(m), "Can't open %s: %s", file, err);
		toast(m);
		return;
	}
	load_text(s, n);
	free(s);
	snprintf(E.file, sizeof(E.file), "%s", file);
	E.on_disk = 1;
	store_stat(path, &E.disk_mtime, &E.disk_size);
	E.open = 1;
	E.mode = mode;
	E.cur = cur; E.anc = anc;
	E.top_para = top_para; E.top_row = top_row;
	E.focus = focus;
	clamp_view();
	E.cur = view_snap(&E.d, E.cur, 1);
	if (!has_sel()) E.anc = E.cur;
	if (E.mode == MODE_VIEW && E.focus < 0) E.focus = E.top_para;
	update_title();
	view_invalidate_all();
	ensure_visible();
	wake_cursor();
	screen = SCR_NOTE;
	redraw();
}

void ed_open_path(const char *path, int readonly)
{
	reset_state();
	char *s, err[128];
	size_t n;
	if (store_read_path(path, &s, &n, err, sizeof(err))) {
		if (readonly || access(path, F_OK) == 0) {
			char m[160];
			snprintf(m, sizeof(m), "Can't open it: %s", err);
			toast(m);
			if (plain_mode) app_quit();
			return;
		}
		s = strdup(""); n = 0;   /* a new file: made at the first save */
	} else E.on_disk = 1;
	load_text(s, n);
	free(s);
	snprintf(E.path, sizeof(E.path), "%s", path);
	E.external = 1;
	E.readonly = readonly;
	if (E.on_disk) store_stat(path, &E.disk_mtime, &E.disk_size);
	E.open = 1;
	E.mode = readonly ? MODE_VIEW : MODE_EDIT;
	E.focus = readonly ? 0 : -1;
	update_title();
	if (!readonly) {
		const char *b = strrchr(path, '/');
		snprintf(E.title, sizeof(E.title), "%s", b ? b + 1 : path);
	}
	view_invalidate_all();
	wake_cursor();
	screen = SCR_NOTE;
	redraw();
}

void ed_new(const char *title)
{
	reset_state();
	E.open = 1;
	E.mode = MODE_EDIT;
	if (title && *title) {
		doc_edit(&E.d, 0, 0, title, strlen(title), 0, now_ms());
		doc_edit(&E.d, len(), 0, "\n", 1, 0, now_ms());
		E.cur = E.anc = len();
		E.d.u[0].group = E.d.u[1].group;   /* one undo step */
		edited();
	}
	update_title();
	view_invalidate_all();
	wake_cursor();
	screen = SCR_NOTE;
	redraw();
}

void ed_remember(void)
{
	if (!E.open || E.external) { st.last[0] = 0; return; }
	snprintf(st.last, sizeof(st.last), "%s", E.file);
	st.cur = E.cur; st.anc = E.anc;
	st.mode = E.mode;
	st.top_para = E.top_para; st.top_row = E.top_row;
	st.focus = E.focus;
}

void ed_close(void)
{
	if (!E.open) return;
	ed_save();
	char keep[256] = "";
	if (!E.external && E.file[0]) {
		size_t n;
		char *s = doc_text(&E.d, &n);
		if (blank_text(s, n)) {   /* an empty note is dropped, as on the iPhone */
			char p[800];
			path_note(p, sizeof(p), E.file);
			unlink(p);
			set_pinned(E.file, 0);
		} else if (!E.save_err) {
			char want[256];
			store_name_for(s, n, E.file, want, sizeof(want));
			if (strcmp(want, E.file) && !store_rename(E.file, want)) {
				int pinned = is_pinned(E.file);
				set_pinned(E.file, 0);
				if (pinned) set_pinned(want, 1);
				snprintf(E.file, sizeof(E.file), "%s", want);
			}
			snprintf(keep, sizeof(keep), "%s", E.file);
		}
		free(s);
	}
	if (E.save_err) {   /* stay: closing would lose the text */
		toast(E.save_msg);
		return;
	}
	if (E.save_timer) lv_timer_pause(E.save_timer);
	if (E.blink_timer) lv_timer_pause(E.blink_timer);
	E.open = 0;
	st.last[0] = 0;
	state_save();
	if (plain_mode) { app_quit(); return; }
	list_show();
	if (keep[0]) list_select_file(keep);
}

/* ---- scrolling ---- */

static int row_before(int *p, int *r)
{
	if (*r > 0) { (*r)--; return 1; }
	if (*p == 0) return 0;
	(*p)--;
	*r = view_nrows(&E.d, *p, E.mode, text_wd(), E.external) - 1;
	return 1;
}

static int row_after(int *p, int *r)
{
	if (*r + 1 < view_nrows(&E.d, *p, E.mode, text_wd(), E.external)) { (*r)++; return 1; }
	if (*p + 1 >= E.d.np) return 0;
	(*p)++;
	*r = 0;
	return 1;
}

static int rh(int p, int r) { return view_row_h(&E.d, p, r, E.mode, text_wd(), E.external); }

/* scroll so that rows [p0,r0]..[p1,r1] show (the end wins when they do not fit) */
static void show_rows(int p0, int r0, int p1, int r1)
{
	if (p0 < E.top_para || (p0 == E.top_para && r0 < E.top_row)) { E.top_para = p0; E.top_row = r0; }
	/* height from the top row down to the end of (p1, r1) */
	int h = 0, p = E.top_para, r = E.top_row, limit = text_h();
	for (;;) {
		h += rh(p, r);
		if (p == p1 && r == r1) break;
		if (!row_after(&p, &r)) break;
		if (h > limit * 4) { E.top_para = p1; E.top_row = r1; h = rh(p1, r1); break; }
	}
	while (h > limit && (E.top_para < p1 || (E.top_para == p1 && E.top_row < r1))) {
		h -= rh(E.top_para, E.top_row);
		row_after(&E.top_para, &E.top_row);
	}
}

static void ensure_visible(void)
{
	if (!E.open) return;
	clamp_view();
	if (E.mode == MODE_EDIT) {
		int p, r, x;
		view_pos(&E.d, E.mode, text_wd(), E.external, E.cur, &p, &r, &x);
		show_rows(p, r, p, r);
	} else if (E.focus >= 0) {
		int n = view_nrows(&E.d, E.focus, E.mode, text_wd(), E.external);
		show_rows(E.focus, 0, E.focus, n - 1);
		if (E.top_para > E.focus) { E.top_para = E.focus; E.top_row = 0; }
	}
}

/* ---- moving ---- */

static size_t word_left(size_t p)
{
	while (p > 0 && strchr(" \n\t", tb_at(&E.d.tb, p - 1))) p = tb_prev(&E.d.tb, p);
	while (p > 0 && !strchr(" \n\t", tb_at(&E.d.tb, p - 1))) p = tb_prev(&E.d.tb, p);
	return p;
}

static size_t word_right(size_t p)
{
	size_t n = len();
	while (p < n && strchr(" \n\t", tb_at(&E.d.tb, p))) p = tb_next(&E.d.tb, p);
	while (p < n && !strchr(" \n\t", tb_at(&E.d.tb, p))) p = tb_next(&E.d.tb, p);
	return p;
}

static void vmove(int rows)
{
	int p, r, x;
	view_pos(&E.d, E.mode, text_wd(), E.external, E.cur, &p, &r, &x);
	if (E.gx < 0) E.gx = x;
	int moved = 0;
	for (int k = 0; k < (rows < 0 ? -rows : rows); k++) {
		if (rows < 0 ? row_before(&p, &r) : row_after(&p, &r)) moved++;
		else break;
	}
	if (!moved) { E.cur = rows < 0 ? 0 : len(); return; }
	int gx = E.gx;
	E.cur = view_hit(&E.d, E.mode, text_wd(), E.external, p, r, gx);
	E.gx = gx;
}

static int page_rows(void) { int l = font_lh(F_REG); return (text_h() / (l ? l : 14)) - 1; }

static void line_home(void)
{
	int p, r, x;
	view_pos(&E.d, E.mode, text_wd(), E.external, E.cur, &p, &r, &x);
	const struct vrow *rows;
	view_rows(&E.d, p, E.mode, text_wd(), E.external, &rows);
	size_t row_start = view_snap(&E.d, E.d.p[p].start + rows[r].start, 1);
	struct linfo li = pli(p);
	size_t text_start = E.d.p[p].start + (size_t)(li.lead + (li.kind == LK_TASK || li.kind == LK_BULLET || li.kind == LK_NUM || li.kind == LK_QUOTE ? li.mlen : 0));
	if (E.cur != row_start) E.cur = row_start;
	else if (E.cur != text_start && r == 0) E.cur = E.cur == text_start ? E.d.p[p].start : text_start;
	else E.cur = E.d.p[p].start;
	E.cur = view_snap(&E.d, E.cur, 1);
}

static void line_end(void)
{
	int p, r, x;
	view_pos(&E.d, E.mode, text_wd(), E.external, E.cur, &p, &r, &x);
	const struct vrow *rows;
	int n = view_rows(&E.d, p, E.mode, text_wd(), E.external, &rows);
	size_t row_end = r + 1 < n ? E.d.p[p].start + rows[r + 1].start : pend(p);
	if (r + 1 < n && row_end > E.d.p[p].start && tb_at(&E.d.tb, row_end - 1) == ' ') row_end--;
	E.cur = E.cur == row_end ? pend(p) : row_end;
}

/* ---- editing helpers ---- */

static size_t adj(size_t pos, size_t at, size_t del, size_t ins)
{
	if (pos <= at) return pos;
	if (pos < at + del) return at + ins;
	return pos - del + ins;
}

/* an edit that keeps the cursor and the selection in place around it */
static void edit_keep(size_t at, size_t del, const char *s, size_t n)
{
	doc_edit(&E.d, at, del, s, n, 0, now_ms());
	E.cur = adj(E.cur, at, del, n);
	E.anc = adj(E.anc, at, del, n);
}

static void replace_sel(const char *s, size_t n, int merge)
{
	size_t a = sel0(), b = sel1();
	doc_edit(&E.d, a, b - a, s, n, merge && a == b, now_ms());
	E.cur = E.anc = a + n;
	E.nsel = 0;
	edited();
}

static int ro_guard(void)
{
	if (!E.readonly) return 0;
	toast("Read-only: Alt \u2192 Duplicate to edit a copy");
	return 1;
}

static void sel_lines(int *a, int *b)
{
	*a = doc_para_at(&E.d, sel0());
	*b = doc_para_at(&E.d, sel1());
	if (*b > *a && sel1() == E.d.p[*b].start && has_sel()) (*b)--;   /* a selection ending at a line's start leaves that line out */
}

static int is_list(int kind) { return kind == LK_TASK || kind == LK_BULLET || kind == LK_NUM || kind == LK_QUOTE; }

static void type_char(uint32_t c)
{
	if (ro_guard()) return;
	char u[4];
	int n = u8_enc(c, u);
	replace_sel(u, (size_t)n, 1);
	/* "[] " or "- [] " at the start of a line becomes a checklist item */
	if (c == ' ') {
		int i = doc_para_at(&E.d, E.cur);
		size_t ps = E.d.p[i].start, k = ps;
		while (k < E.cur && tb_at(&E.d.tb, k) == ' ') k++;
		size_t rest = E.cur - k;
		char t[8] = "";
		for (size_t j = 0; j < rest && j < 6; j++) t[j] = tb_at(&E.d.tb, k + j);
		if ((rest == 3 && !memcmp(t, "[] ", 3)) || (rest == 5 && !memcmp(t, "- [] ", 5))) {
			doc_batch(&E.d, 1);
			edit_keep(k, rest, "- [ ] ", 6);
			doc_batch(&E.d, 0);
			E.cur = E.anc = k + 6;
			edited();
		}
	}
}

static void do_enter(void)
{
	if (ro_guard()) return;
	if (has_sel()) replace_sel("", 0, 0);
	int i = doc_para_at(&E.d, E.cur);
	struct linfo li = pli(i);
	size_t ps = E.d.p[i].start, mend = ps + (size_t)(li.lead + li.mlen);
	char ins[64];
	size_t n = 0;
	if (is_list(li.kind) && E.cur >= mend) {
		if (pend(i) == mend || (pend(i) == mend + 0)) {
			/* Enter on an empty item ends the list */
			doc_edit(&E.d, ps, mend - ps, "", 0, 0, now_ms());
			E.cur = E.anc = ps;
			edited();
			return;
		}
		ins[n++] = '\n';
		for (int k = 0; k < li.lead && n < 40; k++) ins[n++] = ' ';
		if (li.kind == LK_TASK) { memcpy(ins + n, "- [ ] ", 6); n += 6; }
		else if (li.kind == LK_BULLET) { ins[n++] = tb_at(&E.d.tb, ps + (size_t)li.lead); ins[n++] = ' '; }
		else if (li.kind == LK_NUM) {
			char delim = tb_at(&E.d.tb, ps + (size_t)li.lead + (size_t)li.mlen - 2);
			if (delim != '.' && delim != ')') delim = tb_at(&E.d.tb, ps + (size_t)li.lead + (size_t)li.mlen - 1);
			n += (size_t)snprintf(ins + n, sizeof(ins) - n, "%d%c ", li.num + 1, delim == ')' ? ')' : '.');
		}
		else { memcpy(ins + n, "> ", 2); n += 2; }
	} else {
		ins[n++] = '\n';
		if (li.kind == LK_CODE) for (int k = 0; k < li.lead && n < 40; k++) ins[n++] = ' ';
	}
	doc_edit(&E.d, E.cur, 0, ins, n, 0, now_ms());
	E.cur = E.anc = E.cur + n;
	edited();
}

static void do_bs(int word)
{
	if (ro_guard()) return;
	if (has_sel()) { replace_sel("", 0, 0); return; }
	if (!E.cur) return;
	int i = doc_para_at(&E.d, E.cur);
	struct linfo li = pli(i);
	size_t ps = E.d.p[i].start, mend = ps + (size_t)(li.lead + li.mlen);
	if (is_list(li.kind) && E.cur == mend) {   /* Backspace at an item's start: plain text again */
		doc_edit(&E.d, ps + (size_t)li.lead, (size_t)li.mlen, "", 0, 0, now_ms());
		E.cur = E.anc = ps + (size_t)li.lead;
		edited();
		return;
	}
	size_t from = word ? word_left(E.cur) : tb_prev(&E.d.tb, E.cur);
	/* stepping back into the previous line's checkbox marks: stop at its end */
	doc_edit(&E.d, from, E.cur - from, "", 0, !word, now_ms());
	E.cur = E.anc = view_snap(&E.d, from, 1);
	edited();
}

static void do_del(int word)
{
	if (ro_guard()) return;
	if (has_sel()) { replace_sel("", 0, 0); return; }
	if (E.cur >= len()) return;
	size_t to = word ? word_right(E.cur) : tb_next(&E.d.tb, E.cur);
	doc_edit(&E.d, E.cur, to - E.cur, "", 0, !word, now_ms());
	E.anc = E.cur;
	edited();
}

/* Ctrl+L: lines into checklist items, or back */
static void do_checklist(void)
{
	if (ro_guard()) return;
	int a, b;
	sel_lines(&a, &b);
	int all = 1;
	for (int i = a; i <= b; i++) { struct linfo li = pli(i); if (li.kind != LK_TASK && (li.kind != LK_BLANK || a == b)) all = 0; }
	doc_batch(&E.d, 1);
	for (int i = b; i >= a; i--) {
		struct linfo li = pli(i);
		size_t ps = E.d.p[i].start + (size_t)li.lead;
		if (all) { if (li.kind == LK_TASK) edit_keep(ps, (size_t)li.mlen, "", 0); continue; }
		if (li.kind == LK_TASK || (li.kind == LK_BLANK && a != b) || li.kind == LK_CODE || li.kind == LK_FENCE || li.kind == LK_RULE) continue;
		size_t drop = li.kind == LK_BULLET || li.kind == LK_NUM || li.kind == LK_HEAD || li.kind == LK_QUOTE ? (size_t)li.mlen : 0;
		int at_line_start = E.cur == ps && !has_sel();
		edit_keep(ps, drop, "- [ ] ", 6);
		if (at_line_start || (li.kind == LK_BLANK)) E.cur = E.anc = ps + 6;
	}
	doc_batch(&E.d, 0);
	E.cur = view_snap(&E.d, E.cur, 1);
	if (!has_sel()) E.anc = E.cur;
	edited();
}

static void tick_bottom_block(int i);

static int tick_line(int i)
{
	struct linfo li = pli(i);
	if (li.kind != LK_TASK) return 0;
	size_t at = E.d.p[i].start + (size_t)li.lead + 3;
	edit_keep(at, 1, li.checked ? " " : "x", 1);
	return 1;
}

static void do_tick(void)
{
	if (ro_guard()) return;
	int a, b, any = 0;
	if (E.mode == MODE_VIEW) a = b = E.focus;
	else sel_lines(&a, &b);
	if (a < 0) return;
	doc_batch(&E.d, 1);
	for (int i = a; i <= b; i++) any |= tick_line(i);
	if (any && st.ticked_bottom) tick_bottom_block(a);
	doc_batch(&E.d, 0);
	if (!any) { toast("Not a checklist item: Ctrl+L makes one"); return; }
	edited();
}

/* ticked items to the bottom of the checklist around line i (i < 0: every checklist) */
static void tick_bottom_block(int i)
{
	int from = 0, to = E.d.np - 1;
	if (i >= 0) {
		if (pli(i).kind != LK_TASK) return;
		from = to = i;
		while (from > 0 && pli(from - 1).kind == LK_TASK) from--;
		while (to + 1 < E.d.np && pli(to + 1).kind == LK_TASK) to++;
	}
	int focus_line = E.mode == MODE_VIEW ? E.focus : -1;
	for (int s = from; s <= to; s++) {
		if (pli(s).kind != LK_TASK) continue;
		int e = s;
		while (e + 1 <= to && pli(e + 1).kind == LK_TASK) e++;
		/* lines s..e: unticked first, then ticked, each in their order */
		size_t a = E.d.p[s].start, bnd = pend(e);
		char *out = malloc(bnd - a + 2);
		size_t o = 0;
		for (int pass = 0; pass < 2; pass++)
			for (int k = s; k <= e; k++) {
				if (pli(k).checked != pass) continue;
				size_t n;
				const char *t = doc_para_text(&E.d, k, &n);
				memcpy(out + o, t, n); o += n;
				out[o++] = '\n';
			}
		o--;   /* no line break after the last line */
		char *old = tb_copy(&E.d.tb, a, bnd);
		if (o != bnd - a || memcmp(old, out, o)) {
			size_t keep_cur = E.cur, keep_anc = E.anc;
			doc_edit(&E.d, a, bnd - a, out, o, 0, now_ms());
			E.cur = keep_cur > len() ? len() : keep_cur;
			E.anc = keep_anc > len() ? len() : keep_anc;
		}
		free(old); free(out);
		s = e;
	}
	if (focus_line >= 0) E.focus = focus_line < E.d.np ? focus_line : E.d.np - 1;
	E.cur = view_snap(&E.d, E.cur, 1);
}

static void do_ticked_bottom(void)
{
	if (ro_guard()) return;
	doc_batch(&E.d, 1);
	unsigned before = E.d.changes;
	tick_bottom_block(-1);
	doc_batch(&E.d, 0);
	if (E.d.changes == before) { toast("Nothing to move"); return; }
	edited();
}

/* Alt+Up / Alt+Down: the line (or the selected lines) swap with the one above / below */
static void do_move(int dir)
{
	if (ro_guard()) return;
	int a, b;
	sel_lines(&a, &b);
	if ((dir < 0 && a == 0) || (dir > 0 && b + 1 >= E.d.np)) return;
	int oa = dir < 0 ? a - 1 : a, ob = dir < 0 ? b : b + 1;
	size_t start = E.d.p[oa].start, end = pend(ob);
	size_t blk_s = E.d.p[a].start, blk_e = pend(b);
	char *blk = tb_copy(&E.d.tb, blk_s, blk_e);
	int other = dir < 0 ? a - 1 : b + 1;
	size_t on;
	char *oth = tb_copy(&E.d.tb, E.d.p[other].start, pend(other));
	on = strlen(oth);
	size_t bn = blk_e - blk_s;
	char *out = malloc(bn + on + 2);
	size_t cur_off = E.cur - blk_s, anc_off = E.anc - blk_s, base;
	if (dir < 0) { memcpy(out, blk, bn); out[bn] = '\n'; memcpy(out + bn + 1, oth, on); base = start; }
	else { memcpy(out, oth, on); out[on] = '\n'; memcpy(out + on + 1, blk, bn); base = start + on + 1; }
	doc_edit(&E.d, start, end - start, out, bn + on + 1, 0, now_ms());
	E.cur = base + cur_off; E.anc = base + anc_off;
	free(blk); free(oth); free(out);
	edited();
}

static void do_indent(int out)
{
	if (ro_guard()) return;
	int a, b;
	sel_lines(&a, &b);
	struct linfo li = pli(a);
	if (!out && a == b && !has_sel() && !is_list(li.kind)) { replace_sel("  ", 2, 0); return; }
	doc_batch(&E.d, 1);
	for (int i = b; i >= a; i--) {
		size_t ps = E.d.p[i].start;
		if (!out) edit_keep(ps, 0, "  ", 2);
		else {
			size_t k = 0;
			while (k < 2 && k < E.d.p[i].len && tb_at(&E.d.tb, ps + k) == ' ') k++;
			if (k) edit_keep(ps, k, "", 0);
		}
	}
	doc_batch(&E.d, 0);
	edited();
}

static void do_heading(void)
{
	if (ro_guard()) return;
	int i = doc_para_at(&E.d, E.cur);
	struct linfo li = pli(i);
	if (li.kind != LK_TEXT && li.kind != LK_HEAD && li.kind != LK_BLANK) { toast("Headings are for plain lines"); return; }
	int lvl = li.kind == LK_HEAD ? li.level : 0;
	static const char *marks[4] = { "", "# ", "## ", "### " };
	int next = lvl >= 3 ? 0 : lvl + 1;
	size_t ps = E.d.p[i].start + (size_t)li.lead;
	size_t had = li.kind == LK_HEAD ? (size_t)li.mlen : 0;
	edit_keep(ps, had, marks[next], strlen(marks[next]));
	if (E.cur < ps + strlen(marks[next])) E.cur = E.anc = ps + strlen(marks[next]);
	edited();
	static const char *names[4] = { "Body text", "Heading 1", "Heading 2", "Heading 3" };
	toast(names[next]);
}

static void do_wrap(const char *m)
{
	if (ro_guard()) return;
	size_t ml = strlen(m), a = sel0(), b = sel1();
	if (a == b) {   /* nothing selected: the marks, the cursor between them */
		char t[8];
		snprintf(t, sizeof(t), "%s%s", m, m);
		doc_edit(&E.d, a, 0, t, 2 * ml, 0, now_ms());
		E.cur = E.anc = a + ml;
		edited();
		return;
	}
	int wrapped = a >= ml && b + ml <= len();
	for (size_t k = 0; k < ml && wrapped; k++)
		wrapped = tb_at(&E.d.tb, a - ml + k) == m[k] && tb_at(&E.d.tb, b + k) == m[k];
	doc_batch(&E.d, 1);
	if (wrapped) {
		doc_edit(&E.d, b, ml, "", 0, 0, now_ms());
		doc_edit(&E.d, a - ml, ml, "", 0, 0, now_ms());
		E.anc = a - ml; E.cur = b - ml;
	} else {
		doc_edit(&E.d, b, 0, m, ml, 0, now_ms());
		doc_edit(&E.d, a, 0, m, ml, 0, now_ms());
		E.anc = a + ml; E.cur = b + ml;
	}
	doc_batch(&E.d, 0);
	edited();
}

/* ---- selecting, copy and paste ---- */

static void sel_more(void)
{
	size_t a = sel0(), b = sel1(), n = len();
	if (E.nsel < 8) { E.selstack[E.nsel][0] = E.anc; E.selstack[E.nsel][1] = E.cur; E.nsel++; }
	int i = doc_para_at(&E.d, a), j = doc_para_at(&E.d, b);
	/* the word under the cursor */
	size_t ws = a, we = a;
	while (ws > 0 && !strchr(" \n\t", tb_at(&E.d.tb, ws - 1))) ws--;
	while (we < n && !strchr(" \n\t", tb_at(&E.d.tb, we))) we++;
	struct linfo li = pli(i);
	size_t ls = E.d.p[i].start + (size_t)(is_list(li.kind) || li.kind == LK_HEAD ? li.lead + li.mlen : li.lead), le = pend(j);
	/* the block: lines with text around it */
	int bs = i, be = j;
	while (bs > 0 && pli(bs - 1).kind != LK_BLANK) bs--;
	while (be + 1 < E.d.np && pli(be + 1).kind != LK_BLANK) be++;
	size_t ks = E.d.p[bs].start, ke = pend(be);
	if (a == b && we > ws) { E.anc = ws; E.cur = we; }
	else if (i == j && (a > ls || b < le) && le > ls) { E.anc = ls; E.cur = le; }
	else if (a > ks || b < ke) { E.anc = ks; E.cur = ke; }
	else { E.anc = 0; E.cur = n; }
	if (sel0() == a && sel1() == b) { E.anc = 0; E.cur = n; }
}

static int line_clip;   /* the last copy was a whole line (nothing selected) */

static void do_copy(int cut)
{
	char *s;
	size_t a, b;
	if (E.mode == MODE_VIEW) {
		if (E.focus < 0) return;
		a = E.d.p[E.focus].start; b = pend(E.focus);
		s = tb_copy(&E.d.tb, a, b);
		clip_set(s, b - a);
		free(s);
		toast("Copied the line");
		return;
	}
	if (has_sel()) { a = sel0(); b = sel1(); line_clip = 0; }
	else {   /* nothing selected: the whole line */
		int i = doc_para_at(&E.d, E.cur);
		a = E.d.p[i].start; b = pend(i);
		if (b < len()) b++;
		line_clip = 1;
	}
	s = tb_copy(&E.d.tb, a, b);
	size_t n = b - a;
	if (line_clip && (!n || s[n - 1] != '\n')) {   /* the last line has no break of its own */
		s = realloc(s, n + 2); s[n++] = '\n'; s[n] = 0;
	}
	clip_set(s, n);
	free(s);
	if (cut && !ro_guard()) {
		size_t from = a;
		if (line_clip && b == len() && a > 0 && tb_at(&E.d.tb, b - 1) != '\n') from = a - 1;   /* last line: its break before */
		doc_edit(&E.d, from, b - from, "", 0, 0, now_ms());
		E.cur = E.anc = view_snap(&E.d, from > len() ? len() : from, 1);
		edited();
		toast(line_clip ? "Cut the line" : "Cut");
	} else if (!cut) toast(line_clip ? "Copied the line" : "Copied");
}

static void paste_text(const char *s, size_t n)
{
	if (ro_guard() || !n) return;
	/* a whole line copied with nothing selected goes in above the current line */
	int whole_line = n && s[n - 1] == '\n' && !has_sel() && memchr(s, '\n', n) == s + n - 1 && line_clip;
	if (whole_line) {
		int i = doc_para_at(&E.d, E.cur);
		size_t ps = E.d.p[i].start, off = E.cur - ps;
		doc_edit(&E.d, ps, 0, s, n, 0, now_ms());
		E.cur = E.anc = ps + n + off;
	} else replace_sel(s, n, 0);
	E.cur = view_snap(&E.d, E.cur, 1);
	E.anc = E.cur;
	edited();
}

static void do_paste(void)
{
	size_t n;
	char *s = clip_get(&n);
	if (!s) { toast("The clipboard is empty"); return; }
	paste_text(s, n);
	free(s);
}

static char *hist_items[20];
static void hist_done(int id, int idx)
{
	(void)idx;
	if (id < 0) return;
	line_clip = 0;
	paste_text(hist_items[id], strlen(hist_items[id]));
	redraw();
}

static void do_paste_hist(void)
{
	int n = clip_history(hist_items, 20);
	if (!n) { toast("Nothing copied yet"); return; }
	struct mitem it[20];
	for (int i = 0; i < n; i++) {
		memset(&it[i], 0, sizeof(it[i]));
		size_t o = 0;
		for (const char *p = hist_items[i]; *p && o < sizeof(it[i].label) - 4; p++)
			it[i].label[o++] = *p == '\n' ? ' ' : *p == '\t' ? ' ' : *p;
		o = u8_whole(it[i].label, o);
		it[i].label[o] = 0;
		size_t l = strlen(hist_items[i]);
		int lines = 1;
		for (size_t k = 0; k + 1 < l; k++) lines += hist_items[i][k] == '\n';
		if (lines > 1) snprintf(it[i].hint, sizeof(it[i].hint), "%d lines", lines);
		it[i].id = i;
	}
	menu_open("Paste from history", it, n, 0, hist_done);
}

/* ---- find ---- */

static void find_update(int keep)
{
	E.nmarks = 0;
	size_t ql = strlen(E.fq);
	if (!ql) return;
	size_t n;
	char *t = doc_text(&E.d, &n), *f = malloc(n + 1), q[128];
	fold_text(t, n, f);
	fold_text(E.fq, ql, q);
	for (char *p = f; (p = memmem(p, (size_t)(f + n - p), q, ql)); p += ql) {
		if (E.nmarks == E.cmarks) { E.cmarks = E.cmarks ? E.cmarks * 2 : 32; E.marks = realloc(E.marks, (size_t)E.cmarks * 2 * sizeof(size_t)); }
		E.marks[2 * E.nmarks] = (size_t)(p - f);
		E.marks[2 * E.nmarks + 1] = (size_t)(p - f) + ql;
		E.nmarks++;
	}
	free(t); free(f);
	if (!keep) {   /* the first match after the cursor */
		size_t from = E.mode == MODE_EDIT ? sel0() : (E.focus >= 0 ? E.d.p[E.focus].start : 0);
		E.mark_i = 0;
		for (int i = 0; i < E.nmarks; i++) if (E.marks[2 * i] >= from) { E.mark_i = i; break; }
	}
}

static void find_go(int dir)
{
	if (!E.nmarks) return;
	E.mark_i = (E.mark_i + dir + E.nmarks) % E.nmarks;
	size_t a = E.marks[2 * E.mark_i], b = E.marks[2 * E.mark_i + 1];
	if (E.mode == MODE_EDIT) { E.anc = a; E.cur = b; }
	else E.focus = doc_para_at(&E.d, a);
	ensure_visible();
}

static void find_show(void)
{
	if (!E.nmarks) return;
	size_t a = E.marks[2 * E.mark_i], b = E.marks[2 * E.mark_i + 1];
	if (E.mode == MODE_EDIT) { E.anc = a; E.cur = b; }
	else E.focus = doc_para_at(&E.d, a);
	ensure_visible();
}

static int find_key(struct nkey k)
{
	uint32_t c = k.k;
	if (c == NK_ESC) { E.finding = 0; E.nmarks = 0; ensure_visible(); return 1; }   /* the match stays selected: type over it, or Esc again */
	if (c == NK_ENTER || c == NK_DOWN || c == NK_UP) { find_go((k.mods & M_SHIFT) || c == NK_UP ? -1 : 1); return 1; }
	if (c == NK_BS) {
		size_t l = strlen(E.fq);
		if (l) { l = u8_whole(E.fq, l - 1); while (l && ((unsigned char)E.fq[l] & 0xc0) == 0x80) l--; E.fq[l] = 0; }
		find_update(0); find_show();
		return 1;
	}
	if (c < 0x110000 && c >= ' ' && !(k.mods & (M_CTRL | M_ALT))) {
		char u[4];
		int n = u8_enc(c, u);
		size_t l = strlen(E.fq);
		if (l + (size_t)n < sizeof(E.fq)) { memcpy(E.fq + l, u, (size_t)n); E.fq[l + (size_t)n] = 0; }
		find_update(0); find_show();
		return 1;
	}
	return 0;   /* anything else: the find bar stays, the key does its usual job */
}

/* ---- the action menu ---- */

static int ctx(void) { return E.mode == MODE_VIEW ? CTX_VIEW : CTX_EDIT; }

static void menu_done(int id, int idx) { (void)idx; if (id > 0) app_cmd(id); }

void ed_menu(void)
{
	struct mitem it[40];
	int n = 0;
	for (const uint8_t *c = cmd_menu(ctx()); *c && n < 40; c++) {
		struct mitem *m = &it[n];
		memset(m, 0, sizeof(*m));
		m->id = *c;
		snprintf(m->label, sizeof(m->label), "%s", cmd_name(*c));
		if (*c == C_SIZE) snprintf(m->label, sizeof(m->label), "Text size: %s", text_size == 0 ? "Small" : text_size == 1 ? "Medium" : "Large");
		if (*c == C_AUTO_TICKED) snprintf(m->label, sizeof(m->label), "Ticked go to bottom: %s", st.ticked_bottom ? "On" : "Off");
		if (*c == C_DUPLICATE && E.readonly) snprintf(m->label, sizeof(m->label), "Duplicate to my notes");
		snprintf(m->hint, sizeof(m->hint), "%s", cmd_keys(ctx(), *c));
		if (E.readonly && *c != C_COPY && *c != C_FIND && *c != C_DUPLICATE && *c != C_SIZE && *c != C_HELP && *c != C_GUIDE && *c != C_NEW) m->off = 1;
		if (E.external && !E.readonly && (*c == C_PIN || *c == C_DELETE_NOTE || *c == C_REVERT || *c == C_DUPLICATE)) m->off = 1;
		if (*c == C_REVERT && !E.history_done) m->off = 1;
		if (*c == C_PIN) snprintf(m->label, sizeof(m->label), "%s", is_pinned(E.file) ? "Unpin" : "Pin");
		n++;
	}
	menu_open(E.title[0] ? E.title : "New note", it, n, 0, menu_done);
}

/* ---- note-level commands ---- */

static void delete_done(int yes)
{
	if (!yes) return;
	ed_save();
	char f[256];
	snprintf(f, sizeof(f), "%s", E.file);
	E.open = 0;
	if (f[0] && !store_trash(f)) toast("Moved to Recently Deleted");
	st.last[0] = 0;
	state_save();
	list_show();
}

static void revert_done(int yes)
{
	if (!yes) return;
	char *s; size_t n;
	if (store_history_read(E.file, &s, &n)) { toast("No earlier copy"); return; }
	doc_edit(&E.d, 0, len(), s, n, 0, now_ms());
	free(s);
	E.cur = E.anc = 0;
	E.top_para = E.top_row = 0;
	edited();
	toast("Back to how it was when opened (Ctrl+Z undoes)");
}

static void duplicate(void)
{
	size_t n;
	char *s = doc_text(&E.d, &n), name[256], err[128];
	store_name_for(s, n, NULL, name, sizeof(name));
	if (store_write(name, s, n, err, sizeof(err))) { free(s); toast(err); return; }
	free(s);
	ed_save();
	E.open = 0;
	ed_open(name, MODE_VIEW, 0, 0, 0, 0, 0);
	toast("Duplicated");
}

int ed_cmd(int cmd, int shifted)
{
	int view = E.mode == MODE_VIEW;
	size_t keep_anc = E.anc;
	int moving = 1;
	switch (cmd) {
	/* moving */
	case C_UP: case C_DOWN:
		if (view) {
			int d = cmd == C_UP ? -1 : 1, f = E.focus < 0 ? E.top_para : E.focus;
			int g = f + d;
			while (g >= 0 && g < E.d.np && (pli(g).kind == LK_BLANK || pli(g).kind == LK_FENCE)) g += d;
			if (g >= 0 && g < E.d.np) E.focus = g;
			else if (d > 0) { int p = E.d.np - 1, r = view_nrows(&E.d, p, E.mode, text_wd(), E.external) - 1; show_rows(p, r, p, r); }
			else { E.top_para = 0; E.top_row = 0; }
		} else vmove(cmd == C_UP ? -1 : 1);
		break;
	case C_PGUP: case C_PGDN:
		if (view) {   /* a page on; the highlight goes to the first line at the new top */
			int k = page_rows(), moved = 0;
			for (int i = 0; i < k; i++) moved += cmd == C_PGUP ? row_before(&E.top_para, &E.top_row) : row_after(&E.top_para, &E.top_row);
			int g = E.top_row ? E.top_para + 1 : E.top_para;
			while (g < E.d.np - 1 && (pli(g).kind == LK_BLANK || pli(g).kind == LK_FENCE)) g++;
			if (!moved) g = cmd == C_PGUP ? 0 : E.d.np - 1;
			E.focus = g < E.d.np ? g : E.d.np - 1;
		} else {
			int k = page_rows();
			vmove(cmd == C_PGUP ? -k : k);
			for (int i = 0; i < k; i++) { if (cmd == C_PGUP) row_before(&E.top_para, &E.top_row); else row_after(&E.top_para, &E.top_row); }
		}
		break;
	case C_LEFT:
		if (has_sel() && !shifted) E.cur = sel0();
		else E.cur = view_snap(&E.d, tb_prev(&E.d.tb, E.cur), -1);
		E.gx = -1;
		break;
	case C_RIGHT:
		if (has_sel() && !shifted) E.cur = sel1();
		else E.cur = view_snap(&E.d, tb_next(&E.d.tb, E.cur), 1);
		E.gx = -1;
		break;
	case C_HOME: line_home(); E.gx = -1; break;
	case C_END: line_end(); E.gx = -1; break;
	case C_WORD_L: E.cur = view_snap(&E.d, word_left(E.cur), -1); E.gx = -1; break;
	case C_WORD_R: E.cur = view_snap(&E.d, word_right(E.cur), 1); E.gx = -1; break;
	case C_TOP: if (view) { E.focus = 0; E.top_para = E.top_row = 0; } else E.cur = view_snap(&E.d, 0, 1); E.gx = -1; break;
	case C_BOTTOM: if (view) E.focus = E.d.np - 1; else E.cur = len(); E.gx = -1; break;
	default: moving = 0;
	}
	if (moving) {
		if (!view) {
			if (shifted) E.anc = keep_anc; else E.anc = E.cur;
			if (!shifted) E.nsel = 0;
		}
		ensure_visible();
		return 1;
	}
	switch (cmd) {
	case C_BS: do_bs(0); break;
	case C_DEL: do_del(0); break;
	case C_DEL_WORD_L: do_bs(1); break;
	case C_DEL_WORD_R: do_del(1); break;
	case C_ENTER: do_enter(); break;
	case C_TAB: do_indent(0); break;
	case C_OUTDENT: do_indent(1); break;
	case C_UNDO: case C_REDO: {
		size_t c = E.cur;
		int ok = cmd == C_UNDO ? doc_undo(&E.d, &c) : doc_redo(&E.d, &c);
		if (!ok) { toast(cmd == C_UNDO ? "Nothing to undo" : "Nothing to redo"); return 1; }
		E.cur = E.anc = view_snap(&E.d, c > len() ? len() : c, 1);
		if (view) E.focus = doc_para_at(&E.d, E.cur);
		edited();
		break;
	}
	case C_CHECKLIST: do_checklist(); break;
	case C_TICK: do_tick(); break;
	case C_TICKED_BOTTOM: do_ticked_bottom(); break;
	case C_MOVE_UP: do_move(-1); break;
	case C_MOVE_DOWN: do_move(1); break;
	case C_HEADING: do_heading(); break;
	case C_BOLD: do_wrap("**"); break;
	case C_ITALIC: do_wrap("*"); break;
	case C_SEL_MORE: sel_more(); break;
	case C_SEL_ALL: if (E.nsel < 8) { E.selstack[E.nsel][0] = E.anc; E.selstack[E.nsel][1] = E.cur; E.nsel++; } E.anc = 0; E.cur = len(); break;
	case C_COPY: do_copy(0); break;
	case C_CUT: do_copy(1); break;
	case C_PASTE: do_paste(); break;
	case C_PASTE_HIST: do_paste_hist(); break;
	case C_SAVE:
		if (E.readonly) break;
		E.d.dirty = 1;
		toast(ed_save() ? E.save_msg : "Saved");
		break;
	case C_FIND:
		E.finding = 1;
		find_update(0);
		break;
	case C_MODE: case C_EDIT_HERE:
		if (view) {
			if (ro_guard()) break;
			E.mode = MODE_EDIT;
			E.finding = 0;   /* typing goes into the note, not a find bar left from a search */
			E.nmarks = 0;
			int f = E.focus >= 0 ? E.focus : E.top_para;
			E.cur = E.anc = view_snap(&E.d, pend(f), 1);
			E.gx = -1;
		} else {
			E.mode = MODE_VIEW;
			E.focus = doc_para_at(&E.d, E.cur);
			ed_save();
		}
		E.nsel = 0;
		E.top_row = 0;
		ensure_visible();
		wake_cursor();
		break;
	case C_BACK:
		if (E.finding) { E.finding = 0; E.nmarks = 0; break; }
		if (!view && E.nsel) { E.nsel--; E.anc = E.selstack[E.nsel][0]; E.cur = E.selstack[E.nsel][1]; break; }
		if (!view && has_sel()) { E.anc = E.cur; break; }
		if (!view && !E.readonly) { ed_cmd(C_MODE, 0); break; }
		ed_close();
		return 1;
	case C_PIN:
		if (E.external || !E.file[0]) { ed_save(); if (!E.file[0]) { toast("Type something first"); break; } }
		set_pinned(E.file, !is_pinned(E.file));
		state_save();
		toast(is_pinned(E.file) ? "Pinned" : "Unpinned");
		break;
	case C_DELETE_NOTE:
		if (E.external) break;
		if (!E.file[0]) { E.d.dirty = 0; E.open = 0; list_show(); break; }
		confirm_open("Move this note to Recently Deleted?", "Delete", delete_done);
		break;
	case C_DUPLICATE: duplicate(); break;
	case C_REVERT: confirm_open("Go back to how this note was when you opened it?", "Revert", revert_done); break;
	default: return 0;
	}
	ensure_visible();
	return 1;
}

void ed_key(struct nkey k)
{
	wake_cursor();
	if (E.finding && find_key(k)) { redraw(); return; }
	int shifted;
	int cmd = cmd_lookup(ctx(), k, &shifted);
	if (cmd) {
		if (shifted && E.mode == MODE_EDIT) {
			/* Shift only extends a move */
			if (!(cmd >= C_UP && cmd <= C_BOTTOM)) shifted = 0;
		}
		if (!ed_cmd(cmd, shifted)) app_cmd(cmd);
		redraw();
		return;
	}
	if (E.mode == MODE_EDIT && k.k < 0x110000 && (k.k >= ' ' || k.k == '\t') && !(k.mods & (M_CTRL | M_ALT))) {
		type_char(k.k);
		ensure_visible();
		redraw();
		return;
	}
	if (E.mode == MODE_VIEW && k.k < 0x110000 && k.k > ' ' && !(k.mods & (M_CTRL | M_ALT)))
		toast(E.readonly ? "Read-only: Alt for the menu, Esc to go back" : "View mode: Enter or E to edit");
	redraw();
}

/* ---- drawing ---- */

void ed_draw(void)
{
	const char *state = "";
	uint32_t sc = pal.dim;
	char st_buf[32];
	if (E.save_err) { state = "Not saved"; sc = pal.warn; }
	else if (E.readonly) state = "Read-only";
	else if (E.mode == MODE_VIEW) state = E.d.dirty ? "View •" : "View";
	else { snprintf(st_buf, sizeof(st_buf), "%s", E.d.dirty ? "Edit •" : "Edit ✓"); state = st_buf; sc = E.d.dirty ? pal.dim : pal.ok; }
	d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, pal.bg);
	struct vdraw v = {
		.x = text_x(), .y = text_y(), .w = text_wd(), .h = text_h(),
		.top_para = E.top_para, .top_row = E.top_row, .mode = E.mode,
		.cur = E.cur, .show_cur = E.mode == MODE_EDIT && E.blink_on,
		.s0 = E.mode == MODE_EDIT ? sel0() : 0, .s1 = E.mode == MODE_EDIT ? sel1() : 0,
		.marks = E.marks, .nmarks = E.finding ? E.nmarks : 0,
		.focus = E.mode == MODE_VIEW ? E.focus : -1, .plain = E.external && !E.readonly,
	};
	if (E.mode == MODE_EDIT && !E.blink_on) v.show_cur = 0;
	if (E.d.np == 1 && !E.d.p[0].len && E.mode == MODE_EDIT) {   /* an empty note: a hint where the title goes */
		const char *h = "Title";
		d_text(F_H2, v.x + 3, v.y, pal.line == pal.bg ? pal.dim : pal.line, h, strlen(h));
	}
	view_draw(&E.d, &v);
	/* scroll position */
	if (E.d.np > 1) {
		int area = v.h, th = area / 6 < 10 ? 10 : area / 6;
		int ty = v.y + (area - th) * E.top_para / (E.d.np - 1);
		d_rect(SCREEN_W - 3, ty, 2, th, pal.line == pal.bg ? pal.dim : pal.line);
	}
	if (E.finding) {
		int y = BAR_H;
		d_rect(0, y, SCREEN_W, 18, pal.code_bg);
		d_rect(0, y + 17, SCREEN_W, 1, pal.line);
		char cnt[32];
		if (!E.fq[0]) snprintf(cnt, sizeof(cnt), "Enter: next");
		else if (!E.nmarks) snprintf(cnt, sizeof(cnt), "no matches");
		else snprintf(cnt, sizeof(cnt), "%d of %d", E.mark_i + 1, E.nmarks);
		int cw = text_w(F_UISMALL, cnt, strlen(cnt));
		d_text(F_UISMALL, SCREEN_W - 6 - cw, y + 3, E.fq[0] && !E.nmarks ? pal.warn : pal.dim, cnt, strlen(cnt));
		int x = 6 + d_text(F_UIBOLD, 6, y + 2, pal.dim, "Find ", 5);
		int maxw = SCREEN_W - 18 - cw - x;
		const char *q = E.fq;
		while (*q && text_w(F_UI, q, strlen(q)) > maxw) q++;   /* a long query: its end shows */
		while (((unsigned char)*q & 0xc0) == 0x80) q++;
		int qw = d_text(F_UI, x, y + 2, pal.fg, q, strlen(q));
		d_rect(x + qw + 1, y + 3, 1, 12, pal.accent);
	}
	bar_draw(E.title[0] ? E.title : (E.readonly ? "Guide" : "New note"), state, sc);
}

/* opened from a search: the find bar holds the search's first word, at its first match */
void ed_find_preset(const char *q)
{
	size_t l = strcspn(q, " ");
	if (!l || l >= sizeof(E.fq)) return;
	memcpy(E.fq, q, l);
	E.fq[l] = 0;
	E.finding = 1;
	E.focus = 0;
	find_update(0);
	find_show();
	redraw();
}

int ed_mode_ctx(void) { return E.mode == MODE_VIEW ? CTX_VIEW : CTX_EDIT; }
