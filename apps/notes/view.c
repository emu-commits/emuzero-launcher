// SPDX-License-Identifier: MIT
// Laying out and drawing a note, in Edit mode (the text as typed, lightly styled) or View mode
// (the Markdown drawn, its marks hidden).
//
// Each line (paragraph) becomes a list of spans over its bytes: text in a font and colour,
// hidden marks (no width), a checkbox or a bullet drawn in place of the bytes it covers. The
// spans are wrapped into rows by the fonts' real glyph widths; the rows are kept per line and
// rebuilt only when that line changes (or the size, width or mode does), and only the lines on
// screen are ever laid out, so a long note costs no more per key than a short one.
#include "notes.h"
#include <stdlib.h>
#include <string.h>

enum { SK_TEXT, SK_HIDE, SK_BOX, SK_GLYPH };
enum { SF_DIM = 1, SF_ACCENT = 2, SF_STRIKE = 4, SF_UNDER = 8, SF_CODEBG = 16, SF_ITALDIM = 32 };
struct span { uint32_t off, len; uint8_t kind, font, flags; int16_t w; const char *glyph; };
struct pstyle {
	int kind, lh, asc, font, first_x, hang_x, pad_top, pad_bot, ticked, quote, rule, codebg, blank_h, title;
	struct linfo li;
};
struct play {
	uint32_t ver, gen; int mode, w, plain;
	struct pstyle ps;
	int n, h;
	struct vrow r[];
};

static uint32_t gen = 1;
void view_invalidate_all(void) { gen++; }

static struct span *sp;
static int nsp, csp;

static void add(uint32_t off, uint32_t len, int kind, int f, int flags, int w, const char *glyph)
{
	if (!len && kind == SK_TEXT) return;
	if (nsp == csp) { csp = csp ? csp * 2 : 64; sp = realloc(sp, (size_t)csp * sizeof(*sp)); }
	sp[nsp++] = (struct span){ off, len, (uint8_t)kind, (uint8_t)f, (uint8_t)flags, (int16_t)w, glyph };
}

static int box_size(int lh) { int s = lh - 5; return s > 15 ? 15 : s < 8 ? 8 : s; }

struct ictx { int base, flags; };
static void emit_inline(void *u, size_t off, size_t len, int style, int hidden)
{
	struct ictx *c = u;
	if (hidden) { add((uint32_t)off, (uint32_t)len, SK_HIDE, c->base, 0, 0, NULL); return; }
	int f = c->base, fl = c->flags;
	if (c->base == F_REG) {
		if ((style & (ST_BOLD | ST_ITAL)) == (ST_BOLD | ST_ITAL)) f = F_BOLDITAL;
		else if (style & ST_BOLD) f = F_BOLD;
		else if (style & ST_ITAL) f = F_ITAL;
	}
	if (style & ST_CODE) { f = F_MONO; fl |= SF_CODEBG; }
	if (style & ST_STRIKE) fl |= SF_STRIKE;
	if (style & ST_LINK) fl |= SF_ACCENT | SF_UNDER;
	add((uint32_t)off, (uint32_t)len, SK_TEXT, f, fl, 0, NULL);
}

static const char *bullet_for(int depth) { static const char *b[3] = { "•", "◦", "▪" }; return b[depth % 3]; }

