// SPDX-License-Identifier: MIT
// One interface, three builds: the device (fbdev + evdev), a desktop window (SDL2), and a
// headless build that plays a key script and writes a PNG (for checks without a screen).
#pragma once
#include "lvgl/lvgl.h"

#define SCREEN_W 320
#define SCREEN_H 170

struct plat_opts {
	const char *kbd;   /* device: keyboard event node */
	const char *keys;  /* headless: comma-separated key script, e.g. "right,right,enter" */
	const char *shot;  /* headless: PNG written when the script ends */
	int zoom;          /* desktop: window scale */
};

const char *plat_name(void);
lv_display_t *plat_display(const struct plat_opts *o);
lv_indev_t *plat_keypad(const struct plat_opts *o);
int plat_run(const struct plat_opts *o);   /* main loop; returns the exit code */
void plat_quit(void);                      /* ends plat_run (desktop and headless only) */
