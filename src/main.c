// SPDX-License-Identifier: MIT
// emuZero launcher: a thin front end for NimbleZero.
//
//   emuzero-launcher [--kbd /dev/input/eventN]           device (default: every keyboard)
//   emuzero-launcher [--zoom N]                          desktop window
//   emuzero-launcher --keys right,enter --shot out.png   headless
//   --theme NAME, --layout carousel|grid: for this run, instead of the settings (Theme page)
//   --themes lists the themes; --list the apps
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "platform.h"
#include "ui.h"
#include "apps.h"
#include "icons.h"
#include "theme.h"

#ifndef EMUZERO_VERSION
#define EMUZERO_VERSION "dev"
#endif

/* start-up timing, logged with the first frame: ms since the process started (exec), so the
 * dynamic loader counts too */
static char marks[300];
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
	char *p = strrchr(b, ')');   /* field 22, starttime, counted after the command name */
	for (int i = 0; p && i < 20; i++) p = strchr(p + 1, ' ');
	if (p) start = strtoull(p + 1, NULL, 10);
	return up * 1000.0 - (double)start * 1000.0 / (double)sysconf(_SC_CLK_TCK);
}
static double mono_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
/* the first mark takes the time since exec (10 ms steps); later ones add exact differences */
void startup_mark(const char *what)
{
	static double base, mono0;
	if (!marks[0]) { base = since_exec_ms(); mono0 = mono_ms(); }
	size_t l = strlen(marks);
	snprintf(marks + l, sizeof(marks) - l, "%s%s %.0f", l ? ", " : "", what, base + mono_ms() - mono0);
}
const char *startup_marks(void) { return marks; }

static uint32_t tick(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void usage(void)
{
	printf("usage: emuzero-launcher [--theme NAME] [--themes] [--layout carousel|grid] [--list] [--kbd DEV] [--zoom N] [--keys K1,K2,...] [--shot FILE.png] [--version]\n");
}

int main(int argc, char **argv)
{
	struct plat_opts o = { .zoom = 3 };
	const char *theme_arg = NULL;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--version")) { printf("emuzero-launcher %s (%s)\n", EMUZERO_VERSION, plat_name()); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
		else if (!strcmp(a, "--list")) {   /* the apps the home screen shows, one per line */
			apps_scan();
			if (theme_arg) theme_load(theme_arg);   /* --theme first: which pictures it brings */
			for (int k = 0; k < napps; k++) {
				const char *f = icon_file(&apps[k]);
				printf("%s\t%s\t%s\n", apps[k].id, apps[k].name, !f ? "tile" : f == apps[k].icon ? "icon" : "theme");
			}
			return 0;
		}
		else if (!strcmp(a, "--kbd") && v) { o.kbd = v; i++; }
		else if (!strcmp(a, "--keys") && v) { o.keys = v; i++; }
		else if (!strcmp(a, "--shot") && v) { o.shot = v; i++; }
		else if (!strcmp(a, "--zoom") && v) { o.zoom = atoi(v); i++; }
		else if (!strcmp(a, "--theme") && v) { ui_set_theme(theme_arg = v); i++; }
		else if (!strcmp(a, "--themes")) {   /* the themes that can be chosen, one per line */
			char ids[32][32];
			int n = theme_list(ids, 32);
			for (int k = 0; k < n; k++) { theme_load(ids[k]); printf("%s\t%s\n", ids[k], theme.name); }
			return 0;
		}
		else if (!strcmp(a, "--layout") && v) {
			if (ui_set_layout(v)) { fprintf(stderr, "emuzero-launcher: no layout '%s' (carousel, grid)\n", v); return 2; }
			i++;
		}
		else { usage(); return 2; }
	}

	startup_mark("main");
	lv_init();
	lv_tick_set_cb(tick);
	if (!plat_display(&o)) { fprintf(stderr, "emuzero-launcher: no display\n"); return 1; }
	startup_mark("display");
	if (ui_fonts()) return 1;
	startup_mark("fonts");

	lv_group_t *g = lv_group_create();
	ui_create(g);
	startup_mark("home");
	lv_indev_t *kb = plat_keypad(&o);
	if (kb) lv_indev_set_group(kb, g);
	else fprintf(stderr, "emuzero-launcher: no keyboard\n");

	return plat_run(&o);
}