/* the spans of line i, and how the line is drawn */
static void build(struct doc *d, int i, int mode, int plain, struct pstyle *ps)
{
	size_t n;
	const char *s = doc_para_text(d, i, &n);
	struct linfo li;
	line_info(s, n, d->p[i].code, &li);
	nsp = 0;
	memset(ps, 0, sizeof(*ps));
	ps->li = li;
	ps->kind = li.kind;
	int title = !plain && i == 0 && (li.kind == LK_TEXT || li.kind == LK_HEAD);
	ps->title = title;
	int base = F_REG;
	if (li.kind == LK_HEAD) base = li.level == 1 ? F_H1 : li.level == 2 ? F_H2 : F_H3;
	else if (title) base = F_H2;
	else if (li.kind == LK_CODE || li.kind == LK_FENCE) base = F_MONO;
	ps->font = base;
	ps->lh = font_lh(base == F_MONO ? F_REG : base);
	ps->asc = font_asc(base == F_MONO ? F_REG : base);
	uint32_t lead = (uint32_t)li.lead, ml = (uint32_t)li.mlen;

	if (mode == MODE_EDIT) {
		switch (li.kind) {
		case LK_HEAD:
			add(0, lead, SK_TEXT, base, SF_DIM, 0, NULL);
			add(lead, ml, SK_TEXT, base, SF_DIM, 0, NULL);
			add(lead + ml, (uint32_t)n - lead - ml, SK_TEXT, base, title ? 0 : SF_ACCENT, 0, NULL);
			break;
		case LK_TASK: {
			int bw = box_size(ps->lh) + 5;
			add(0, lead, SK_TEXT, base, 0, 0, NULL);
			add(lead, ml, SK_BOX, base, li.checked, bw, NULL);
			ps->hang_x = 0;   /* set below from the box's x */
			ps->ticked = li.checked;
			add(lead + ml, (uint32_t)n - lead - ml, SK_TEXT, base, li.checked ? SF_DIM | SF_STRIKE : 0, 0, NULL);
			ps->hang_x = text_w(base, s, lead) + bw;
			break;
		}
		case LK_BULLET: case LK_NUM: case LK_QUOTE:
			add(0, lead + ml, SK_TEXT, base, SF_DIM, 0, NULL);
			add(lead + ml, (uint32_t)n - lead - ml, SK_TEXT, base, li.kind == LK_QUOTE ? SF_ITALDIM : 0, 0, NULL);
			ps->hang_x = text_w(base, s, lead + ml);
			break;
		case LK_CODE: case LK_FENCE:
			add(0, (uint32_t)n, SK_TEXT, F_MONO, li.kind == LK_FENCE ? SF_DIM : 0, 0, NULL);
			ps->codebg = 1;
			break;
		case LK_RULE:
			add(0, (uint32_t)n, SK_TEXT, base, SF_DIM, 0, NULL);
			break;
		default:
			add(0, (uint32_t)n, SK_TEXT, base, 0, 0, NULL);
		}
		ps->first_x = 0;
		return;
	}

	/* View mode */
	struct ictx c = { base, 0 };
	int indent = (int)(lead / 2) * 14;
	switch (li.kind) {
	case LK_BLANK:
		add(0, (uint32_t)n, SK_HIDE, base, 0, 0, NULL);
		ps->blank_h = ps->lh / 2;
		break;
	case LK_FENCE:
		add(0, (uint32_t)n, SK_HIDE, base, 0, 0, NULL);
		ps->blank_h = 4;
		ps->codebg = 1;
		break;
	case LK_CODE:
		add(0, (uint32_t)n, SK_TEXT, F_MONO, 0, 0, NULL);
		ps->codebg = 1;
		ps->first_x = ps->hang_x = 4;
		break;
	case LK_RULE:
		add(0, (uint32_t)n, SK_HIDE, base, 0, 0, NULL);
		ps->rule = 1;
		ps->blank_h = ps->lh / 2 + 2;
		break;
	case LK_HEAD:
		add(0, lead + ml, SK_HIDE, base, 0, 0, NULL);
		md_inline(s + lead + ml, n - lead - ml, lead + ml, 0, emit_inline, &c);
		ps->pad_top = i ? ps->lh / 4 : 0;
		ps->pad_bot = 2;
		break;
	case LK_TASK: {
		int bw = box_size(ps->lh) + 6;
		add(0, lead, SK_HIDE, base, 0, 0, NULL);
		add(lead, ml, SK_BOX, base, li.checked, bw, NULL);
		ps->ticked = li.checked;
		c.flags = li.checked ? SF_DIM | SF_STRIKE : 0;
		md_inline(s + lead + ml, n - lead - ml, lead + ml, 0, emit_inline, &c);
		ps->first_x = indent;
		ps->hang_x = indent + bw;
		break;
	}
	case LK_BULLET: {
		const char *g = bullet_for((int)lead / 2);
		int gw = text_w(F_REG, g, strlen(g)) + 6;
		if (gw < 12) gw = 12;
		add(0, lead, SK_HIDE, base, 0, 0, NULL);
		add(lead, ml, SK_GLYPH, F_REG, SF_DIM, gw, g);
		md_inline(s + lead + ml, n - lead - ml, lead + ml, 0, emit_inline, &c);
		ps->first_x = indent + 2;
		ps->hang_x = indent + 2 + gw;
		break;
	}
	case LK_NUM: {
		add(0, lead, SK_HIDE, base, 0, 0, NULL);
		add(lead, ml, SK_TEXT, base, SF_DIM, 0, NULL);
		md_inline(s + lead + ml, n - lead - ml, lead + ml, 0, emit_inline, &c);
		ps->first_x = indent + 2;
		ps->hang_x = indent + 2 + text_w(base, s + lead, ml);
		break;
	}
	case LK_QUOTE:
		add(0, lead + ml, SK_HIDE, base, 0, 0, NULL);
		c.base = F_ITAL;
		c.flags = SF_DIM;
		md_inline(s + lead + ml, n - lead - ml, lead + ml, 0, emit_inline, &c);
		ps->quote = 1;
		ps->first_x = ps->hang_x = indent + 10;
		break;
	default:
		if (lead) add(0, lead, SK_HIDE, base, 0, 0, NULL);
		md_inline(s + lead, n - lead, lead, 0, emit_inline, &c);
		ps->first_x = ps->hang_x = indent;
		if (title) ps->pad_bot = 3;
	}
}


