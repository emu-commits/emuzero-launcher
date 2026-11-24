// SPDX-License-Identifier: MIT
// One interface, three builds: the device (fbdev + evdev), a desktop window (SDL2), and a
// headless build that plays a key script and writes a PNG (for checks without a screen).
#pragma once
#include "lvgl/lvgl.h"

#define SCREEN_W 320
#define SCREEN_H 170

struct plat_opts {
	const char *kbd;   /* device: only this keyboard event node (default: every keyboard) */
	const char *keys;  /* headless: comma-separated key script, e.g. "right,right,enter" */
	const char *shot;  /* headless: PNG written when the script ends */
	int zoom;          /* desktop: window scale */
};

const char *plat_name(void);
lv_display_t *plat_display(const struct plat_opts *o);
lv_indev_t *plat_keypad(const struct plat_opts *o);
int plat_run(const struct plat_opts *o);   /* main loop; returns the exit code */
void plat_quit(void);                      /* ends plat_run (desktop and headless only) */

/* Run an app in the foreground and wait for it: the command line goes to /bin/sh -c, in its
 * own process group. On the device the app gets the screen and the keys (a terminal app gets
 * the text console, a graphical app's output goes to /tmp/emuzero-app.log); the screen
 * still goes dark after idle_s seconds without a key (0 = never), and Ctrl+Alt+Backspace
 * stops the app. Returns its exit status, 128 + signal when it was killed, -1 when it could
 * not be started. The other builds only print the command. */
struct plat_app { const char *exec, *dir; int terminal, idle_s; };
int plat_exec(const struct plat_app *a);
extern uint32_t plat_exec_end;             /* lv_tick_get() when the last app ended */
/* screen off (backlight and panel) until a key; returns once that key is let go, and the key
 * does nothing else. The other builds return at once. */
void plat_sleep(void);
