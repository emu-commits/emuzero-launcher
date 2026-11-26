// SPDX-License-Identifier: MIT
// emuZero Wiki: an offline reference for the CardputerZero. Survival guides, the US Army
// Survival Manual and (when its pack is on the device) Simple English Wikipedia, in one search
// box; articles read in Notes' View mode.
//
//   emuzero-wiki                                   search box, shelves
//   emuzero-wiki --keys K1,K2 --shot out.png       headless build: play keys, write the screen
//   emuzero-wiki --script FILE [--shot out.png]    headless build: a script (Notes' nplat_headless.c)
//   --theme NAME   a launcher theme for this run
// WIKI_DIR=dir1:dir2 looks for packs there instead; WIKI_TRACE=FILE writes what each screen
// shows (for the checks); WIKI_TIMING=1 logs search and article times.
#include "wiki.h"
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef EMUZERO_VERSION
#define EMUZERO_VERSION "dev"
#endif

struct scr stack[32];
int nstack;
static int quit;
static volatile sig_atomic_t sig_quit;
static lv_obj_t *root;
static FILE *trace_f;

uint32_t wnow(void) { return lv_tick_get(); }
uint32_t now_ms(void) { return lv_tick_get(); }
void redraw(void) { if (root) lv_obj_invalidate(root); }

void trace(const char *fmt, ...)
{
	if (!trace_f) return;
	va_list a;
	va_start(a, fmt);
	vfprintf(trace_f, fmt, a);
	va_end(a);
	fputc('\n', trace_f);
	fflush(trace_f);
}

struct scr *cur_scr(void) { return &stack[nstack - 1]; }

static void enter_top(void)
{
	switch (cur_scr()->kind) {
	case S_READ: read_enter(); break;
	case S_PIC: pic_enter(); trace("picture %s", cur_scr()->name); break;
	default: home_enter();
	}
	redraw();
}

void push_scr(const struct scr *s)
{
	if (nstack && cur_scr()->kind == S_READ) read_leave();
	if (nstack == (int)(sizeof(stack) / sizeof(stack[0]))) {   /* deep link chains: forget the oldest after home */
		memmove(stack + 1, stack + 2, sizeof(stack[0]) * (size_t)(nstack - 2));
		nstack--;
	}
	stack[nstack++] = *s;
	enter_top();
}

void pop_scr(void)
{
	if (nstack <= 1) return;
	nstack--;
	enter_top();
}

void go_home(void)
{
	if (nstack && cur_scr()->kind == S_READ) read_leave();
	nstack = 1;
	enter_top();
}

int cur_ctx(void)
{
	switch (cur_scr()->kind) {
	case S_HOME: return X_HOME;
	case S_LIST: return X_LIST;
	case S_TRIAGE: return X_TRIAGE;
	case S_READ: return X_READ;
	default: return X_PIC;
	}
}

void menu_cmd_done(int id, int idx)
{
	(void)idx;
	if (id <= 0) return;
	if (cur_scr()->kind == S_READ && read_cmd(id)) { redraw(); return; }
	if ((cur_scr()->kind == S_HOME || cur_scr()->kind == S_LIST || cur_scr()->kind == S_TRIAGE) && home_cmd(id)) { redraw(); return; }
	wiki_cmd(id);
}

static int first_pack(int memory)
{
	for (int p = 0; p < npacks; p++) if (packs[p].memory == memory) return p;
	return -1;
}

void wiki_cmd(int cmd)
{
	switch (cmd) {
	case W_BACK: pop_scr(); break;
	case W_SEARCH: go_home(); break;
	case W_HELP: help_open(cur_ctx()); break;
	case W_MENU:
		if (cur_scr()->kind == S_READ) read_menu();
		else home_menu();
		break;
	case W_SIZE:
		fonts_set_size((text_size + 1) % 3);
		ws.size = text_size;
		view_invalidate_all();
		pics_clear();
		wstate_save();
		toast(text_size == 0 ? "Text size: Small" : text_size == 1 ? "Text size: Medium" : "Text size: Large");
		break;
	case W_TRIAGE: if (triage_pack() >= 0) { go_home(); open_triage("root"); } break;
	case W_GUIDES: go_home(); open_list(L_CATEGORIES, 0); break;
	case W_MANUAL: go_home(); open_list(L_CHAPTERS, 0); break;
	case W_RECENT: open_list(L_RECENT, 0); break;
	case W_MARKS: open_list(L_MARKS, 0); break;
	case W_RANDOM: { int p = first_pack(0); if (p >= 0) open_article(p, pack_random(p)); else toast("Random needs the Wikipedia pack"); break; }
	case W_ABOUT: { struct scr s = { .kind = S_READ, .pack = -1, .id = 1 }; push_scr(&s); break; }
	case W_QUIT: quit = 1; break;
	default: break;
	}
	redraw();
}

