// SPDX-License-Identifier: MIT
// Desktop build: the 320x170 screen in an SDL2 window (scaled by --zoom), keys from the PC
// keyboard with their modifiers. F1 is Help (the device's Fn+H); the Menu key or Right Alt
// tapped alone opens the action menu, like a tap of Alt on the device.
#include "nplat.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <unistd.h>

static void (*key_fn)(struct nkey);
static void (*mods_fn)(unsigned);
static int alt_alone;

const char *nplat_name(void) { return "desktop"; }
uint32_t nplat_tick(void) { return SDL_GetTicks(); }

lv_display_t *nplat_display(const struct nplat_opts *o)
{
	lv_display_t *d = lv_sdl_window_create(SCREEN_W, SCREEN_H);
	if (!d) return NULL;
	lv_sdl_window_set_zoom(d, o->zoom > 0 ? o->zoom : 1);
	lv_sdl_window_set_title(d, "emuZero Notes");
	return d;
}

static unsigned sdl_mods(Uint16 m)
{
	return (m & KMOD_SHIFT ? M_SHIFT : 0) | (m & KMOD_CTRL ? M_CTRL : 0) | (m & KMOD_ALT ? M_ALT : 0);
}

static int watch(void *u, SDL_Event *e)
{
	(void)u;
	if (e->type == SDL_TEXTINPUT) {
		if (SDL_GetModState() & (KMOD_CTRL | KMOD_ALT)) return 0;
		const unsigned char *s = (const unsigned char *)e->text.text;
		while (*s) {
			uint32_t c; int n;
			if (s[0] < 0x80) { c = s[0]; n = 1; }
			else if ((s[0] & 0xe0) == 0xc0) { c = (uint32_t)(s[0] & 0x1f) << 6 | (s[1] & 0x3f); n = 2; }
			else if ((s[0] & 0xf0) == 0xe0) { c = (uint32_t)(s[0] & 0x0f) << 12 | (uint32_t)(s[1] & 0x3f) << 6 | (s[2] & 0x3f); n = 3; }
			else { c = (uint32_t)(s[0] & 0x07) << 18 | (uint32_t)(s[1] & 0x3f) << 12 | (uint32_t)(s[2] & 0x3f) << 6 | (s[3] & 0x3f); n = 4; }
			key_fn((struct nkey){ c, sdl_mods(SDL_GetModState()) });
			s += n;
		}
		return 0;
	}
	if (e->type == SDL_KEYUP) {
		SDL_Keycode k = e->key.keysym.sym;
		if ((k == SDLK_LALT || k == SDLK_RALT) && alt_alone) { alt_alone = 0; key_fn((struct nkey){ NK_ALT_TAP, 0 }); }
		if (mods_fn) mods_fn(sdl_mods(e->key.keysym.mod));
		return 0;
	}
	if (e->type != SDL_KEYDOWN) return 0;
	SDL_Keycode k = e->key.keysym.sym;
	unsigned m = sdl_mods(e->key.keysym.mod);
	if (mods_fn) mods_fn(m);
	if (k == SDLK_LALT || k == SDLK_RALT) { if (!e->key.repeat) alt_alone = 1; return 0; }
	if (k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LSHIFT || k == SDLK_RSHIFT) return 0;
	alt_alone = 0;
	uint32_t c = 0;
	switch (k) {
	case SDLK_UP: c = NK_UP; break;
	case SDLK_DOWN: c = NK_DOWN; break;
	case SDLK_LEFT: c = NK_LEFT; break;
	case SDLK_RIGHT: c = NK_RIGHT; break;
	case SDLK_HOME: c = NK_HOME; break;
	case SDLK_END: c = NK_END; break;
	case SDLK_PAGEUP: c = NK_PGUP; break;
	case SDLK_PAGEDOWN: c = NK_PGDN; break;
	case SDLK_RETURN: case SDLK_KP_ENTER: c = NK_ENTER; break;
	case SDLK_ESCAPE: c = NK_ESC; break;
	case SDLK_BACKSPACE: c = NK_BS; break;
	case SDLK_DELETE: c = NK_DEL; break;
	case SDLK_TAB: c = NK_TAB; break;
	case SDLK_F1: case SDLK_HELP: c = NK_HELP; break;
	case SDLK_MENU: case SDLK_APPLICATION: c = NK_ALT_TAP; break;
	case SDLK_INSERT: c = NK_INSERT; break;
	default:
		/* printable keys come as SDL_TEXTINPUT, except in Ctrl/Alt chords */
		if ((m & (M_CTRL | M_ALT)) && k >= 32 && k < 127) c = (uint32_t)(k >= 'A' && k <= 'Z' ? k + 32 : k);
		else if ((m & (M_CTRL | M_ALT)) && k == SDLK_SPACE) c = ' ';
	}
	if (c) key_fn((struct nkey){ c, m });
	return 0;
}

void nplat_keys(const struct nplat_opts *o, void (*key_cb)(struct nkey), void (*mods_cb)(unsigned))
{
	(void)o;
	key_fn = key_cb;
	mods_fn = mods_cb;
	SDL_AddEventWatch(watch, NULL);
	SDL_StartTextInput();
}

int nplat_run(const struct nplat_opts *o, int (*should_quit)(void))
{
	(void)o;
	while (!should_quit()) {
		uint32_t d = lv_timer_handler();
		usleep((d == LV_NO_TIMER_READY || d > 20 ? 20 : d) * 1000);
	}
	lv_sdl_quit();
	return 0;
}
