// SPDX-License-Identifier: MIT
// emuZero launcher: a thin front end for NimbleZero.
//
//   emuzero-launcher [--kbd /dev/input/eventN]           device
//   emuzero-launcher [--zoom N]                          desktop window
//   emuzero-launcher --keys right,enter --shot out.png   headless
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"
#include "ui.h"

#ifndef EMUZERO_VERSION
#define EMUZERO_VERSION "dev"
#endif

static uint32_t tick(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void usage(void)
{
	printf("usage: emuzero-launcher [--kbd DEV] [--zoom N] [--keys K1,K2,...] [--shot FILE.png] [--version]\n");
}

int main(int argc, char **argv)
{
	struct plat_opts o = { .kbd = "/dev/input/by-path/platform-3f804000.i2c-event", .zoom = 3 };
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--version")) { printf("emuzero-launcher %s (%s)\n", EMUZERO_VERSION, plat_name()); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
		else if (!strcmp(a, "--kbd") && v) { o.kbd = v; i++; }
		else if (!strcmp(a, "--keys") && v) { o.keys = v; i++; }
		else if (!strcmp(a, "--shot") && v) { o.shot = v; i++; }
		else if (!strcmp(a, "--zoom") && v) { o.zoom = atoi(v); i++; }
		else { usage(); return 2; }
	}

	lv_init();
	lv_tick_set_cb(tick);
	if (!plat_display(&o)) { fprintf(stderr, "emuzero-launcher: no display\n"); return 1; }
	if (ui_fonts()) return 1;

	lv_group_t *g = lv_group_create();
	ui_create(g);
	lv_indev_t *kb = plat_keypad(&o);
	if (kb) lv_indev_set_group(kb, g);
	else fprintf(stderr, "emuzero-launcher: no keyboard\n");

	return plat_run(&o);
}
