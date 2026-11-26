// SPDX-License-Identifier: MIT
// The reader: an article as a read-only document in Notes' View mode (the same renderer, fonts,
// colours and text sizes), with a highlighted line that the plain letters move. Wiki articles
// have long paragraphs, so moving down a paragraph taller than the screen scrolls through it
// before the highlight goes on. Enter follows the highlighted line's link (a chooser when it has
// several) or shows its picture; C lists the sections; / finds; B bookmarks.
#define _GNU_SOURCE
#include "wiki.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
	struct doc d;
	int open, pack, id;
	char title[320];
	int top_para, top_row, focus;
	int finding;
	char fq[128];
	size_t *marks; int nmarks, cmarks, mark_i;
} R;

int reader_pack(void) { return R.pack; }
static const char *pname(void) { return R.pack >= 0 ? packs[R.pack].name : ""; }
const char *read_title(void) { return R.title; }

static int text_y(void) { return BAR_H + 3 + (R.finding ? 18 : 0); }
static int text_wd(void) { return SCREEN_W - 2 * MARGIN - 3; }
static int text_h(void) { return SCREEN_H - text_y() - 1; }
static int nrows(int p) { return view_nrows(&R.d, p, MODE_VIEW, text_wd(), 0); }
static int rh(int p, int r) { return view_row_h(&R.d, p, r, MODE_VIEW, text_wd(), 0); }

static struct linfo pli(int i)
{
	size_t n;
	const char *s = doc_para_text(&R.d, i, &n);
	struct linfo li;
	line_info(s, n, R.d.p[i].code, &li);
	return li;
}

static int skip(int i) { int k = pli(i).kind; return k == LK_BLANK || k == LK_FENCE || k == LK_RULE; }

/* ---- scrolling (Notes' editor.c) ---- */

static int row_before(int *p, int *r)
{
	if (*r > 0) { (*r)--; return 1; }
	if (*p == 0) return 0;
	(*p)--;
	*r = nrows(*p) - 1;
	return 1;
}

static int row_after(int *p, int *r)
{
	if (*r + 1 < nrows(*p)) { (*r)++; return 1; }
	if (*p + 1 >= R.d.np) return 0;
	(*p)++;
	*r = 0;
	return 1;
}

/* scroll so that rows [p0,r0]..[p1,r1] show (the end wins when they do not fit) */
static void show_rows(int p0, int r0, int p1, int r1)
{
	if (p0 < R.top_para || (p0 == R.top_para && r0 < R.top_row)) { R.top_para = p0; R.top_row = r0; }
	int h = 0, p = R.top_para, r = R.top_row, limit = text_h();
	for (;;) {
		h += rh(p, r);
		if (p == p1 && r == r1) break;
		if (!row_after(&p, &r)) break;
		if (h > limit * 4) { R.top_para = p1; R.top_row = r1; h = rh(p1, r1); break; }
	}
	while (h > limit && (R.top_para < p1 || (R.top_para == p1 && R.top_row < r1))) {
		h -= rh(R.top_para, R.top_row);
		row_after(&R.top_para, &R.top_row);
	}
}

/* height from the top of the screen to the end of line i (more than the screen: limit + 1) */
static int bottom_of(int i)
{
	int h = 0, p = R.top_para, r = R.top_row, limit = text_h();
	if (i < R.top_para) return 0;
	for (;;) {
		h += rh(p, r);
		if (h > limit) return limit + 1;
		if (p == i && r == nrows(i) - 1) return h;
		if (!row_after(&p, &r)) return h;
	}
}

static int top_shows(int i) { return R.top_para < i || (R.top_para == i && R.top_row == 0); }
static int bottom_shows(int i) { return bottom_of(i) <= text_h(); }
static int page_rows(void) { int l = font_lh(F_REG); return text_h() / (l ? l : 14) - 1; }

/* the highlight on line g: all of it when it fits, else its first rows */
static void show_line(int g, int from_below)
{
	int last = nrows(g) - 1, h = 0;
	for (int r = 0; r <= last; r++) h += rh(g, r);
	if (h <= text_h()) show_rows(g, 0, g, last);
	else if (from_below) show_rows(g, last, g, last);
	else { if (!top_shows(g) || g > R.top_para) show_rows(g, 0, g, 0); }
	if (g < R.top_para) { R.top_para = g; R.top_row = 0; }
}