static struct vrow *rows;
static int crows;

static void row_push(int *nr, uint32_t at, int x0, int lh)
{
	if (*nr == crows) { crows = crows ? crows * 2 : 64; rows = realloc(rows, (size_t)crows * sizeof(*rows)); }
	rows[(*nr)++] = (struct vrow){ at, (int16_t)x0, (int16_t)lh };
}

/* wrap the spans into rows (in the static rows array) at width w */
static int wrap(const char *s, const struct pstyle *ps, int w)
{
	int nr = 0, x = ps->first_x, row_x0 = ps->first_x;
	uint32_t row_start = 0, brk = 0;
	int x_brk = 0, have_brk = 0;
	nr = 0;
	row_push(&nr, 0, ps->first_x, ps->lh);
#define NEWROW(at, nx) do { row_push(&nr, (at), ps->hang_x, ps->lh); \
		row_start = (at); row_x0 = ps->hang_x; x = (nx); have_brk = 0; } while (0)
	for (int k = 0; k < nsp; k++) {
		const struct span *q = &sp[k];
		if (q->kind == SK_HIDE) continue;
		if (q->kind != SK_TEXT) {
			if (x + q->w > w && x > row_x0) NEWROW(q->off, ps->hang_x);
			x += q->w;
			brk = q->off + q->len; x_brk = x; have_brk = 1;
			continue;
		}
		size_t i = q->off, e = q->off + q->len;
		while (i < e) {
			size_t j = i;
			uint32_t c = u8_dec(s, e, &j);
			size_t jj = j;
			uint32_t nx = j < e ? u8_dec(s, e, &jj) : 0;
			int cw = glyph_w(q->font, c, nx);
			if (c == ' ') { x += cw; brk = (uint32_t)j; x_brk = x; have_brk = 1; i = j; continue; }
			if (x + cw > w && x > row_x0) {
				if (have_brk && brk > row_start) NEWROW(brk, ps->hang_x + (x - x_brk));
				else NEWROW((uint32_t)i, ps->hang_x);
			}
			x += cw;
			i = j;
		}
	}
#undef NEWROW
	return nr;
}

