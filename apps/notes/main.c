// SPDX-License-Identifier: MIT
// emuZero Notes: notes for the CardputerZero, in the manner of iPhone Notes.
//
//   emuzero-notes                    the notes in ~/Notes (or $NOTES_DIR); reopens the last one
//   emuzero-notes FILE               one file, any file (the old Editor's job); quits when closed
//   emuzero-notes --keys K1,K2 --shot out.png      headless build: play keys, write the screen
//   emuzero-notes --script FILE [--shot out.png]   headless build: a script (see nplat_headless.c)
//   --theme NAME   a launcher theme for this run;  --dir DIR  another notes folder
#include "notes.h"
#include "paths.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef EMUZERO_VERSION
#define EMUZERO_VERSION "dev"
#endif

int screen = SCR_LIST;
int plain_mode;
static int quit, saved_at_exit;
static volatile sig_atomic_t sig_quit;
static lv_obj_t *root;

uint32_t now_ms(void) { return lv_tick_get(); }

void redraw(void) { if (root) lv_obj_invalidate(root); }

const char *guide_path(void)
{
	static char p[600];
	const char *e = getenv("NOTES_GUIDE");
	if (e && !access(e, R_OK)) return e;
	path_join(p, sizeof(p), "/usr/share/emuzero/notes/guide.md");
	if (!access(p, R_OK)) return p;
	if (!access("apps/notes/guide.md", R_OK)) return "apps/notes/guide.md";   /* a run from the source tree */
	return p;
}

static void final_save(void)
{
	if (saved_at_exit) return;
	saved_at_exit = 1;
	if (ed_is_open()) { ed_save(); ed_remember(); }
	else st.last[0] = 0;
	state_save();
}

void app_quit(void)
{
	final_save();
	quit = 1;
}

static int should_quit(void)
{
	if (sig_quit && !quit) app_quit();   /* the launcher's stop chord, power-off: save first */
	return quit;
}

static void on_signal(int s) { (void)s; sig_quit = 1; }

static int ctx_now(void)
{
	extern int ed_mode_ctx(void);
	return screen == SCR_LIST ? CTX_LIST : ed_mode_ctx();
}

void app_cmd(int cmd)
{
	extern int list_cmd(int cmd);
	extern int ed_cmd(int cmd, int shifted);
	switch (cmd) {
	case C_HELP: help_open(ctx_now()); break;
	case C_MENU: if (screen == SCR_NOTE) ed_menu(); else list_cmd(C_MENU); break;
	case C_NEW:
		if (screen == SCR_NOTE) { int pm = plain_mode; plain_mode = 0; ed_close(); plain_mode = pm; if (ed_is_open()) break; }
		ed_new("");
		break;
	case C_SIZE:
		fonts_set_size((text_size + 1) % 3);
		st.size = text_size;
		view_invalidate_all();
		state_save();
		toast(text_size == 0 ? "Text size: Small" : text_size == 1 ? "Text size: Medium" : "Text size: Large");
		break;
	case C_AUTO_TICKED:
		st.ticked_bottom = !st.ticked_bottom;
		state_save();
		toast(st.ticked_bottom ? "Ticked items now go to the bottom" : "Ticked items stay where they are");
		break;
	case C_GUIDE:
		if (screen == SCR_NOTE) { int pm = plain_mode; plain_mode = 0; ed_close(); plain_mode = pm; if (ed_is_open()) break; }
		ed_open_path(guide_path(), 1);
		break;
	case C_QUIT: app_quit(); break;
	default:
		if (screen == SCR_NOTE) ed_cmd(cmd, 0);
		else list_cmd(cmd);
	}
	redraw();
}

static void on_key(struct nkey k)
{
	if (overlay_key(k)) return;
	if (screen == SCR_NOTE && ed_is_open()) ed_key(k);
	else list_key(k);
}

static void on_mods(unsigned m)
{
	ui_mods = m;
	if (root) { lv_area_t a = { 0, 0, SCREEN_W - 1, BAR_H - 1 }; lv_obj_invalidate_area(root, &a); }
}

static void on_draw(lv_event_t *e)
{
	draw_begin(lv_event_get_layer(e));
	if (screen == SCR_NOTE && ed_is_open()) ed_draw();
	else list_draw();
	overlay_draw();
}