static void move(int d)
{
	if (!R.d.np) return;
	int f = R.focus < 0 ? R.top_para : R.focus;
	if (d > 0 && !bottom_shows(f)) {   /* the rest of a tall line first */
		int k = page_rows() / 2 + 1;
		for (int i = 0; i < k && !bottom_shows(f); i++) row_after(&R.top_para, &R.top_row);
		return;
	}
	if (d < 0 && !top_shows(f)) {
		int k = page_rows() / 2 + 1;
		for (int i = 0; i < k && !top_shows(f); i++) row_before(&R.top_para, &R.top_row);
		return;
	}
	int g = f + d;
	while (g >= 0 && g < R.d.np && skip(g)) g += d;
	if (g < 0 || g >= R.d.np) {
		if (d < 0) { R.top_para = R.top_row = 0; }
		return;
	}
	R.focus = g;
	show_line(g, d < 0);
}

static void page(int d)
{
	int k = page_rows(), moved = 0;
	for (int i = 0; i < k; i++) moved += d < 0 ? row_before(&R.top_para, &R.top_row) : row_after(&R.top_para, &R.top_row);
	int g = R.top_row ? R.top_para + 1 : R.top_para;
	if (R.top_row && !bottom_shows(R.top_para)) g = R.top_para;   /* still inside a tall line */
	while (g < R.d.np - 1 && skip(g)) g++;
	if (!moved) g = d < 0 ? 0 : R.d.np - 1;
	R.focus = g < R.d.np ? g : R.d.np - 1;
}

/* ---- opening ---- */

static void md_italic(char *o, size_t max, const char *s)
{
	size_t k = 0;
	o[k++] = '*';
	for (; *s && k + 3 < max; s++) { if (*s == '*' || *s == '_' || *s == '\\') o[k++] = '\\'; o[k++] = *s; }
	o[k++] = '*';
	o[k] = 0;
}

void read_enter(void)
{
	struct scr *s = cur_scr();
	char *md;
	size_t n;
	doc_free(&R.d);
	free(R.marks);
	memset(&R, 0, sizeof(R));
	R.focus = -1;
	doc_init(&R.d);
	R.pack = s->pack; R.id = s->id;
	uint32_t t0 = real_ms();
	extern char *builtin_page(int id, char *title, size_t tn);
	if (s->pack < 0) { md = builtin_page(s->id, R.title, sizeof(R.title)); n = strlen(md); }
	else if (pack_article(s->pack, s->id, &md, &n, R.title, sizeof(R.title))) {
		toast("This article could not be read");
		pop_scr();
		return;
	}
	struct pack *pk = s->pack >= 0 ? &packs[s->pack] : NULL;
	char sub[400] = "", foot[400] = "";
	for (int i = 0; pk && pk->memory && i < pk->narts; i++)
		if (pk->arts[i].id == s->id && pk->arts[i].summary[0]) md_italic(sub, sizeof(sub), pk->arts[i].summary);
	if (pk && pk->footer[0]) md_italic(foot, sizeof(foot), pk->footer);
	size_t tl = strlen(R.title), sl = strlen(sub), fl = strlen(foot);
	char *all = malloc(tl + sl + n + fl + 16), *o = all;
	memcpy(o, R.title, tl); o += tl; *o++ = '\n';
	if (sl) { memcpy(o, sub, sl); o += sl; *o++ = '\n'; }
	*o++ = '\n';
	memcpy(o, md, n); o += n;
	while (o > all && o[-1] == '\n') o--;
	if (fl) { memcpy(o, "\n\n---\n", 6); o += 6; memcpy(o, foot, fl); o += fl; }
	doc_load(&R.d, all, (size_t)(o - all));
	free(all);
	free(md);
	R.open = 1;
	R.focus = s->focus >= 0 && s->focus < R.d.np ? s->focus : 0;
	R.top_para = s->top_para < R.d.np ? s->top_para : 0;
	R.top_row = s->top_row;
	if (R.top_row >= nrows(R.top_para)) R.top_row = 0;
	if (pk) recent_add(pk->name, R.title);
	timing_log("article", t0);
	trace("read \"%s\" pack=%s lines=%d", R.title, pk ? pk->name : "-", R.d.np);
}

void read_leave(void)
{
	struct scr *s = cur_scr();
	s->focus = R.focus; s->top_para = R.top_para; s->top_row = R.top_row;
}

/* ---- links ---- */