static struct play *layout(struct doc *d, int i, int mode, int w, int plain)
{
	struct play *p = d->p[i].lay;
	if (p && p->ver == d->p[i].ver && p->gen == gen && p->mode == mode && p->w == w && p->plain == plain) return p;
	free(p);
	struct pstyle ps;
	build(d, i, mode, plain, &ps);
	size_t n;
	const char *s = doc_para_text(d, i, &n);
	(void)n;
	int nr = wrap(s, &ps, w);
	p = malloc(sizeof(*p) + (size_t)nr * sizeof(struct vrow));
	p->ver = d->p[i].ver; p->gen = gen; p->mode = mode; p->w = w; p->plain = plain;
	p->ps = ps;
	p->n = nr;
	memcpy(p->r, rows, (size_t)nr * sizeof(*rows));
	if (ps.blank_h) { p->n = 1; p->r[0].h = (int16_t)ps.blank_h; }
	p->h = ps.pad_top + ps.pad_bot;
	for (int k = 0; k < p->n; k++) p->h += p->r[k].h;
	d->p[i].lay = p;
	return p;
}

int view_rows(struct doc *d, int i, int mode, int w, int plain, const struct vrow **rows)
{
	struct play *p = layout(d, i, mode, w, plain);
	*rows = p->r;
	return p->n;
}

int view_para_h(struct doc *d, int i, int mode, int w, int plain) { return layout(d, i, mode, w, plain)->h; }

/* a row's height with the line's spacing above its first row and below its last */
int view_row_h(struct doc *d, int i, int r, int mode, int w, int plain)
{
	struct play *p = layout(d, i, mode, w, plain);
	if (r >= p->n) r = p->n - 1;
	return p->r[r].h + (r == 0 ? p->ps.pad_top : 0) + (r == p->n - 1 ? p->ps.pad_bot : 0);
}

int view_nrows(struct doc *d, int i, int mode, int w, int plain) { return layout(d, i, mode, w, plain)->n; }

/* the row of line i that holds byte rel (0..len) */
static int row_of(const struct play *p, uint32_t rel)
{
	int r = 0;
	while (r + 1 < p->n && p->r[r + 1].start <= rel) r++;
	return r;
}

static uint32_t row_end(const struct play *p, int r, size_t len) { return r + 1 < p->n ? p->r[r + 1].start : (uint32_t)len; }

void view_pos(struct doc *d, int mode, int w, int plain, size_t pos, int *para, int *row, int *x)
{
	int i = doc_para_at(d, pos);
	struct play *p = layout(d, i, mode, w, plain);
	build(d, i, mode, plain, &p->ps);
	size_t n;
	const char *s = doc_para_text(d, i, &n);
	uint32_t rel = (uint32_t)(pos - d->p[i].start);
	int r = row_of(p, rel);
	uint32_t a = p->r[r].start;
	int xx = p->r[r].x0;
	for (int k = 0; k < nsp && a < rel; k++) {
		const struct span *q = &sp[k];
		uint32_t s0 = q->off > a ? q->off : a, s1 = q->off + q->len < rel ? q->off + q->len : rel;
		if (q->off + q->len <= a || s0 >= s1) continue;
		if (q->kind == SK_TEXT) xx += text_w(q->font, s + s0, s1 - s0);
		else if (q->kind != SK_HIDE && s1 == q->off + q->len && s0 == q->off) xx += q->w;
	}
	*para = i; *row = r; *x = xx;
}

