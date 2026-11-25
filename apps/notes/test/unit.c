// SPDX-License-Identifier: MIT
// Unit tests for the parts of Notes that need no screen: the text buffer, the line index,
// undo/redo, Markdown lines and inline marks, file names, saving, trash and the state file.
//   make notes-test
#include "notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; const char *_a = (a), *_b = (b); if (strcmp(_a, _b)) { fails++; fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); } } while (0)

/* stand-ins for what the store needs from the rest of the app */
int screen, plain_mode;

static char *text(struct doc *d) { size_t n; return doc_text(d, &n); }

static void test_tb(void)
{
	struct tb t;
	tb_init(&t);
	tb_insert(&t, 0, "hello world", 11);
	tb_insert(&t, 5, ",", 1);
	tb_delete(&t, 0, 1);
	tb_insert(&t, 0, "H", 1);
	char *s = tb_copy(&t, 0, tb_len(&t));
	CHECKS(s, "Hello, world");
	free(s);
	const char *r = tb_range(&t, 3, 9);
	CHECK(!memcmp(r, "lo, wo", 6));
	/* a big insert, then UTF-8 stepping */
	char *big = malloc(1 << 20);
	memset(big, 'a', 1 << 20);
	tb_insert(&t, 6, big, 1 << 20);
	CHECK(tb_len(&t) == 12 + (1 << 20));
	free(big);
	tb_set(&t, "a\xc3\xa9z", 4);   /* a é z */
	CHECK(tb_next(&t, 1) == 3);
	CHECK(tb_prev(&t, 3) == 1);
	CHECK(tb_prev(&t, 1) == 0);
	tb_free(&t);
	CHECK(u8_whole("ab\xc3", 3) == 2);
	CHECK(u8_whole("ab\xc3\xa9", 4) == 4);
	CHECK(u8_whole("\xe2\x80", 2) == 0);
}

static void test_doc(void)
{
	struct doc d;
	doc_init(&d);
	CHECK(d.np == 1);
	doc_edit(&d, 0, 0, "one\ntwo\nthree", 13, 0, 0);
	CHECK(d.np == 3);
	CHECK(d.p[1].start == 4 && d.p[1].len == 3);
	CHECK(doc_para_at(&d, 4) == 1 && doc_para_at(&d, 7) == 1 && doc_para_at(&d, 8) == 2);
	doc_edit(&d, 5, 0, "XX", 2, 0, 0);   /* no line break: the fast path */
	CHECK(d.p[1].len == 5 && d.p[2].start == 10);
	/* code fences */
	doc_load(&d, "a\n```\ncode\n```\nb", 16);
	CHECK(d.p[0].code == 0 && d.p[1].code == 0 && d.p[2].code == 1 && d.p[3].code == 2 && d.p[4].code == 0);
	doc_free(&d);

	/* undo: typing merges into words; a pause or a jump starts a new step */
	doc_init(&d);
	const char *w = "hello world";
	for (int i = 0; w[i]; i++) doc_edit(&d, (size_t)i, 0, w + i, 1, 1, 100);
	size_t cur = 0;
	char *s;
	CHECK(doc_undo(&d, &cur));
	s = text(&d); CHECKS(s, "hello "); free(s);
	CHECK(doc_undo(&d, &cur));
	s = text(&d); CHECKS(s, ""); free(s);
	CHECK(!doc_undo(&d, &cur));
	CHECK(doc_redo(&d, &cur));
	CHECK(doc_redo(&d, &cur));
	s = text(&d); CHECKS(s, "hello world"); free(s);
	CHECK(cur == 11);
	/* Backspace held merges too */
	for (int i = 0; i < 5; i++) doc_edit(&d, 10 - (size_t)i, 1, "", 0, 1, 200);
	s = text(&d); CHECKS(s, "hello "); free(s);
	doc_undo(&d, &cur);
	s = text(&d); CHECKS(s, "hello world"); free(s);
	/* a batch is one step */
	doc_batch(&d, 1);
	doc_edit(&d, 0, 0, "- [ ] ", 6, 0, 300);
	doc_edit(&d, 6, 5, "HELLO", 5, 0, 300);
	doc_batch(&d, 0);
	s = text(&d); CHECKS(s, "- [ ] HELLO world"); free(s);
	doc_undo(&d, &cur);
	s = text(&d); CHECKS(s, "hello world"); free(s);
	/* a new edit drops the redo */
	doc_edit(&d, 0, 0, "X", 1, 0, 400);
	CHECK(!doc_redo(&d, &cur));
	/* two seconds apart: two steps */
	doc_edit(&d, 1, 0, "a", 1, 1, 1000);
	doc_edit(&d, 2, 0, "b", 1, 1, 4000);
	doc_undo(&d, &cur);
	s = text(&d); CHECKS(s, "Xahello world"); free(s);
	doc_free(&d);

	/* the undo cap: many edits never grow past it */
	doc_init(&d);
	for (int i = 0; i < 5000; i++) doc_edit(&d, 0, 0, "ab ", 3, 0, (uint32_t)i * 3000);
	CHECK(d.nu <= 2000);
	doc_free(&d);
}

