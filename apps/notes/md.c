// SPDX-License-Identifier: MIT
// The Markdown Notes understands: what kind of line each line is, and (for View mode) the
// inline marks inside a line. Deliberately small, not CommonMark: headings, bullets, numbered
// lists, checklists, quotes, rules, ``` code blocks; **bold**, *italic*, `code`, ~~strike~~,
// [links](url) and \ escapes.
#include "notes.h"
#include <string.h>

void line_info(const char *s, size_t n, int code, struct linfo *li)
{
	memset(li, 0, sizeof(*li));
	size_t i = 0;
	while (i < n && s[i] == ' ') i++;
	li->lead = (int)i;
	if (code == 1) { li->kind = LK_CODE; return; }
	if (code == 2) { li->kind = LK_FENCE; return; }
	if (n - i >= 3 && (s[i] == '`' || s[i] == '~') && s[i + 1] == s[i] && s[i + 2] == s[i] && i < 4) { li->kind = LK_FENCE; return; }
	if (i == n) { li->kind = LK_BLANK; return; }
	char c = s[i];
	if (c == '#' && i < 4) {
		size_t h = i;
		while (h < n && s[h] == '#' && h - i < 7) h++;
		if (h - i <= 6 && (h == n || s[h] == ' ')) {
			li->kind = LK_HEAD;
			li->level = (int)(h - i);
			li->mlen = (int)(h - i + (h < n));
			return;
		}
	}
	if (c == '-' || c == '*' || c == '_') {   /* a rule: three or more of the same, spaces allowed */
		size_t k = i;
		int cnt = 0, only = 1;
		for (; k < n; k++) { if (s[k] == c) cnt++; else if (s[k] != ' ') { only = 0; break; } }
		if (only && cnt >= 3) { li->kind = LK_RULE; li->mlen = (int)(n - i); return; }
	}
	if ((c == '-' || c == '*' || c == '+') && i + 1 < n && s[i + 1] == ' ') {
		if (i + 4 < n && s[i + 2] == '[' && (s[i + 3] == ' ' || s[i + 3] == 'x' || s[i + 3] == 'X') && s[i + 4] == ']' &&
		    (i + 5 == n || s[i + 5] == ' ')) {
			li->kind = LK_TASK;
			li->checked = s[i + 3] != ' ';
			li->mlen = i + 5 == n ? 5 : 6;
			return;
		}
		li->kind = LK_BULLET;
		li->mlen = 2;
		return;
	}
	if (c >= '0' && c <= '9') {
		size_t k = i;
		int v = 0;
		while (k < n && k - i < 9 && s[k] >= '0' && s[k] <= '9') v = v * 10 + (s[k++] - '0');
		if (k < n && (s[k] == '.' || s[k] == ')') && (k + 1 == n || s[k + 1] == ' ')) {
			li->kind = LK_NUM;
			li->num = v;
			li->mlen = (int)(k - i + 1 + (k + 1 < n));
			return;
		}
	}
	if (c == '>') {
		li->kind = LK_QUOTE;
		li->mlen = 1 + (i + 1 < n && s[i + 1] == ' ');
		return;
	}
	li->kind = LK_TEXT;
}

/* View mode's inline marks: one call per line, adding spans for [0, n) */
static int closes(const char *s, size_t n, size_t from, const char *mark, size_t ml)
{
	for (size_t k = from; k + ml <= n; k++) {
		if (s[k] == '\\') { k++; continue; }
		if (s[k] == '`' && mark[0] != '`') {   /* marks inside `code` do not count */
			const char *e = memchr(s + k + 1, '`', n - k - 1);
			if (e) { k = (size_t)(e - s); continue; }
		}
		if (!memcmp(s + k, mark, ml) && k > from) return 1;
	}
	return 0;
}

static int alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80; }

