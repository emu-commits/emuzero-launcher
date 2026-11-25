// SPDX-License-Identifier: MIT
// emuZero Notes: the screen and the keys, in three builds like the launcher (device, desktop
// window, headless). Notes needs every modifier (Shift, Ctrl, Alt, and the Fn and Sym layers),
// which an LVGL keypad cannot carry, so its keys come to the app as struct nkey instead.
#pragma once
#include "lvgl/lvgl.h"
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 170

/* special keys live above Unicode; anything below is a character to type */
enum {
	NK_UP = 0x110000, NK_DOWN, NK_LEFT, NK_RIGHT, NK_HOME, NK_END, NK_PGUP, NK_PGDN,
	NK_ENTER, NK_ESC, NK_BS, NK_DEL, NK_TAB, NK_HELP, NK_INSERT,
	NK_ALT_TAP,   /* Alt pressed and let go with no other key between: the action menu */
	NK_F1, NK_F2, NK_F3, NK_F4, NK_F5, NK_F6, NK_F7, NK_F8, NK_F9, NK_F10,
};
enum { M_SHIFT = 1, M_CTRL = 2, M_ALT = 4, M_FN = 8, M_SYM = 16 };

struct nkey {
	uint32_t k;      /* a character (Shift already applied) or NK_* */
	unsigned mods;   /* M_* held when the key went down; for Ctrl/Alt chords k is the lower-case letter */
};

struct nplat_opts {
	const char *kbd;     /* device: only this event node */
	const char *script;  /* headless: script file (see nplat_headless.c) */
	const char *keys;    /* headless: comma-separated key specs */
	const char *shot;    /* headless: PNG written at the end */
	int zoom;            /* desktop: window scale */
};

const char *nplat_name(void);
uint32_t nplat_tick(void);                       /* ms clock for LVGL */
lv_display_t *nplat_display(const struct nplat_opts *o);
/* keys go to key_cb; mods_cb is told whenever the held modifiers change (status bar badges) */
void nplat_keys(const struct nplat_opts *o, void (*key_cb)(struct nkey), void (*mods_cb)(unsigned));
int nplat_run(const struct nplat_opts *o, int (*should_quit)(void));
/* a key spec like "ctrl+shift+left", "enter", "a" into a key; 0 when it is not one */
int nplat_parse_key(const char *spec, struct nkey *out);
