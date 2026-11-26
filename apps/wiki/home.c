// SPDX-License-Identifier: MIT
// The screens made of rows: the search box (home), the lists (topics, a topic's guides, the
// Army manual's chapters, recently read, bookmarks, "search inside" results) and the triage
// questions. Home with an empty box shows the shelves; typing searches titles at every key,
// survival first, then Wikipedia; Enter opens the highlighted row, Ctrl+Enter (or the last row)
// searches inside the articles.
#include "wiki.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_H 20

enum { RW_HEAD, RW_ROW, RW_NOTE };
enum { A_NONE, A_TRIAGE, A_GUIDES, A_MANUAL, A_RECENT, A_MARKS, A_WIKI, A_GET, A_INSIDE, A_ARTICLE, A_LIST, A_ENTRY, A_ANSWER };
struct row {
	int kind, action;
	char text[200], sub[200], tag[64];
	uint32_t colour, tag_colour;
	int pack, id, param;
	int hl[6][2], nhl;
};

static struct row rows[160];
static int nrows;
static char query[256];
static struct result res[MAX_RESULTS];
static int nres;

static int row_h(int i)
{
	if (rows[i].kind == RW_HEAD) return 15;
	return rows[i].sub[0] ? 30 : 17;
}

static int area_y(void) { return cur_scr()->kind == S_HOME ? BAR_H + SEARCH_H + 2 : BAR_H + 2; }
static int selectable(int i) { return i >= 0 && i < nrows && rows[i].kind == RW_ROW; }

static struct row *add(int kind, int action, const char *text)
{
	if (nrows >= (int)(sizeof(rows) / sizeof(rows[0]))) return &rows[nrows - 1];
	struct row *r = &rows[nrows++];
	memset(r, 0, sizeof(*r));
	r->kind = kind; r->action = action;
	r->pack = -1;
	scopy(r->text, sizeof(r->text), text);
	return r;
}

static int wiki_pack(void)
{
	for (int p = 0; p < npacks; p++) if (!packs[p].memory) return p;
	return -1;
}

static int survival_pack(void)
{
	for (int p = 0; p < npacks; p++) if (packs[p].memory) return p;
	return -1;
}

static void show_sel(void)
{
	struct scr *s = cur_scr();
	if (!selectable(s->sel)) {
		int k = s->sel < 0 ? 0 : s->sel;
		while (k < nrows && !selectable(k)) k++;
		if (k >= nrows) { k = s->sel; while (k > 0 && !selectable(k)) k--; }
		s->sel = selectable(k) ? k : -1;
	}
	if (s->sel < 0) { s->top = 0; return; }
	if (s->sel < s->top) s->top = s->sel;
	while (s->top > 0 && rows[s->top - 1].kind == RW_HEAD && s->top - 1 >= 0 && s->sel - s->top < 2) s->top--;   /* keep a header with its first row */
	int area = SCREEN_H - area_y();
	for (;;) {
		int h = 0;
		for (int i = s->top; i <= s->sel; i++) h += row_h(i);
		if (h <= area || s->top >= s->sel) break;
		s->top++;
	}
}

/* ---- building the rows ---- */

static void shelves(void)
{
	int sp = survival_pack(), wp = wiki_pack();
	if (triage_pack() >= 0) {
		struct row *r = add(RW_ROW, A_TRIAGE, "What is happening?");
		r->colour = pal.warn;
		snprintf(r->tag, sizeof(r->tag), "first aid, disasters");
	}
	if (sp >= 0) {
		for (int i = 0; i < packs[sp].nshelves; i++) {
			struct shelf *sh = &packs[sp].shelves[i];
			int guides = !strcmp(sh->kind, "guides");
			struct row *r = add(RW_ROW, guides ? A_GUIDES : A_MANUAL, sh->name);
			scopy(r->tag, sizeof(r->tag), guides ? sh->about : "FM 21-76");
		}
	}
	if (ws.nrecent) {
		struct row *r = add(RW_ROW, A_RECENT, "Recently read");
		snprintf(r->tag, sizeof(r->tag), "%.60s", ws.recent[0].title);
	}
	if (ws.nmarks) {
		struct row *r = add(RW_ROW, A_MARKS, "Bookmarks");
		snprintf(r->tag, sizeof(r->tag), "%d", ws.nmarks);
	}
	if (wp >= 0) {
		struct row *r = add(RW_ROW, A_WIKI, packs[wp].shelves[0].name);
		scopy(r->tag, sizeof(r->tag), packs[wp].shelves[0].about);
	} else {
		struct row *r = add(RW_ROW, A_GET, "Get Wikipedia");
		snprintf(r->tag, sizeof(r->tag), "267,000 articles, 234 MB");
	}
	if (!npacks) add(RW_NOTE, A_NONE, "No packs found: see About (Alt)");
	if (pack_errors[0]) {
		char m[600];
		snprintf(m, sizeof(m), "Could not read: %s", pack_errors);
		add(RW_NOTE, A_NONE, m);
	}
}