void md_inline(const char *s, size_t n, size_t base, int style0, void (*emit)(void *, size_t off, size_t len, int style, int hidden), void *u)
{
	int st = style0;
	size_t run = 0, i = 0;
#define FLUSH(to) do { if ((to) > run) emit(u, base + run, (to) - run, st, 0); } while (0)
#define HIDE(k, l) do { emit(u, base + (k), (l), st, 1); } while (0)
	while (i < n) {
		char c = s[i];
		if (c == '\\' && i + 1 < n && strchr("\\`*_~[]()#>-+!|", s[i + 1])) {
			FLUSH(i); HIDE(i, 1); run = i + 1; i += 2; continue;
		}
		if (c == '`') {   /* `code`, or ``code with a ` in it`` */
			size_t tick = 1;
			while (i + tick < n && s[i + tick] == '`' && tick < 3) tick++;
			size_t k = i + tick, end = 0;
			for (; k + tick <= n; k++) {
				size_t t = 0;
				while (k + t < n && s[k + t] == '`') t++;
				if (t == tick) { end = k; break; }
				if (t) k += t - 1;
			}
			if (end) {
				size_t a = i + tick, b = end;
				if (tick > 1 && b > a + 1 && s[a] == ' ' && s[b - 1] == ' ') { a++; b--; }
				FLUSH(i); HIDE(i, a - i);
				if (b > a) emit(u, base + a, b - a, st | ST_CODE, 0);
				HIDE(b, end + tick - b);
				i = run = end + tick;
				continue;
			}
		}
		if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
			char m[2] = { c, c };
			int open = !(st & ST_BOLD);
			if (!open || (closes(s, n, i + 2, m, 2) && i + 2 < n && s[i + 2] != ' ')) {
				FLUSH(i); HIDE(i, 2); st ^= ST_BOLD; i += 2; run = i; continue;
			}
		}
		if (c == '*' || c == '_') {
			char m[1] = { c };
			int open = !(st & ST_ITAL);
			/* _ only at a word's edge, so snake_case stays as it is */
			int edge_before = i == 0 || !alnum(s[i - 1]), edge_after = i + 1 == n || !alnum(s[i + 1]);
			if ((!open && (c == '*' || edge_after)) ||
			    (open && (c == '*' || edge_before) && i + 1 < n && s[i + 1] != ' ' && closes(s, n, i + 1, m, 1))) {
				FLUSH(i); HIDE(i, 1); st ^= ST_ITAL; i += 1; run = i; continue;
			}
		}
		if (c == '~' && i + 1 < n && s[i + 1] == '~') {
			if ((st & ST_STRIKE) || closes(s, n, i + 2, "~~", 2)) { FLUSH(i); HIDE(i, 2); st ^= ST_STRIKE; i += 2; run = i; continue; }
		}
		if (c == '[') {   /* [text](url) */
			const char *e = memchr(s + i + 1, ']', n - i - 1);
			if (e && (size_t)(e - s) + 1 < n && e[1] == '(') {
				size_t te = (size_t)(e - s);
				const char *pe = memchr(e + 2, ')', n - te - 2);
				if (pe) {
					size_t ue = (size_t)(pe - s);
					FLUSH(i); HIDE(i, 1);
					md_inline(s + i + 1, te - i - 1, base + i + 1, st | ST_LINK, emit, u);
					HIDE(te, ue - te + 1);
					i = run = ue + 1;
					continue;
				}
			}
		}
		i++;
	}
	FLUSH(n);
#undef FLUSH
#undef HIDE
}

/* a note's title for the list: its first line that has text, without Markdown marks */
size_t md_plain_line(const char *s, size_t n, char *out, size_t max)
{
	struct linfo li;
	line_info(s, n, 0, &li);
	size_t i = (size_t)(li.lead + li.mlen), o = 0;
	if (li.kind == LK_RULE || li.kind == LK_FENCE) i = n;
	for (; i < n && o + 1 < max; i++) {
		char c = s[i];
		if (c == '*' || c == '`' || (c == '_' && (i + 1 == n || s[i + 1] == '_' || (i && s[i - 1] == '_'))) ||
		    (c == '~' && i + 1 < n && s[i + 1] == '~') || (c == '~' && i && s[i - 1] == '~')) continue;
		if (c == '\t') c = ' ';
		if ((unsigned char)c < 32) continue;
		out[o++] = c;
	}
	if (i < n) o = u8_whole(out, o);   /* cut short: no half character at the end */
	while (o && out[o - 1] == ' ') o--;
	out[o] = 0;
	return o;
}
