// SPDX-License-Identifier: MIT
// The note being edited: its text, the paragraph (line) index, and undo/redo.
//
// Undo keeps a log of edits (inserted or deleted bytes), each tagged with a group. Typing
// merges into the last group until a word ends, the cursor jumps or two seconds pass, so
// Ctrl+Z takes back a word at a time; a command that makes several edits (a checklist toggle,
// moving a line) is one group. The log is capped at 2000 edits / 1 MiB of text.
#include "notes.h"
#include <stdlib.h>
#include <string.h>

#define UNDO_MAX_OPS 2000
#define UNDO_MAX_BYTES (1u << 20)

void doc_init(struct doc *d) { memset(d, 0, sizeof(*d)); tb_init(&d->tb); doc_reindex(d); }

static void free_layouts(struct doc *d)
{
	for (int i = 0; i < d->np; i++) free(d->p[i].lay);
}

static void undo_clear(struct doc *d, int from)
{
	for (int i = from; i < d->utop; i++) free(d->u[i].s);
	d->utop = from;
	if (d->nu > from) d->nu = from;
}

void doc_free(struct doc *d)
{
	free_layouts(d);
	free(d->p);
	undo_clear(d, 0);
	free(d->u);
	tb_free(&d->tb);
	memset(d, 0, sizeof(*d));
}

void doc_load(struct doc *d, const char *s, size_t n)
{
	doc_free(d);
	tb_init(&d->tb);
	tb_insert(&d->tb, 0, s, n);
	doc_reindex(d);
}

static int is_fence(struct doc *d, size_t start, size_t len)
{
	size_t i = 0;
	while (i < len && i < 3 && tb_at(&d->tb, start + i) == ' ') i++;
	if (len - i < 3) return 0;
	char c = tb_at(&d->tb, start + i);
	return (c == '`' || c == '~') && tb_at(&d->tb, start + i + 1) == c && tb_at(&d->tb, start + i + 2) == c;
}

/* which lines are inside a ``` block; a line whose state changes is laid out again */
static void mark_code(struct doc *d)
{
	int in = 0;
	for (int i = 0; i < d->np; i++) {
		struct para *p = &d->p[i];
		int f = is_fence(d, p->start, p->len);
		uint8_t c = (uint8_t)(in ? (f ? 2 : 1) : 0);   /* 0 outside, 1 inside, 2 the closing fence */
		if (f) in = !in;
		if (p->code != c) { p->code = c; p->ver = ++d->ver; }
	}
}

void doc_reindex(struct doc *d)
{
	free_layouts(d);
	size_t n = tb_len(&d->tb), start = 0;
	d->np = 0;
	for (size_t i = 0; i <= n; i++) {
		if (i < n && tb_at(&d->tb, i) != '\n') continue;
		if (d->np == d->pcap) {
			d->pcap = d->pcap ? d->pcap * 2 : 64;
			d->p = realloc(d->p, (size_t)d->pcap * sizeof(*d->p));
		}
		d->p[d->np++] = (struct para){ .start = start, .len = i - start, .ver = ++d->ver };
		start = i + 1;
	}
	mark_code(d);
}

int doc_para_at(const struct doc *d, size_t pos)
{
	int lo = 0, hi = d->np - 1;
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (d->p[mid].start <= pos) lo = mid; else hi = mid - 1;
	}
	return lo;
}

const char *doc_para_text(struct doc *d, int i, size_t *n)
{
	*n = d->p[i].len;
	return tb_range(&d->tb, d->p[i].start, d->p[i].start + d->p[i].len);
}

/* keep the index right after an edit: a fast path when no line break comes or goes */
static void index_edit(struct doc *d, size_t pos, size_t del, int del_nl, const char *ins, size_t n)
{
	if (!del_nl && !memchr(ins, '\n', n)) {
		int i = doc_para_at(d, pos);
		d->p[i].len = d->p[i].len - del + n;
		d->p[i].ver = ++d->ver;
		for (int k = i + 1; k < d->np; k++) d->p[k].start = d->p[k].start - del + n;
		mark_code(d);
	} else doc_reindex(d);
}

static void raw_edit(struct doc *d, size_t pos, size_t del, const char *ins, size_t n)
{
	int del_nl = 0;
	for (size_t i = 0; i < del && !del_nl; i++) del_nl = tb_at(&d->tb, pos + i) == '\n';
	tb_delete(&d->tb, pos, del);
	tb_insert(&d->tb, pos, ins, n);
	index_edit(d, pos, del, del_nl, ins, n);
	d->dirty = 1;
	d->changes++;
}