static void results(void)
{
	nres = search_titles(query, res, MAX_RESULTS);
	int section = -1;
	for (int i = 0; i < nres; i++) {
		if (res[i].section != section) {
			section = res[i].section;
			add(RW_HEAD, A_NONE, section == 0 ? "SURVIVAL" : "WIKIPEDIA");
		}
		struct row *r = add(RW_ROW, A_ARTICLE, res[i].title);
		r->pack = res[i].pack; r->id = res[i].id;
		scopy(r->tag, sizeof(r->tag), res[i].tag);
		r->tag_colour = res[i].tag_colour;
	}
	if (!nres) add(RW_NOTE, A_NONE, "No titles match");
	char t[300];
	snprintf(t, sizeof(t), "Search inside articles for “%s”", query);
	add(RW_ROW, A_INSIDE, t)->colour = pal.accent;
}

static void list_rows(void)
{
	struct scr *s = cur_scr();
	int sp = survival_pack();
	switch (s->list) {
	case L_CATEGORIES:
		if (sp < 0) break;
		for (int i = 0; i < packs[sp].ncats; i++) {
			struct category *c = &packs[sp].cats[i];
			struct row *r = add(RW_ROW, A_LIST, c->name);
			int n = 0;
			for (int k = 0; k < packs[sp].narts; k++) n += packs[sp].arts[k].category == c->id;
			scopy(r->sub, sizeof(r->sub), c->about);
			snprintf(r->tag, sizeof(r->tag), "%d", n);
			r->tag_colour = c->colour;
			r->param = c->id;
		}
		break;
	case L_CATEGORY:
	case L_CHAPTERS:
		if (sp < 0) break;
		for (int k = 0; k < packs[sp].narts; k++) {
			struct mem_article *a = &packs[sp].arts[k];
			if (s->list == L_CATEGORY ? a->category != s->param : a->shelf != 2) continue;
			char t[200];
			if (s->list == L_CHAPTERS) snprintf(t, sizeof(t), "%d  %s", a->ord, a->title);
			else scopy(t, sizeof(t), a->title);
			struct row *r = add(RW_ROW, A_ARTICLE, t);
			r->pack = sp; r->id = a->id;
			if (s->list == L_CATEGORY) scopy(r->sub, sizeof(r->sub), a->summary);
		}
		break;
	case L_RECENT:
	case L_MARKS: {
		struct entry *l = s->list == L_RECENT ? ws.recent : ws.marks;
		int n = s->list == L_RECENT ? ws.nrecent : ws.nmarks;
		for (int i = 0; i < n; i++) {
			struct row *r = add(RW_ROW, A_ENTRY, l[i].title);
			r->pack = pack_by_name(l[i].pack);
			r->param = i;
			scopy(r->tag, sizeof(r->tag), r->pack < 0 ? "pack missing" : !strcmp(l[i].pack, "Survival") ? "Survival" : "Wikipedia");
		}
		if (!n) add(RW_NOTE, A_NONE, s->list == L_RECENT ? "Nothing read yet" : "No bookmarks yet: B in an article");
		break;
	}
	case L_INSIDE: {
		int too_common;
		nres = search_inside(s->q, res, MAX_RESULTS, &too_common);
		if (too_common) add(RW_NOTE, A_NONE, "Only very common words: add a word for better results");
		uint32_t t0 = real_ms();
		for (int i = 0; i < nres; i++) {
			struct row *r = add(RW_ROW, A_ARTICLE, res[i].title);
			r->pack = res[i].pack; r->id = res[i].id;
			scopy(r->tag, sizeof(r->tag), res[i].tag);
			r->tag_colour = res[i].tag_colour;
			r->param = i;
			if (i < 12) {   /* the matching line: for the first screenfuls, the rest when drawn */
				result_snippet(&res[i], s->q);
				scopy(r->sub, sizeof(r->sub), res[i].snip[0] ? res[i].snip : " ");
				memcpy(r->hl, res[i].hl, sizeof(r->hl)); r->nhl = res[i].nhl;
			} else snprintf(r->sub, sizeof(r->sub), " ");
		}
		timing_log("snippets", t0);
		if (!nres) add(RW_NOTE, A_NONE, "Nothing found");
		break;
	}
	}
}

