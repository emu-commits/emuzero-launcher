// SPDX-License-Identifier: MIT
// Desktop build: the same 320x170 screen in an SDL2 window (scaled by --zoom), keys from the
// PC keyboard. Arrows, Enter and Esc map to the keys the device sends.
#include "platform.h"
#include <unistd.h>

static int quit;

const char *plat_name(void) { return "desktop"; }

lv_display_t *plat_display(const struct plat_opts *o)
{
	lv_display_t *d = lv_sdl_window_create(SCREEN_W, SCREEN_H);
	if (!d) return NULL;
	lv_sdl_window_set_zoom(d, o->zoom > 0 ? o->zoom : 1);
	lv_sdl_window_set_title(d, "emuZero launcher");
	return d;
}

lv_indev_t *plat_keypad(const struct plat_opts *o)
{
	(void)o;
	return lv_sdl_keyboard_create();
}

void plat_quit(void) { quit = 1; }

int plat_run(const struct plat_opts *o)
{
	(void)o;
	while (!quit) {
		uint32_t d = lv_timer_handler();
		usleep((d == LV_NO_TIMER_READY || d > 30 ? 30 : d) * 1000);
	}
	lv_sdl_quit();
	return 0;
}