static void li(const char *s, int kind, int mlen)
{
	struct linfo l;
	line_info(s, strlen(s), 0, &l);
	checks++;
	if (l.kind != kind || l.mlen != mlen) { fails++; fprintf(stderr, "line_info(\"%s\"): kind %d mlen %d, want %d %d\n", s, l.kind, l.mlen, kind, mlen); }
}

struct spans { char out[512]; };
static void emit(void *u, size_t off, size_t len, int style, int hidden)
{
	struct spans *sp = u;
	char b[96];
	if (hidden) style = 0;   /* a hidden mark's style does not matter */
	snprintf(b, sizeof(b), "%s%zu+%zu%s%s%s%s;", hidden ? "h" : "", off, len, style & ST_BOLD ? "B" : "", style & ST_ITAL ? "I" : "",
	         style & ST_CODE ? "C" : "", style & ST_LINK ? "L" : "");
	strncat(sp->out, b, sizeof(sp->out) - strlen(sp->out) - 1);
}
static void inl(const char *s, const char *want)
{
	struct spans sp = { "" };
	md_inline(s, strlen(s), 0, 0, emit, &sp);
	checks++;
	if (strcmp(sp.out, want)) { fails++; fprintf(stderr, "md_inline(\"%s\") = %s, want %s\n", s, sp.out, want); }
}

static void test_md(void)
{
	li("plain", LK_TEXT, 0);
	li("", LK_BLANK, 0);
	li("   ", LK_BLANK, 0);
	li("# Title", LK_HEAD, 2);
	li("### Three", LK_HEAD, 4);
	li("#hashtag", LK_TEXT, 0);
	li("- item", LK_BULLET, 2);
	li("* item", LK_BULLET, 2);
	li("-item", LK_TEXT, 0);
	li("- [ ] todo", LK_TASK, 6);
	li("- [x] done", LK_TASK, 6);
	li("  - [X] nested", LK_TASK, 6);
	li("- [ ]", LK_TASK, 5);
	li("- [ ]x", LK_BULLET, 2);
	li("12. twelve", LK_NUM, 4);
	li("3) three", LK_NUM, 3);
	li("2024 was", LK_TEXT, 0);
	li("> quote", LK_QUOTE, 2);
	li("---", LK_RULE, 3);
	li("- - -", LK_RULE, 5);
	li("```", LK_FENCE, 0);
	inl("a **b** c", "0+2;h2+2;4+1B;h5+2;7+2;");
	inl("*i* and _j_", "h0+1;1+1I;h2+1;3+5;h8+1;9+1I;h10+1;");
	inl("snake_case_word", "0+15;");
	inl("`co*de`", "h0+1;1+5C;h6+1;");
	inl("``a`b``", "h0+2;2+3C;h5+2;");
	inl("[t](http://x)", "h0+1;1+1L;h2+11;");
	inl("2 * 3 * 4", "0+9;");
	inl("\\*no\\*", "h0+1;1+3;h4+1;5+1;");
	char out[64];
	md_plain_line("- [ ] **buy** milk", 18, out, sizeof(out));
	CHECKS(out, "buy milk");
	md_plain_line("# \xc3\xa9t\xc3\xa9", 7, out, 3);   /* cut short: never half an é */
	CHECKS(out, "\xc3\xa9");
	md_plain_line("# \xc3\xa9t\xc3\xa9", 7, out, 2);
	CHECKS(out, "");
}

static char dir[256];