static void triage_rows(void)
{
	const struct tri_node *t = triage_node(cur_scr()->node);
	if (!t) return;
	for (int i = 0; i < t->n; i++) {
		char b[96];
		snprintf(b, sizeof(b), "%d  %s", i + 1, t->label[i]);
		struct row *r = add(RW_ROW, A_ANSWER, b);
		r->param = i;
	}
}

static void trace_rows(void)
{
	struct scr *s = cur_scr();
	char b[6000];
	size_t o = 0;
	const char *what = s->kind == S_HOME ? "home" : s->kind == S_TRIAGE ? "triage" : "list";
	o += (size_t)snprintf(b + o, sizeof(b) - o, "%s", what);
	if (s->kind == S_HOME) o += (size_t)snprintf(b + o, sizeof(b) - o, " q=\"%s\"", query);
	if (s->kind == S_TRIAGE) o += (size_t)snprintf(b + o, sizeof(b) - o, " node=%s", s->node);
	if (s->kind == S_LIST) o += (size_t)snprintf(b + o, sizeof(b) - o, " list=%d", s->list);
	o += (size_t)snprintf(b + o, sizeof(b) - o, " sel=%d:", s->sel);
	for (int i = 0; i < nrows && i < 40 && o + 4 < sizeof(b); i++)
		o += (size_t)snprintf(b + o, sizeof(b) - o, " %s%s%s |", i == s->sel ? ">" : "", rows[i].kind == RW_HEAD ? "#" : "", rows[i].text);
	trace("%s", b);
}

void home_enter(void)
{
	struct scr *s = cur_scr();
	nrows = 0;
	if (s->kind == S_HOME) { if (query[0]) results(); else shelves(); }
	else if (s->kind == S_LIST) list_rows();
	else if (s->kind == S_TRIAGE) triage_rows();
	show_sel();
	trace_rows();
}

/* ---- opening things ---- */

void open_article(int pack, int id)
{
	if (pack < 0 || !id) { toast("That article is not in any pack here"); return; }
	struct scr s = { .kind = S_READ, .pack = pack, .id = id, .focus = 0 };
	push_scr(&s);
}

void open_list(int list, int param)
{
	struct scr s = { .kind = S_LIST, .list = list, .param = param, .sel = 0 };
	push_scr(&s);
}

void open_triage(const char *node)
{
	struct scr s = { .kind = S_TRIAGE, .sel = 0 };
	scopy(s.node, sizeof(s.node), node);
	push_scr(&s);
}

void open_link(int pack, const char *url)
{
	if (!strncmp(url, "id:", 3)) open_article(pack, atoi(url + 3));
	else if (!strncmp(url, "wiki:", 5)) {   /* a title in any pack */
		for (int p = 0; p < npacks; p++) { int id = pack_find_title(p, url + 5); if (id) { open_article(p, id); return; } }
		toast("That article is not in any pack here");
	} else toast("Links to websites don't work offline");
}

static void inside(const char *q)
{
	if (!q[0]) return;
	struct scr s = { .kind = S_LIST, .list = L_INSIDE, .sel = 0 };
	snprintf(s.q, sizeof(s.q), "%.199s", q);
	push_scr(&s);
}

