// SPDX-License-Identifier: MIT
// A gap buffer over UTF-8 bytes, and the UTF-8 helpers the rest of the app uses.
#include "notes.h"
#include <stdlib.h>
#include <string.h>

void tb_init(struct tb *t) { memset(t, 0, sizeof(*t)); }
void tb_free(struct tb *t) { free(t->b); memset(t, 0, sizeof(*t)); }
size_t tb_len(const struct tb *t) { return t->cap - (t->ge - t->gs); }
char tb_at(const struct tb *t, size_t i) { return i < t->gs ? t->b[i] : t->b[i + (t->ge - t->gs)]; }

static void move_gap(struct tb *t, size_t pos)
{
	if (pos < t->gs) {
		size_t n = t->gs - pos;
		memmove(t->b + t->ge - n, t->b + pos, n);
		t->gs -= n; t->ge -= n;
	} else if (pos > t->gs) {
		size_t n = pos - t->gs;
		memmove(t->b + t->gs, t->b + t->ge, n);
		t->gs += n; t->ge += n;
	}
}

static int grow(struct tb *t, size_t need)
{
	if (t->ge - t->gs >= need) return 0;
	size_t len = tb_len(t), cap = t->cap ? t->cap : 256;
	while (cap - len < need + 64) cap *= 2;
	char *b = malloc(cap + 1);   /* +1: room for a NUL after the text (tb_range at the end) */
	if (!b) return -1;
	size_t tail = t->cap - t->ge;
	if (t->b) {
		memcpy(b, t->b, t->gs);
		memcpy(b + cap - tail, t->b + t->ge, tail);
	}
	free(t->b);
	t->ge = cap - tail;
	t->b = b;
	t->cap = cap;
	return 0;
}

int tb_insert(struct tb *t, size_t pos, const char *s, size_t n)
{
	if (!n) return 0;
	if (grow(t, n)) return -1;
	move_gap(t, pos);
	memcpy(t->b + t->gs, s, n);
	t->gs += n;
	return 0;
}

void tb_delete(struct tb *t, size_t pos, size_t n)
{
	if (!n) return;
	move_gap(t, pos);
	t->ge += n;
}

const char *tb_range(struct tb *t, size_t start, size_t end)
{
	static const char empty[1] = "";
	if (!t->b) return empty;
	if (t->gs > start && t->gs < end) move_gap(t, end);
	return start < t->gs ? t->b + start : t->b + start + (t->ge - t->gs);
}

char *tb_copy(const struct tb *t, size_t start, size_t end)
{
	char *s = malloc(end - start + 1);
	if (!s) return NULL;
	for (size_t i = start; i < end; i++) s[i - start] = tb_at(t, i);
	s[end - start] = 0;
	return s;
}

void tb_set(struct tb *t, const char *s, size_t n)
{
	tb_free(t);
	tb_insert(t, 0, s, n);
}

/* ---- UTF-8 ---- */

size_t tb_next(const struct tb *t, size_t i)
{
	size_t n = tb_len(t);
	if (i >= n) return n;
	i++;
	while (i < n && ((unsigned char)tb_at(t, i) & 0xc0) == 0x80) i++;
	return i;
}

size_t tb_prev(const struct tb *t, size_t i)
{
	if (!i) return 0;
	i--;
	while (i && ((unsigned char)tb_at(t, i) & 0xc0) == 0x80) i--;
	return i;
}

uint32_t u8_dec(const char *s, size_t n, size_t *i)
{
	const unsigned char *u = (const unsigned char *)s + *i;
	size_t left = n - *i;
	uint32_t c = u[0];
	int len = 1;
	if (c >= 0xf0 && left >= 4) { c = (uint32_t)(u[0] & 7) << 18 | (uint32_t)(u[1] & 0x3f) << 12 | (uint32_t)(u[2] & 0x3f) << 6 | (u[3] & 0x3f); len = 4; }
	else if (c >= 0xe0 && left >= 3) { c = (uint32_t)(u[0] & 15) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f); len = 3; }
	else if (c >= 0xc0 && left >= 2) { c = (uint32_t)(u[0] & 31) << 6 | (u[1] & 0x3f); len = 2; }
	else if (c >= 0x80) c = 0xfffd;
	*i += (size_t)len;
	return c;
}

int u8_enc(uint32_t c, char *o)
{
	if (c < 0x80) { o[0] = (char)c; return 1; }
	if (c < 0x800) { o[0] = (char)(0xc0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3f)); return 2; }
	if (c < 0x10000) { o[0] = (char)(0xe0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 0x3f)); o[2] = (char)(0x80 | (c & 0x3f)); return 3; }
	o[0] = (char)(0xf0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 0x3f)); o[2] = (char)(0x80 | (c >> 6 & 0x3f)); o[3] = (char)(0x80 | (c & 0x3f));
	return 4;
}

/* cut s to at most max bytes, at a character boundary; returns the new length */
size_t u8_cut(const char *s, size_t n, size_t max)
{
	if (n <= max) return n;
	while (max && ((unsigned char)s[max] & 0xc0) == 0x80) max--;
	return max;
}

/* drop a character cut in half at the end of s[0, n) */
size_t u8_whole(const char *s, size_t n)
{
	size_t k = n;
	while (k && ((unsigned char)s[k - 1] & 0xc0) == 0x80) k--;
	if (!k) return n;
	unsigned char c = (unsigned char)s[k - 1];
	size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
	return n - (k - 1) >= need ? n : k - 1;
}