static void test_store(void)
{
	snprintf(dir, sizeof(dir), "/tmp/emuzero-notes-unit-%d", (int)getpid());
	CHECK(!store_init(dir));
	state_load();
	char name[256], err[128];
	store_name_for("# Groceries\nmilk", 16, NULL, name, sizeof(name));
	CHECKS(name, "Groceries.md");
	CHECK(!store_write(name, "# Groceries\nmilk", 16, err, sizeof(err)));
	store_name_for("Groceries", 9, NULL, name, sizeof(name));
	CHECKS(name, "Groceries 2.md");
	store_name_for("Groceries", 9, "Groceries.md", name, sizeof(name));
	CHECKS(name, "Groceries.md");
	store_name_for("Shopping", 8, "list.txt", name, sizeof(name));
	CHECKS(name, "Shopping.txt");
	CHECK(note_stem("a.txt") == 1 && note_stem("bb.md") == 2 && !strcmp(note_ext("x.txt"), ".txt"));
	store_name_for("a/b: c?", 7, NULL, name, sizeof(name));
	CHECKS(name, "ab c.md");
	store_name_for("...hidden", 9, NULL, name, sizeof(name));
	CHECKS(name, "hidden.md");
	store_name_for("\n\n", 2, NULL, name, sizeof(name));
	CHECK(!strncmp(name, "Note ", 5));
	store_name_for("A very long title that goes on and on past the edge of the row for sure", 71, NULL, name, sizeof(name));
	CHECKS(name, "A very long title that goes on and on past the edge of the.md");
	/* titles for the list */
	char t[128], sn[160];
	store_note_title("\n# Trip\n\n- [ ] passport\n", 24, t, sizeof(t), sn, sizeof(sn));
	CHECKS(t, "Trip");
	CHECKS(sn, "\xe2\x98\x90 passport");
	/* the save is whole: no temp file left, the content exact */
	char *s; size_t n;
	CHECK(!store_read("Groceries.md", &s, &n));
	CHECK(n == 16 && !memcmp(s, "# Groceries\nmilk", 16));
	free(s);
	char p[400];
	snprintf(p, sizeof(p), "%s/.Groceries.md.tmp", dir);
	CHECK(access(p, F_OK) != 0);
	/* a leftover temp file (power cut mid-save) is cleaned up at start */
	FILE *f = fopen(p, "w"); fputs("half", f); fclose(f);
	CHECK(!store_init(dir));
	CHECK(access(p, F_OK) != 0);
	/* the list */
	store_scan();
	CHECK(nnotes == 1 && !strcmp(notes[0].title, "Groceries") && !strcmp(notes[0].snippet, "milk"));
	/* rename never replaces */
	CHECK(!store_write("Other.md", "Other", 5, err, sizeof(err)));
	CHECK(store_rename("Other.md", "Groceries.md") != 0);
	CHECK(!store_rename("Other.md", "Renamed.md"));
	/* trash and restore, pins follow */
	set_pinned("Renamed.md", 1);
	CHECK(!store_trash("Renamed.md"));
	CHECK(!is_pinned("Renamed.md"));
	CHECK(st.ntrash == 1);
	store_scan();
	CHECK(nnotes == 1);
	char back[256];
	CHECK(!store_restore(0, back, sizeof(back)));
	CHECKS(back, "Other.md");
	CHECK(st.ntrash == 0);
	/* the state file round trip */
	set_pinned("Groceries.md", 1);
	snprintf(st.last, sizeof(st.last), "Groceries.md");
	st.cur = 7; st.mode = MODE_EDIT; st.size = 2; st.ticked_bottom = 1;
	state_save();
	memset(&st, 0, sizeof(st));
	state_load();
	CHECKS(st.last, "Groceries.md");
	CHECK(st.cur == 7 && st.mode == MODE_EDIT && st.size == 2 && st.ticked_bottom == 1 && is_pinned("Groceries.md"));
	/* a file too big or not text is refused, not half-loaded */
	snprintf(p, sizeof(p), "%s/bin.md", dir);
	f = fopen(p, "wb"); fwrite("a\0b", 1, 3, f); fclose(f);
	CHECK(store_read("bin.md", &s, &n) != 0);
	char cmd[400];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd)) {}
}

int main(void)
{
	test_tb();
	test_doc();
	test_md();
	test_store();
	printf("notes unit tests: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