size_t view_hit(struct doc *d, int mode, int w, int plain, int i, int r, int x)
{
	struct play *p = layout(d, i, mode, w, plain);
	struct pstyle ps;
	build(d, i, mode, plain, &ps);
	size_t n;
	const char *s = doc_para_text(d, i, &n);
	if (r >= p->n) r = p->n - 1;
	uint32_t a = p->r[r].start, e = row_end(p, r, n);
	/* the last row's end is the line's end; other rows end before their first byte of the next */
	int xx = p->r[r].x0;
	uint32_t best = a;
	int bestd = abs(x - xx);
	for (int k = 0; k < nsp; k++) {
		const struct span *q = &sp[k];
		if (q->off + q->len <= a || q->off >= e) continue;
		if (q->kind == SK_HIDE) continue;
		if (q->kind != SK_TEXT) {
			xx += q->w;
			uint32_t pos = q->off + q->len;
			if (abs(x - xx) < bestd) { bestd = abs(x - xx); best = pos; }
			continue;
		}
		size_t i2 = q->off > a ? q->off : a, end = q->off + q->len < e ? q->off + q->len : e;
		while (i2 < end) {
			size_t j = i2;
			uint32_t c = u8_dec(s, end, &j);
			size_t jj = j;
			uint32_t nx = j < end ? u8_dec(s, end, &jj) : 0;
			xx += glyph_w(q->font, c, nx);
			if (abs(x - xx) < bestd) { bestd = abs(x - xx); best = (uint32_t)j; }
			i2 = j;
		}
	}
	/* a soft break: the row's last position is before the space that ended it */
	if (r + 1 < p->n && best == e && e > a && s[e - 1] == ' ') best = e - 1;
	return view_snap(d, d->p[i].start + best, 1);
}

size_t view_snap(struct doc *d, size_t pos, int dir)
{
	int i = doc_para_at(d, pos);
	size_t n;
	const char *s = doc_para_text(d, i, &n);
	struct linfo li;
	line_info(s, n, d->p[i].code, &li);
	if (li.kind != LK_TASK) return pos;
	size_t bs = d->p[i].start + (size_t)li.lead, be = bs + (size_t)li.mlen;
	if (pos < bs || pos >= be) return pos;
	return dir < 0 && bs > 0 ? bs - 1 : be;
}

/* ---- drawing ---- */

static uint32_t span_color(int flags, const struct pstyle *ps)
{
	(void)ps;
	if (flags & SF_ACCENT) return pal.accent;
	if (flags & (SF_DIM | SF_ITALDIM)) return pal.dim;
	return pal.fg;
}

static void draw_bg_range(const char *s, const struct play *p, int r, int rx, int ry, int rh, size_t a0, size_t a1, uint32_t color, int extend)
{
	(void)p;
	uint32_t a = p->r[r].start;
	int xx = rx + p->r[r].x0;
	uint32_t e = r + 1 < p->n ? p->r[r + 1].start : UINT32_MAX;
	int x0 = -1, x1 = -1;
	for (int k = 0; k < nsp; k++) {
		const struct span *q = &sp[k];
		if (q->off + q->len <= a || q->off >= e || q->kind == SK_HIDE) continue;
		uint32_t s0 = q->off > a ? q->off : a, s1 = q->off + q->len < e ? q->off + q->len : e;
		int sw = q->kind == SK_TEXT ? text_w(q->font, s + s0, s1 - s0) : q->w;
		uint32_t c0 = s0 > a0 ? s0 : (uint32_t)a0, c1 = s1 < a1 ? s1 : (uint32_t)a1;
		if (c0 < c1) {
			int px0 = xx, px1 = xx + sw;
			if (q->kind == SK_TEXT) {
				px0 = xx + text_w(q->font, s + s0, c0 - s0);
				px1 = xx + text_w(q->font, s + s0, c1 - s0);
			}
			if (x0 < 0) x0 = px0;
			x1 = px1;
		}
		xx += sw;
	}
	if (extend && a1 > e && x0 < 0) x0 = xx;   /* the line break is selected too */
	if (x0 < 0) return;
	if (extend && a1 > e) x1 = xx + 4;
	d_rect(x0, ry, x1 - x0, rh, color);
}