/* the clock in the status bar: once a minute, on the minute */
static void clock_cb(lv_timer_t *t)
{
	time_t now = time(NULL);
	lv_timer_set_period(t, (uint32_t)(60 - now % 60) * 1000 + 50);
	lv_area_t a = { 0, 0, SCREEN_W - 1, BAR_H - 1 };
	if (root) lv_obj_invalidate_area(root, &a);
}

static double since_exec_ms(void)
{
	char b[512];
	double up = 0;
	unsigned long long start = 0;
	FILE *f = fopen("/proc/uptime", "r");
	if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
	f = fopen("/proc/self/stat", "r");
	if (!f) return 0;
	size_t n = fread(b, 1, sizeof(b) - 1, f);
	fclose(f);
	b[n] = 0;
	char *p = strrchr(b, ')');
	for (int i = 0; p && i < 20; i++) p = strchr(p + 1, ' ');
	if (p) start = strtoull(p + 1, NULL, 10);
	return up * 1000.0 - (double)start * 1000.0 / (double)sysconf(_SC_CLK_TCK);
}

static void usage(void)
{
	printf("usage: emuzero-notes [FILE] [--dir DIR] [--theme NAME] [--kbd DEV] [--zoom N]\n"
	       "                     [--keys K1,K2,...] [--script FILE] [--shot FILE.png] [--version]\n");
}

int main(int argc, char **argv)
{
	struct nplat_opts o = { .zoom = 3 };
	const char *theme_arg = NULL, *dir = NULL, *file = NULL;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--version")) { printf("emuzero-notes %s (%s)\n", EMUZERO_VERSION, nplat_name()); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
		else if (!strcmp(a, "--kbd") && v) { o.kbd = v; i++; }
		else if (!strcmp(a, "--keys") && v) { o.keys = v; i++; }
		else if (!strcmp(a, "--script") && v) { o.script = v; i++; }
		else if (!strcmp(a, "--shot") && v) { o.shot = v; i++; }
		else if (!strcmp(a, "--zoom") && v) { o.zoom = atoi(v); i++; }
		else if (!strcmp(a, "--theme") && v) { theme_arg = v; i++; }
		else if (!strcmp(a, "--dir") && v) { dir = v; i++; }
		else if (a[0] == '-' && a[1]) { usage(); return 2; }
		else file = a;
	}
	lv_init();
	lv_tick_set_cb(nplat_tick);
	if (!nplat_display(&o)) { fprintf(stderr, "emuzero-notes: no display\n"); return 1; }
	if (fonts_init(theme_arg)) return 1;
	if (store_init(dir)) { fprintf(stderr, "emuzero-notes: can't use the notes folder %s\n", notes_dir); return 1; }
	state_load();
	fonts_set_size(st.size);
	store_purge_old();

	lv_obj_t *scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, C(pal.bg), 0);
	root = lv_obj_create(scr);
	lv_obj_remove_style_all(root);
	lv_obj_set_size(root, SCREEN_W, SCREEN_H);
	lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(root, on_draw, LV_EVENT_DRAW_MAIN, NULL);
	lv_timer_t *clk = lv_timer_create(clock_cb, 1000, NULL);
	clock_cb(clk);

	struct sigaction sa = { .sa_handler = on_signal };   /* no SA_RESTART: poll() wakes up */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	atexit(final_save);   /* the desktop window's close button exits from inside LVGL */

	nplat_keys(&o, on_key, on_mods);

	if (file) {
		plain_mode = 1;
		char abs[600];
		if (file[0] != '/' && getcwd(abs, sizeof(abs) - 300)) { strcat(abs, "/"); strncat(abs, file, sizeof(abs) - strlen(abs) - 1); file = abs; }
		ed_open_path(file, 0);
	} else {
		store_scan();
		if (st.last[0] && store_find(st.last) >= 0) ed_open(st.last, st.mode, st.cur, st.anc, st.top_para, st.top_row, st.focus);
		else list_show();
	}
	if (!ed_is_open() && screen == SCR_NOTE) list_show();

	lv_refr_now(NULL);
	if (!strcmp(nplat_name(), "device")) fprintf(stderr, "emuzero-notes %s: first frame %.0f ms after exec\n", EMUZERO_VERSION, since_exec_ms());
	int r = nplat_run(&o, should_quit);
	final_save();
	return r;
}