static void log_op(struct doc *d, int ins, size_t pos, const char *s, size_t n)
{
	undo_clear(d, d->nu);   /* a new edit drops what could be redone */
	if (d->nu == d->ucap) {
		d->ucap = d->ucap ? d->ucap * 2 : 64;
		d->u = realloc(d->u, (size_t)d->ucap * sizeof(*d->u));
	}
	char *c = malloc(n ? n : 1);
	memcpy(c, s, n);
	d->u[d->nu++] = (struct undo_op){ (uint8_t)ins, pos, c, n, d->group };
	d->utop = d->nu;
	d->ubytes += n;
	/* drop the oldest whole groups past the cap */
	while (d->nu > 1 && (d->nu > UNDO_MAX_OPS || d->ubytes > UNDO_MAX_BYTES)) {
		uint32_t g = d->u[0].group;
		int k = 0;
		while (k < d->nu - 1 && d->u[k].group == g) { d->ubytes -= d->u[k].n; free(d->u[k].s); k++; }
		if (!k) break;
		memmove(d->u, d->u + k, (size_t)(d->nu - k) * sizeof(*d->u));
		d->nu -= k; d->utop -= k;
	}
}

void doc_group(struct doc *d) { d->merge = 0; }
void doc_batch(struct doc *d, int on)
{
	if (on && !d->batch++) { d->group++; d->merge = 0; }
	else if (!on && d->batch) d->batch--;
}

static int word_char(char c) { return c != ' ' && c != '\n' && c != '\t'; }

void doc_edit(struct doc *d, size_t pos, size_t del, const char *ins, size_t n, int merge, uint32_t now)
{
	if (!del && !n) return;
	struct undo_op *last = d->nu && d->nu == d->utop ? &d->u[d->nu - 1] : NULL;
	int merged = 0;
	if (merge && d->merge && last && !d->batch && now - d->last_ms < 2000) {
		if (!del && n && last->ins && pos == last->pos + last->n &&
		    !(last->n && !word_char(last->s[last->n - 1]) && word_char(ins[0]))) {
			/* typing on: append to the last insert */
			last->s = realloc(last->s, last->n + n);
			memcpy(last->s + last->n, ins, n);
			last->n += n; d->ubytes += n;
			raw_edit(d, pos, 0, ins, n);
			merged = 1;
		} else if (del && !n && !last->ins && (pos + del == last->pos || pos == last->pos)) {
			/* Backspace (or Delete) held: grow the last deletion */
			char *s = tb_copy(&d->tb, pos, pos + del);
			char *c = malloc(last->n + del);
			if (pos + del == last->pos) { memcpy(c, s, del); memcpy(c + del, last->s, last->n); last->pos = pos; }
			else { memcpy(c, last->s, last->n); memcpy(c + last->n, s, del); }
			free(last->s); free(s);
			last->s = c; last->n += del; d->ubytes += del;
			raw_edit(d, pos, del, "", 0);
			merged = 1;
		}
	}
	if (!merged) {
		if (!d->batch) d->group++;
		if (del) { char *s = tb_copy(&d->tb, pos, pos + del); log_op(d, 0, pos, s, del); free(s); }
		if (n) log_op(d, 1, pos, ins, n);
		raw_edit(d, pos, del, ins, n);
	}
	d->merge = merge && !d->batch;
	d->last_ms = now;
}

/* undo the last group; the cursor goes where the change was */
int doc_undo(struct doc *d, size_t *cur)
{
	if (!d->nu) return 0;
	uint32_t g = d->u[d->nu - 1].group;
	while (d->nu && d->u[d->nu - 1].group == g) {
		struct undo_op *o = &d->u[--d->nu];
		if (o->ins) { raw_edit(d, o->pos, o->n, "", 0); *cur = o->pos; }
		else { raw_edit(d, o->pos, 0, o->s, o->n); *cur = o->pos + o->n; }
	}
	d->merge = 0;
	return 1;
}

int doc_redo(struct doc *d, size_t *cur)
{
	if (d->nu >= d->utop) return 0;
	uint32_t g = d->u[d->nu].group;
	while (d->nu < d->utop && d->u[d->nu].group == g) {
		struct undo_op *o = &d->u[d->nu++];
		if (o->ins) { raw_edit(d, o->pos, 0, o->s, o->n); *cur = o->pos + o->n; }
		else { raw_edit(d, o->pos, o->n, "", 0); *cur = o->pos; }
	}
	d->merge = 0;
	return 1;
}

char *doc_text(struct doc *d, size_t *n)
{
	*n = tb_len(&d->tb);
	return tb_copy(&d->tb, 0, *n);
}