struct link { char text[96]; char url[64]; };

static int line_links(int i, struct link *out, int max)
{
	size_t n;
	const char *s = doc_para_text(&R.d, i, &n);
	int k = 0;
	for (size_t a = 0; a < n && k < max; a++) {
		if (s[a] != '[' || (a && s[a - 1] == '\\')) continue;
		const char *e = memchr(s + a + 1, ']', n - a - 1);
		if (!e || (size_t)(e - s) + 1 >= n || e[1] != '(') continue;
		const char *pe = memchr(e + 2, ')', n - (size_t)(e + 2 - s));
		if (!pe) continue;
		size_t tl = (size_t)(e - s) - a - 1, ul = (size_t)(pe - e) - 2;
		char raw[200];
		snprintf(raw, sizeof(raw), "%.*s", (int)(tl < 190 ? tl : 190), s + a + 1);
		md_plain_line(raw, strlen(raw), out[k].text, sizeof(out[k].text));
		/* drop the escapes md_plain_line leaves */
		char *t = out[k].text, *w = t;
		for (; *t; t++) { if (*t == '\\' && t[1]) t++; *w++ = *t; }
		*w = 0;
		snprintf(out[k].url, sizeof(out[k].url), "%.*s", (int)(ul < 60 ? ul : 60), e + 2);
		k++;
		a = (size_t)(pe - s);
	}
	return k;
}

static struct link choose[32];
static void link_chosen(int id, int idx) { (void)idx; if (id >= 0) open_link(R.pack, choose[id].url); }

static void follow(void)
{
	if (R.focus < 0) return;
	struct link l[32];
	int n = line_links(R.focus, l, 32);
	if (n == 1) {
		if (!strncmp(l[0].url, "img:", 4)) {
			struct scr s = { .kind = S_PIC, .pack = R.pack };
			scopy(s.name, sizeof(s.name), l[0].url + 4);
			push_scr(&s);
		} else open_link(R.pack, l[0].url);
		return;
	}
	if (!n) { toast("No link on this line"); return; }
	struct mitem it[32];
	memcpy(choose, l, sizeof(l[0]) * (size_t)n);
	for (int i = 0; i < n; i++) {
		memset(&it[i], 0, sizeof(it[i]));
		scopy(it[i].label, sizeof(it[i].label), l[i].text);
		it[i].id = i;
	}
	menu_open("Open which link?", it, n, 0, link_chosen);
}

/* ---- contents ---- */

static int heads[64];
static void contents_done(int id, int idx)
{
	(void)idx;
	if (id < 0) return;
	R.focus = heads[id];
	R.top_para = heads[id];
	R.top_row = 0;
	show_rows(R.focus, 0, R.focus, 0);
	trace("contents -> line %d", R.focus);
	redraw();
}

static void contents(void)
{
	struct mitem it[64];
	int n = 0, sel = 0;
	for (int i = 1; i < R.d.np && n < 64; i++) {
		struct linfo li = pli(i);
		if (li.kind != LK_HEAD) continue;
		size_t len;
		const char *s = doc_para_text(&R.d, i, &len);
		memset(&it[n], 0, sizeof(it[n]));
		char plain[96];
		md_plain_line(s, len, plain, sizeof(plain));
		size_t ind = li.level >= 3 ? 4 : 0;
		memcpy(it[n].label, "    ", ind);
		scopy(it[n].label + ind, sizeof(it[n].label) - ind, plain);
		it[n].id = n;
		heads[n] = i;
		if (R.focus >= i) sel = n;
		n++;
	}
	if (!n) { toast("This article has no sections"); return; }
	menu_open("Contents", it, n, sel, contents_done);
}

/* ---- find (Notes' find bar) ---- */

static void find_update(void)
{
	R.nmarks = 0;
	size_t ql = strlen(R.fq);
	if (!ql) return;
	size_t n;
	char *t = doc_text(&R.d, &n), *f = malloc(n + 1), q[128];
	fold_text(t, n, f);
	fold_text(R.fq, ql, q);
	for (char *p = f; (p = memmem(p, (size_t)(f + n - p), q, ql)); p += ql) {
		if (R.nmarks == R.cmarks) { R.cmarks = R.cmarks ? R.cmarks * 2 : 32; R.marks = realloc(R.marks, (size_t)R.cmarks * 2 * sizeof(size_t)); }
		R.marks[2 * R.nmarks] = (size_t)(p - f);
		R.marks[2 * R.nmarks + 1] = (size_t)(p - f) + ql;
		R.nmarks++;
	}
	free(t); free(f);
	size_t from = R.focus >= 0 ? R.d.p[R.focus].start : 0;
	R.mark_i = 0;
	for (int i = 0; i < R.nmarks; i++) if (R.marks[2 * i] >= from) { R.mark_i = i; break; }
}