/* the built-in pages: About (1) and Get Wikipedia (2), as Markdown */
char *builtin_page(int id, char *title, size_t tn)
{
	size_t cap = 8192, o = 0;
	char *b = malloc(cap);
#define P(...) o += (size_t)snprintf(b + o, cap - o, __VA_ARGS__)
	if (id == 1) {
		snprintf(title, tn, "About the Wiki");
		P("emuZero Wiki %s: an offline reference. Type a few letters on the search screen; Enter opens, Ctrl+Enter searches inside the articles.\n\n", EMUZERO_VERSION);
		P("## Packs on this device\n\n");
		for (int p = 0; p < npacks; p++) {
			P("**%s**: %d articles\n\n", packs[p].name, packs[p].narticles);
			if (packs[p].source[0]) P("- Source: %s\n", packs[p].source);
			if (packs[p].licence[0]) P("- Licence: %s\n", packs[p].licence);
			P("- File: `%s`\n\n", packs[p].path);
			if (packs[p].notice[0]) P("> %s\n\n", packs[p].notice);
		}
		if (!npacks) P("No packs found. The survival pack belongs in `/usr/share/emuzero/wiki/`; more packs go in `~/Wiki/` or `/data/wiki/`.\n\n");
		if (pack_errors[0]) P("Could not read: %s\n\n", pack_errors);
		P("## Licences\n\n");
		P("- The survival guides come from the CYD Survival Wiki by Everyday3D / BoxerDawg 3D (MIT licence).\n");
		P("- FM 21-76, the US Army Survival Manual (1992), is a US government publication in the public domain.\n");
		P("- Simple English Wikipedia text is by Wikipedia contributors, under CC BY-SA 4.0.\n");
		P("- This program is MIT licensed, part of the emuZero launcher.\n\n");
		P("> In an emergency, call emergency services first if you can.\n");
	} else {
		snprintf(title, tn, "Get Wikipedia");
		P("Simple English Wikipedia is one file, `simplewiki.wpack` (about 234 MB, 267,000 articles, searchable inside the text).\n\n");
		P("## Copy it from a computer\n\n");
		P("1. From the computer, over Wi-Fi (one line):\n\n");
		P("```\nssh zero@nimblezero.local 'mkdir -p ~/Wiki && cat > ~/Wiki/simplewiki.part && mv ~/Wiki/simplewiki.part ~/Wiki/simplewiki.wpack' < simplewiki.wpack\n```\n\n");
		P("2. Open the Wiki again: Wikipedia shows on the search screen.\n\n");
		P("The file arrives under another name and gets its own only when it is complete, so a copy that stops halfway is never taken for the pack; run the line again to start over.\n\n");
		P("At this house's Wi-Fi speed (about 200 KB/s) the copy takes about 20 minutes.\n\n");
		P("## Make the file yourself\n\n");
		P("From the emuzero-launcher source: `apps/wiki/tools/build-pack.py simplewiki wiki-slim.db simplewiki.wpack`\n");
	}
#undef P
	return b;
}

static int should_quit(void) { return quit || sig_quit; }
static void on_signal(int s) { (void)s; sig_quit = 1; }

static void on_key(struct nkey k)
{
	if (overlay_key(k)) return;
	switch (cur_scr()->kind) {
	case S_READ: read_key(k); break;
	case S_PIC: pic_key(k); break;
	default: home_key(k);
	}
}

static void on_mods(unsigned m)
{
	ui_mods = m;
	if (root) { lv_area_t a = { 0, 0, SCREEN_W - 1, BAR_H - 1 }; lv_obj_invalidate_area(root, &a); }
}

static void on_draw(lv_event_t *e)
{
	draw_begin(lv_event_get_layer(e));
	switch (cur_scr()->kind) {
	case S_READ: read_draw(); break;
	case S_PIC: pic_draw(); break;
	default: home_draw();
	}
	overlay_draw();
}

static void clock_cb(lv_timer_t *t)
{
	time_t now = time(NULL);
	lv_timer_set_period(t, (uint32_t)(60 - now % 60) * 1000 + 50);
	lv_area_t a = { 0, 0, SCREEN_W - 1, BAR_H - 1 };
	if (root) lv_obj_invalidate_area(root, &a);
}

static void usage(void)
{
	printf("usage: emuzero-wiki [--theme NAME] [--kbd DEV] [--zoom N] [--keys K1,K2,...] [--script FILE] [--shot FILE.png] [--version]\n");
}

int main(int argc, char **argv)
{
	struct nplat_opts o = { .zoom = 3 };
	const char *theme_arg = NULL;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--version")) { printf("emuzero-wiki %s (%s)\n", EMUZERO_VERSION, nplat_name()); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
		else if (!strcmp(a, "--kbd") && v) { o.kbd = v; i++; }
		else if (!strcmp(a, "--keys") && v) { o.keys = v; i++; }
		else if (!strcmp(a, "--script") && v) { o.script = v; i++; }
		else if (!strcmp(a, "--shot") && v) { o.shot = v; i++; }
		else if (!strcmp(a, "--zoom") && v) { o.zoom = atoi(v); i++; }
		else if (!strcmp(a, "--theme") && v) { theme_arg = v; i++; }
		else { usage(); return 2; }
	}
	const char *tf = getenv("WIKI_TRACE");
	if (tf && *tf) trace_f = fopen(tf, "w");
	lv_init();
	lv_tick_set_cb(nplat_tick);
	if (!nplat_display(&o)) { fprintf(stderr, "emuzero-wiki: no display\n"); return 1; }
	if (fonts_init(theme_arg)) return 1;
	wstate_load();
	fonts_set_size(ws.size);
	uint32_t t0 = real_ms();
	packs_open();
	triage_load();
	timing_log("packs", t0);

	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, C(pal.bg), 0);
	root = lv_obj_create(scr);
	lv_obj_remove_style_all(root);
	lv_obj_set_size(root, SCREEN_W, SCREEN_H);
	lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(root, on_draw, LV_EVENT_DRAW_MAIN, NULL);
	lv_timer_t *clk = lv_timer_create(clock_cb, 1000, NULL);
	clock_cb(clk);

	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	nplat_keys(&o, on_key, on_mods);
	struct scr home = { .kind = S_HOME };
	stack[0] = home;
	nstack = 1;
	enter_top();
	lv_refr_now(NULL);
	int r = nplat_run(&o, should_quit);
	packs_close();
	if (trace_f) fclose(trace_f);
	return r;
}