static void activate(int i)
{
	if (!selectable(i)) return;
	struct row *r = &rows[i];
	struct scr *s = cur_scr();
	switch (r->action) {
	case A_TRIAGE: open_triage("root"); break;
	case A_GUIDES: open_list(L_CATEGORIES, 0); break;
	case A_MANUAL: open_list(L_CHAPTERS, 0); break;
	case A_RECENT: open_list(L_RECENT, 0); break;
	case A_MARKS: open_list(L_MARKS, 0); break;
	case A_WIKI: { int p = wiki_pack(); if (p >= 0) open_article(p, pack_random(p)); break; }
	case A_GET: { struct scr x = { .kind = S_READ, .pack = -1, .id = 2 }; push_scr(&x); break; }
	case A_INSIDE: inside(query); break;
	case A_ARTICLE: open_article(r->pack, r->id); break;
	case A_LIST: open_list(L_CATEGORY, r->param); break;
	case A_ENTRY: {
		struct entry *e = s->list == L_RECENT ? &ws.recent[r->param] : &ws.marks[r->param];
		int p = pack_by_name(e->pack);
		open_article(p, p >= 0 ? pack_find_title(p, e->title) : 0);
		break;
	}
	case A_ANSWER: {
		const struct tri_node *t = triage_node(s->node);
		if (!t || r->param >= t->n) break;
		const char *to = t->to[r->param];
		if (!strncmp(to, "id:", 3)) open_article(triage_pack(), atoi(to + 3));
		else open_triage(to);
		break;
	}
	}
}

static void move_sel(int d)
{
	struct scr *s = cur_scr();
	int k = s->sel;
	do k += d; while (k >= 0 && k < nrows && !selectable(k));
	if (selectable(k)) s->sel = k;
	else if (d < 0) s->top = 0;
	show_sel();
}

int home_cmd(int cmd)
{
	struct scr *s = cur_scr();
	int area = SCREEN_H - area_y();
	switch (cmd) {
	case W_UP: move_sel(-1); break;
	case W_DOWN: move_sel(1); break;
	case W_PGUP: for (int h = 0; h < area && s->sel > 0;) { int o = s->sel; move_sel(-1); if (s->sel == o) break; h += row_h(s->sel); } break;
	case W_PGDN: for (int h = 0; h < area;) { int o = s->sel; move_sel(1); if (s->sel == o) break; h += row_h(s->sel); } break;
	case W_TOP: s->sel = 0; s->top = 0; show_sel(); break;
	case W_BOTTOM: s->sel = nrows - 1; show_sel(); break;
	case W_OPEN:
		if (s->kind == S_HOME && query[0] && !nres) inside(query);
		else activate(s->sel);
		return 1;
	case W_INSIDE: if (s->kind == S_HOME) inside(query); return 1;
	case W_CLEAR:
		if (query[0]) { query[0] = 0; s->sel = 0; s->top = 0; home_enter(); }
		else wiki_cmd(W_QUIT);
		return 1;
	case W_DELETE:
		if (s->kind == S_LIST && (s->list == L_RECENT || s->list == L_MARKS) && selectable(s->sel)) {
			int i = rows[s->sel].param;
			if (s->list == L_RECENT) { memmove(ws.recent + i, ws.recent + i + 1, sizeof(ws.recent[0]) * (size_t)(ws.nrecent - i - 1)); ws.nrecent--; }
			else { memmove(ws.marks + i, ws.marks + i + 1, sizeof(ws.marks[0]) * (size_t)(ws.nmarks - i - 1)); ws.nmarks--; }
			wstate_save();
			home_enter();
			toast("Removed");
		}
		return 1;
	default: return 0;
	}
	trace("%s sel=%d top=%d", s->kind == S_HOME ? "home" : "list", s->sel, s->top);
	return 1;
}

static void type_into(uint32_t c)
{
	char u[4];
	int n = u8_enc(c, u);
	size_t l = strlen(query);
	if (l + (size_t)n >= sizeof(query) || (l == 0 && c == ' ')) return;
	memcpy(query + l, u, (size_t)n);
	query[l + (size_t)n] = 0;
	cur_scr()->sel = 0;
	cur_scr()->top = 0;
	home_enter();
}

void home_key(struct nkey k)
{
	struct scr *s = cur_scr();
	int ctx = s->kind == S_HOME ? X_HOME : s->kind == S_TRIAGE ? X_TRIAGE : X_LIST;
	if (s->kind == S_TRIAGE && k.k >= '1' && k.k <= '5' && !(k.mods & (M_CTRL | M_ALT))) {
		int i = (int)(k.k - '1');
		if (selectable(i)) { s->sel = i; activate(i); }
		redraw();
		return;
	}
	int cmd = wcmd_lookup(ctx, k);
	if (s->kind == S_HOME && k.k == NK_BS && !(k.mods & (M_CTRL | M_ALT))) {
		size_t l = strlen(query);
		if (l) {
			l--;
			while (l && ((unsigned char)query[l] & 0xc0) == 0x80) l--;
			query[l] = 0;
			s->sel = 0; s->top = 0;
			home_enter();
		}
		redraw();
		return;
	}
	if (cmd) { if (!home_cmd(cmd)) wiki_cmd(cmd); redraw(); return; }
	if (k.k < 0x110000 && k.k >= ' ' && !(k.mods & (M_CTRL | M_ALT))) {
		if (s->kind != S_HOME) {   /* typing in a list or a question: back to the search box */
			if (k.k == ' ') { redraw(); return; }
			go_home();
			query[0] = 0;
		}
		type_into(k.k);
	}
	redraw();
}