static void find_show(void)
{
	if (!R.nmarks) return;
	R.focus = doc_para_at(&R.d, R.marks[2 * R.mark_i]);
	/* the match's own row, inside a tall line */
	int p, r, x;
	view_pos(&R.d, MODE_VIEW, text_wd(), 0, R.marks[2 * R.mark_i], &p, &r, &x);
	show_rows(p, r, p, r);
}

static int find_key(struct nkey k)
{
	uint32_t c = k.k;
	if (c == NK_ESC) { R.finding = 0; R.nmarks = 0; return 1; }
	if (c == NK_ENTER || c == NK_DOWN || c == NK_UP) {
		if (R.nmarks) R.mark_i = (R.mark_i + ((k.mods & M_SHIFT) || c == NK_UP ? -1 : 1) + R.nmarks) % R.nmarks;
		find_show();
		return 1;
	}
	if (c == NK_BS) {
		size_t l = strlen(R.fq);
		if (l) { l--; while (l && ((unsigned char)R.fq[l] & 0xc0) == 0x80) l--; R.fq[l] = 0; }
		find_update(); find_show();
		return 1;
	}
	if (c < 0x110000 && c >= ' ' && !(k.mods & (M_CTRL | M_ALT))) {
		char u[4];
		int n = u8_enc(c, u);
		size_t l = strlen(R.fq);
		if (l + (size_t)n < sizeof(R.fq)) { memcpy(R.fq + l, u, (size_t)n); R.fq[l + (size_t)n] = 0; }
		find_update(); find_show();
		return 1;
	}
	return 0;
}

/* ---- commands ---- */

static void copy_text(void)
{
	size_t n, o = 0;
	char *t = doc_text(&R.d, &n), *out = malloc(n + 1);
	for (int i = 0; i < R.d.np; i++) {
		size_t len;
		const char *s = doc_para_text(&R.d, i, &len);
		struct linfo li;
		line_info(s, len, 0, &li);
		if (li.kind == LK_RULE) continue;
		char line[2048], plain[2048];
		size_t k = 0;
		for (size_t a = 0; a < len && k + 1 < sizeof(line); a++) {   /* links: their text only */
			if (s[a] == ']' && a + 1 < len && s[a + 1] == '(') { const char *e = memchr(s + a, ')', len - a); if (e) { a = (size_t)(e - s); continue; } }
			if (s[a] == '[' && (!a || s[a - 1] != '\\')) continue;
			if (s[a] == '!' && a + 1 < len && s[a + 1] == '[') continue;
			line[k++] = s[a];
		}
		md_plain_line(line, k, plain, sizeof(plain));
		size_t pl = strlen(plain);
		if (li.kind == LK_BULLET && o + 2 < n) { memcpy(out + o, "- ", 2); o += 2; }
		if (o + pl + 1 > n) break;
		memcpy(out + o, plain, pl); o += pl;
		out[o++] = '\n';
	}
	clip_set(out, o);
	free(out); free(t);
	toast("Article text copied");
}

int read_cmd(int cmd)
{
	switch (cmd) {
	case W_UP: move(-1); break;
	case W_DOWN: move(1); break;
	case W_PGUP: page(-1); break;
	case W_PGDN: page(1); break;
	case W_TOP: R.focus = 0; R.top_para = R.top_row = 0; break;
	case W_BOTTOM: R.focus = R.d.np - 1; show_rows(R.d.np - 1, nrows(R.d.np - 1) - 1, R.d.np - 1, nrows(R.d.np - 1) - 1); break;
	case W_OPEN: follow(); break;
	case W_CONTENTS: contents(); break;
	case W_FIND: R.finding = 1; find_update(); break;
	case W_BOOKMARK: {
		if (R.pack < 0) break;
		int on = !bookmarked(pname(), R.title);
		bookmark_set(pname(), R.title, on);
		toast(on ? "Bookmarked" : "Bookmark removed");
		break;
	}
	case W_COPY: copy_text(); break;
	default: return 0;
	}
	trace("reader focus=%d top=%d.%d", R.focus, R.top_para, R.top_row);
	return 1;
}

