// SPDX-License-Identifier: MIT
// Search for the list: every word of the query must appear (in any order) in the title or the
// text; case and accents are ignored. Title matches rank first, then newer notes. Each hit
// carries the line that matched, with the matching words marked.
#define _GNU_SOURCE
#include "notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* lower case, Latin-1 accents to their letter; one byte out per byte in, so offsets line up
 * (an accented letter's second byte becomes a 0x01 filler that never matches a query) */
size_t fold_text(const char *s, size_t n, char *out)
{
	static const char lat[64] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";   /* U+00E0.. lower half */
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 'A' && c <= 'Z') out[i] = (char)(c + 32);
		else if (c == 0xc3 && i + 1 < n) {
			unsigned char d = (unsigned char)s[i + 1];
			int k = (d & 0x1f);   /* 0x80-0x9f upper, 0xa0-0xbf lower: same letters */
			char l = (d >= 0x80 && d <= 0xbf) ? lat[k] : 0;
			if (l && l != '/') { out[i] = l; out[i + 1] = 1; i++; }
			else { out[i] = (char)c; }
		}
		else out[i] = (char)c;
	}
	out[n] = 0;
	return n;
}

static void load_body(struct note *n)
{
	if (n->body && n->body_mtime == n->mtime && n->body_size == n->size) return;
	free(n->body); free(n->fold);
	n->body = n->fold = NULL;
	char *s; size_t len;
	if (store_read(n->file, &s, &len)) { s = strdup(""); len = 0; }
	n->body = s; n->blen = len;
	n->fold = malloc(len + 1);
	fold_text(s, len, n->fold);
	n->body_mtime = n->mtime; n->body_size = n->size;
}

static const char *find(const char *hay, size_t hn, const char *w, size_t wn)
{
	if (!wn || wn > hn) return NULL;
	return memmem(hay, hn, w, wn);
}

static int by_score(const void *a, const void *b)
{
	const struct hit *x = a, *y = b;
	if (x->score != y->score) return y->score - x->score;
	return notes[y->note].mtime > notes[x->note].mtime ? 1 : notes[y->note].mtime < notes[x->note].mtime ? -1 : 0;
}

int search_run(const char *query, struct hit *out, int max)
{
	char q[256], words[8][64];
	size_t wl[8];
	int nw = 0;
	size_t ql = strlen(query);
	if (ql > sizeof(q) - 1) ql = sizeof(q) - 1;
	fold_text(query, ql, q);
	for (char *s = q; *s && nw < 8;) {
		while (*s == ' ') s++;
		if (!*s) break;
		size_t l = strcspn(s, " ");
		if (l > 63) l = 63;
		memcpy(words[nw], s, l); words[nw][l] = 0;
		/* drop the filler bytes from a folded accent */
		size_t o = 0;
		for (size_t k = 0; k < l; k++) if (words[nw][k] != 1) words[nw][o++] = words[nw][k];
		words[nw][o] = 0;
		wl[nw++] = o;
		s += l;
	}
	int nh = 0;
	for (int i = 0; i < nnotes && nh < max; i++) {
		struct note *n = &notes[i];
		load_body(n);
		char ft[128];
		size_t tl = strlen(n->title);
		fold_text(n->title, tl, ft);
		int ok = 1, title_hits = 0;
		for (int w = 0; w < nw && ok; w++) {
			int in_title = find(ft, tl, words[w], wl[w]) != NULL;
			title_hits += in_title;
			ok = in_title || find(n->fold, n->blen, words[w], wl[w]);
		}
		if (!ok) continue;
		struct hit *h = &out[nh++];
		memset(h, 0, sizeof(*h));
		h->note = i;
		h->title_hit = title_hits;
		h->score = title_hits * 10 + (title_hits == nw ? 50 : 0);
		/* the snippet: the first body line (after the title) where a word matched, without its
		 * Markdown marks, the matching words marked */
		const char *body = n->fold;
		size_t bl = n->blen;
		const char *nl = memchr(body, '\n', bl);
		size_t from = nl ? (size_t)(nl - body) + 1 : bl;
		const char *m = NULL;
		for (int w = 0; w < nw && !m; w++) m = find(body + from, bl - from, words[w], wl[w]);
		char plain[400];
		if (m) {
			size_t at = (size_t)(m - body), ls = at, le = at;
			while (ls > from && body[ls - 1] != '\n') ls--;
			while (le < bl && body[le] != '\n') le++;
			struct linfo li;
			line_info(n->body + ls, le - ls, 0, &li);
			size_t o = 0;
			if (li.kind == LK_TASK) o = (size_t)snprintf(plain, sizeof(plain), "%s ", li.checked ? "\u2611" : "\u2610");
			md_plain_line(n->body + ls, le - ls, plain + o, sizeof(plain) - o);
		} else snprintf(plain, sizeof(plain), "%s", n->snippet);
		/* a long line: start a little before the first match */
		char fp[400];
		size_t pl = strlen(plain), start = 0;
		fold_text(plain, pl, fp);
		const char *f0 = NULL;
		for (int w = 0; w < nw && !f0; w++) f0 = find(fp, pl, words[w], wl[w]);
		if (f0 && f0 - fp > 24) {
			start = (size_t)(f0 - fp) - 18;
			while (start > 0 && plain[start] != ' ') start--;
			while (start < pl && ((unsigned char)plain[start] & 0xc0) == 0x80) start++;
		}
		size_t o = 0;
		if (start) { memcpy(h->snip, "\u2026", 3); o = 3; }
		size_t len = pl - start;
		if (len > sizeof(h->snip) - 1 - o) len = u8_whole(plain + start, sizeof(h->snip) - 1 - o);
		memcpy(h->snip + o, plain + start, len);
		h->snip[o + len] = 0;
		for (int w = 0; w < nw && h->nhl < 6; w++) {
			const char *f = find(fp + start, len, words[w], wl[w]);
			if (f) {
				h->hl[h->nhl][0] = (int)(o + (size_t)(f - (fp + start)));
				h->hl[h->nhl][1] = h->hl[h->nhl][0] + (int)wl[w];
				h->nhl++;
			}
		}
	}
	qsort(out, (size_t)nh, sizeof(*out), by_score);
	return nh;
}