void home_menu(void)
{
	int ctx = cur_ctx();
	struct mitem it[24];
	int n = 0;
	for (const uint8_t *c = wcmd_menu(ctx); *c && n < 24; c++) {
		struct mitem *m = &it[n++];
		memset(m, 0, sizeof(*m));
		m->id = *c;
		scopy(m->label, sizeof(m->label), wcmd_name(*c));
		if (*c == W_SIZE) snprintf(m->label, sizeof(m->label), "Text size: %s", text_size == 0 ? "Small" : text_size == 1 ? "Medium" : "Large");
		if (*c == W_INSIDE && !query[0]) m->off = 1;
		if (*c == W_RANDOM && wiki_pack() < 0) m->off = 1;
		if ((*c == W_GUIDES || *c == W_MANUAL) && survival_pack() < 0) m->off = 1;
		if (*c == W_TRIAGE && triage_pack() < 0) m->off = 1;
		if (*c == W_DELETE && !(cur_scr()->kind == S_LIST && (cur_scr()->list == L_RECENT || cur_scr()->list == L_MARKS))) m->off = 1;
		scopy(m->hint, sizeof(m->hint), wcmd_keys(ctx, *c));
	}
	extern void menu_cmd_done(int id, int idx);
	menu_open(cur_scr()->kind == S_HOME ? "Wiki" : "Wiki", it, n, 0, menu_cmd_done);
}

/* ---- drawing ---- */

static void draw_rows(int y)
{
	struct scr *s = cur_scr();
	for (int i = s->top; i < nrows && y < SCREEN_H; i++) {
		struct row *r = &rows[i];
		int h = row_h(i);
		if (r->kind == RW_HEAD) {
			d_text(F_UISMALL, 8, y + 3, pal.accent, r->text, strlen(r->text));
			y += h;
			continue;
		}
		if (r->kind == RW_NOTE) {
			d_text_fit(F_UI, 10, y + 2, SCREEN_W - 20, pal.dim, r->text);
			y += h;
			continue;
		}
		int sel = i == s->sel;
		if (sel) d_rrect(3, y + 1, SCREEN_W - 9, h - 2, 4, pal.sel_bg);
		uint32_t fg = sel ? pal.sel_fg : (r->colour ? r->colour : pal.fg), dim = sel ? pal.sel_dim : pal.dim;
		int tagw = 0, x = 10;
		if (r->tag_colour) { d_rrect(x - 4, y + 4, 3, h - 8 > 9 ? 9 : h - 8, 1, r->tag_colour); }
		if (r->tag[0]) {
			tagw = text_w(F_UISMALL, r->tag, strlen(r->tag));
			int maxtag = SCREEN_W / 2 - 10;
			if (tagw > maxtag) tagw = maxtag;
			int tx = SCREEN_W - 12 - tagw;
			d_text_fit(F_UISMALL, tx, y + 3, tagw, r->tag_colour && !sel ? r->tag_colour : dim, r->tag);
		}
		int bold = r->action != A_ARTICLE || (r->pack >= 0 && packs[r->pack].memory);
		d_text_fit(bold ? F_UIBOLD : F_UI, x, y + 2, SCREEN_W - 26 - tagw, fg, r->text);
		if (r->sub[0]) {
			if (s->kind == S_LIST && s->list == L_INSIDE && r->sub[0] == ' ' && !r->sub[1] && r->param < nres) {   /* drawn: its snippet now */
				result_snippet(&res[r->param], s->q);
				scopy(r->sub, sizeof(r->sub), res[r->param].snip[0] ? res[r->param].snip : "-");
				memcpy(r->hl, res[r->param].hl, sizeof(r->hl)); r->nhl = res[r->param].nhl;
			}
			uint32_t mark = sel ? (pal.sel_bg & 0xfefefe) / 2 + (0xb08a20 & 0xfefefe) / 2 : pal.mark_bg;
			text_marked(F_UI, x, y + 16, SCREEN_W - 24, dim, mark, r->sub, (const int (*)[2])r->hl, r->nhl);
		}
		if (!sel && i + 1 < nrows && rows[i + 1].kind == RW_ROW && r->sub[0]) d_rect(10, y + h - 1, SCREEN_W - 20, 1, pal.line);
		y += h;
	}
}