void view_draw(struct doc *d, const struct vdraw *v)
{
	int y = v->y, bottom = v->y + v->h;
	int cur_para = -1, cur_row = 0, cur_x = 0;
	if (v->show_cur) view_pos(d, v->mode, v->w, v->plain, v->cur, &cur_para, &cur_row, &cur_x);
	for (int i = v->top_para; i < d->np && y < bottom; i++) {
		struct play *p = layout(d, i, v->mode, v->w, v->plain);
		struct pstyle ps;
		build(d, i, v->mode, v->plain, &ps);
		size_t n;
		const char *s = doc_para_text(d, i, &n);
		int r0 = i == v->top_para ? v->top_row : 0;
		if (r0 >= p->n) r0 = p->n - 1;
		int py = y + (r0 == 0 ? ps.pad_top : 0);
		int para_y0 = py;
		int ph = 0;
		for (int r = r0; r < p->n; r++) ph += p->r[r].h;
		if (v->focus == i) {
			d_rect(v->x - 4, para_y0 - 1, v->w + 6, ph + 2, pal.focus);
			d_rect(v->x - 4, para_y0 - 1, 2, ph + 2, pal.accent);
		}
		if (ps.codebg && d_clip_rows(py, ph)) d_rect(v->x - 2, py, v->w + 4, ph, pal.code_bg);
		if (ps.quote && d_clip_rows(py, ph)) d_rect(v->x + ps.first_x - 8, py + 1, 3, ph - 2, pal.line == pal.bg ? pal.dim : pal.line);
		if (ps.rule && d_clip_rows(py, ph)) d_rect(v->x, py + ph / 2, v->w, 1, pal.dim);
		size_t pstart = d->p[i].start;
		for (int r = r0; r < p->n && py < bottom; r++) {
			int rh = p->r[r].h;
			if (!d_clip_rows(py, rh) || ps.blank_h) {
				if (ps.blank_h && v->s0 < v->s1 && v->s0 <= pstart + n && v->s1 > pstart + n && v->mode == MODE_EDIT)
					d_rect(v->x, py, 4, rh, pal.text_sel);
				py += rh; continue;
			}
			uint32_t a = p->r[r].start, e = r + 1 < p->n ? p->r[r + 1].start : (uint32_t)n;
			/* backgrounds: find matches, then the selection */
			for (int m = 0; m < v->nmarks; m++) {
				size_t m0 = v->marks[2 * m], m1 = v->marks[2 * m + 1];
				if (m1 <= pstart + a || m0 >= pstart + e + (r + 1 == p->n)) continue;
				draw_bg_range(s, p, r, v->x, py, rh, m0 > pstart ? m0 - pstart : 0, m1 - pstart, pal.mark_bg, 0);
			}
			if (v->s0 < v->s1 && v->s1 > pstart + a && v->s0 <= pstart + e)
				draw_bg_range(s, p, r, v->x, py, rh, v->s0 > pstart ? v->s0 - pstart : 0, v->s1 - pstart, pal.text_sel, r + 1 == p->n);
			/* the text */
			int xx = v->x + p->r[r].x0;
			for (int k = 0; k < nsp; k++) {
				const struct span *q = &sp[k];
				if (q->off + q->len <= a || q->off >= e || q->kind == SK_HIDE) continue;
				if (q->kind == SK_BOX) {
					int bs = box_size(ps.lh);
					d_check(xx + 1, py + (rh - bs) / 2, bs, q->flags);
					xx += q->w;
					continue;
				}
				if (q->kind == SK_GLYPH) {
					d_text(q->font, xx, py + ps.asc - font_asc(q->font), pal.dim, q->glyph, strlen(q->glyph));
					xx += q->w;
					continue;
				}
				uint32_t s0 = q->off > a ? q->off : a, s1 = q->off + q->len < e ? q->off + q->len : e;
				int sw = text_w(q->font, s + s0, s1 - s0);
				int ty = py + ps.asc - font_asc(q->font);
				if (q->flags & SF_CODEBG) d_rect(xx - 1, py + 1, sw + 2, rh - 2, pal.code_bg);
				d_text_deco(q->font, xx, ty, span_color(q->flags, &ps), s + s0, s1 - s0, (q->flags & SF_STRIKE) != 0, (q->flags & SF_UNDER) != 0);
				xx += sw;
			}
			if (v->show_cur && cur_para == i && cur_row == r) {
				int cw = text_size == 2 ? 2 : 1;
				d_rect(v->x + cur_x, py + 1, cw + 1, rh - 2, pal.accent);
			}
			py += rh;
		}
		y = py + ps.pad_bot;
	}
}