void read_key(struct nkey k)
{
	if (R.finding && find_key(k)) { trace("find \"%s\" %d matches, at line %d", R.fq, R.nmarks, R.focus); redraw(); return; }
	int cmd = wcmd_lookup(X_READ, k);
	if (cmd) { if (!read_cmd(cmd)) wiki_cmd(cmd); redraw(); return; }
	if (k.k < 0x110000 && k.k > ' ' && !(k.mods & (M_CTRL | M_ALT))) toast("Alt for the menu, Fn+H for the keys, Esc to go back");
	redraw();
}

void read_menu(void)
{
	struct mitem it[24];
	int n = 0;
	for (const uint8_t *c = wcmd_menu(X_READ); *c && n < 24; c++) {
		struct mitem *m = &it[n++];
		memset(m, 0, sizeof(*m));
		m->id = *c;
		scopy(m->label, sizeof(m->label), wcmd_name(*c));
		if (*c == W_BOOKMARK) scopy(m->label, sizeof(m->label), bookmarked(pname(), R.title) ? "Remove bookmark" : "Bookmark");
		if (*c == W_BOOKMARK && R.pack < 0) m->off = 1;
		if (*c == W_SIZE) snprintf(m->label, sizeof(m->label), "Text size: %s", text_size == 0 ? "Small" : text_size == 1 ? "Medium" : "Large");
		if (*c == W_RANDOM && pack_by_name("Simple English Wikipedia") < 0) m->off = 1;
		scopy(m->hint, sizeof(m->hint), wcmd_keys(X_READ, *c));
	}
	extern void menu_cmd_done(int id, int idx);
	menu_open(R.title, it, n, 0, menu_cmd_done);
}

void read_draw(void)
{
	d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, pal.bg);
	if (!R.open) return;
	view_pic = &wiki_pics;
	struct vdraw v = {
		.x = MARGIN, .y = text_y(), .w = text_wd(), .h = text_h(),
		.top_para = R.top_para, .top_row = R.top_row, .mode = MODE_VIEW,
		.marks = R.marks, .nmarks = R.finding ? R.nmarks : 0, .focus = R.focus,
	};
	view_draw(&R.d, &v);
	if (R.d.np > 1) {
		int area = v.h, th = area / 6 < 10 ? 10 : area / 6;
		int ty = v.y + (area - th) * R.top_para / (R.d.np - 1);
		d_rect(SCREEN_W - 3, ty, 2, th, pal.line == pal.bg ? pal.dim : pal.line);
	}
	if (R.finding) {
		int y = BAR_H;
		d_rect(0, y, SCREEN_W, 18, pal.code_bg);
		d_rect(0, y + 17, SCREEN_W, 1, pal.line);
		char cnt[32];
		if (!R.fq[0]) snprintf(cnt, sizeof(cnt), "Enter: next");
		else if (!R.nmarks) snprintf(cnt, sizeof(cnt), "no matches");
		else snprintf(cnt, sizeof(cnt), "%d of %d", R.mark_i + 1, R.nmarks);
		int cw = text_w(F_UISMALL, cnt, strlen(cnt));
		d_text(F_UISMALL, SCREEN_W - 6 - cw, y + 3, R.fq[0] && !R.nmarks ? pal.warn : pal.dim, cnt, strlen(cnt));
		int x = 6 + d_text(F_UIBOLD, 6, y + 2, pal.dim, "Find ", 5);
		int maxw = SCREEN_W - 18 - cw - x;
		const char *q = R.fq;
		while (*q && text_w(F_UI, q, strlen(q)) > maxw) q++;
		while (((unsigned char)*q & 0xc0) == 0x80) q++;
		int qw = d_text(F_UI, x, y + 2, pal.fg, q, strlen(q));
		d_rect(x + qw + 1, y + 3, 1, 12, pal.accent);
	}
	char state[48];
	int pct = R.d.np > 1 ? R.top_para * 100 / (R.d.np - 1) : 0;
	if (bottom_shows(R.d.np - 1)) pct = 100;
	snprintf(state, sizeof(state), "%s%d%%", bookmarked(pname(), R.title) ? "★ " : "", pct);
	bar_draw(R.title, state, pal.dim);
}