static void wrap_text(int f, int x, int y, int w, uint32_t c, const char *s, int max_lines)
{
	size_t n = strlen(s), at = 0;
	for (int line = 0; line < max_lines && at < n; line++) {
		size_t cut = n - at;
		if (text_w(f, s + at, cut) > w) {
			size_t sp = 0;
			for (size_t k = at; k < n; k++) {
				if (s[k] == ' ' && text_w(f, s + at, k - at) <= w) sp = k;
				if (text_w(f, s + at, k - at) > w) break;
			}
			cut = sp > at ? sp - at : u8_whole(s + at, (n - at) / 2);
		}
		d_text(f, x, y + line * font_lh(f), c, s + at, cut);
		at += cut;
		while (at < n && s[at] == ' ') at++;
	}
}

void home_draw(void)
{
	struct scr *s = cur_scr();
	d_rect(0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, pal.bg);
	if (s->kind == S_HOME) {
		int sy = BAR_H + 2;
		d_rrect(4, sy, SCREEN_W - 8, SEARCH_H - 4, 5, pal.code_bg);
		int ix = 10;
		d_frame(ix, sy + 3, 8, 8, 1, pal.dim);
		d_line(ix + 7, sy + 10, ix + 10, sy + 13, 2, pal.dim);
		int qx = ix + 16, maxw = SCREEN_W - qx - 12;
		if (query[0]) {
			const char *q = query;
			while (*q && text_w(F_UI, q, strlen(q)) > maxw) q++;   /* a long query: its end shows */
			while (((unsigned char)*q & 0xc0) == 0x80) q++;
			int qw = d_text(F_UI, qx, sy + 1, pal.fg, q, strlen(q));
			d_rect(qx + qw + 1, sy + 2, 1, 12, pal.accent);
		} else {
			d_rect(qx, sy + 2, 1, 12, pal.accent);
			d_text(F_UI, qx + 4, sy + 1, pal.dim, "Search", 6);
		}
		draw_rows(area_y());
		char t[64];
		if (query[0]) snprintf(t, sizeof(t), "Wiki · %d found", nres);
		else snprintf(t, sizeof(t), "Wiki");
		bar_draw(t, "", pal.dim);
		return;
	}
	if (s->kind == S_TRIAGE) {
		const struct tri_node *t = triage_node(s->node);
		char q[160] = "", warn[120] = "";
		if (t) {
			scopy(q, sizeof(q), t->q);
			char *cut = strstr(q, "? ");
			if (cut && !strcmp(s->node, "root")) { scopy(warn, sizeof(warn), cut + 2); cut[1] = 0; }
		}
		int y = BAR_H + 3;
		wrap_text(F_UIBOLD, 8, y, SCREEN_W - 16, pal.fg, q, 2);
		y += font_lh(F_UIBOLD) * (text_w(F_UIBOLD, q, strlen(q)) > SCREEN_W - 16 ? 2 : 1) + 1;
		if (warn[0]) { d_text_fit(F_UISMALL, 8, y, SCREEN_W - 16, pal.warn, warn); y += font_lh(F_UISMALL) + 2; }
		s->top = 0;
		draw_rows(y + 1);
		bar_draw("What is happening?", "1-5", pal.dim);
		return;
	}
	draw_rows(area_y());
	char t[240];
	int sp = survival_pack();
	const struct category *c = s->list == L_CATEGORY && sp >= 0 ? pack_category(sp, s->param) : NULL;
	switch (s->list) {
	case L_CATEGORIES: snprintf(t, sizeof(t), "Survival guides"); break;
	case L_CATEGORY: scopy(t, sizeof(t), c ? c->name : "Guides"); break;
	case L_CHAPTERS: snprintf(t, sizeof(t), "Army Survival Manual"); break;
	case L_RECENT: snprintf(t, sizeof(t), "Recently read"); break;
	case L_MARKS: snprintf(t, sizeof(t), "Bookmarks"); break;
	default: snprintf(t, sizeof(t), "“%s” inside · %d", s->q, nres);
	}
	bar_draw(t, "", pal.dim);
}
