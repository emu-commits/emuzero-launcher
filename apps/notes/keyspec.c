// SPDX-License-Identifier: MIT
// Key specs for scripts and tests: "ctrl+shift+left", "alt+up", "enter", "a", "A", "space".
#include "nplat.h"
#include <string.h>
#include <strings.h>

int nplat_parse_key(const char *spec, struct nkey *out)
{
	static const struct { const char *name; uint32_t k; } names[] = {
		{ "up", NK_UP }, { "down", NK_DOWN }, { "left", NK_LEFT }, { "right", NK_RIGHT },
		{ "home", NK_HOME }, { "end", NK_END }, { "pgup", NK_PGUP }, { "pgdn", NK_PGDN },
		{ "enter", NK_ENTER }, { "esc", NK_ESC }, { "bs", NK_BS }, { "backspace", NK_BS },
		{ "del", NK_DEL }, { "delete", NK_DEL }, { "tab", NK_TAB }, { "help", NK_HELP },
		{ "insert", NK_INSERT }, { "alt", NK_ALT_TAP }, { "space", ' ' }, { "comma", ',' },
		{ "plus", '+' }, { "f1", NK_F1 }, { "f2", NK_F2 }, { "f3", NK_F3 }, { "f4", NK_F4 }, { "f5", NK_F5 },
	};
	struct nkey k = { 0, 0 };
	const char *s = spec;
	for (;;) {
		const char *p = strchr(s, '+');
		if (!p || p == s || !p[1]) break;   /* "+" alone or a trailing '+' is the key itself */
		size_t n = (size_t)(p - s);
		if (n == 4 && !strncasecmp(s, "ctrl", 4)) k.mods |= M_CTRL;
		else if (n == 5 && !strncasecmp(s, "shift", 5)) k.mods |= M_SHIFT;
		else if (n == 3 && !strncasecmp(s, "alt", 3)) k.mods |= M_ALT;
		else if (n == 2 && !strncasecmp(s, "fn", 2)) k.mods |= M_FN;
		else return 0;
		s = p + 1;
	}
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcasecmp(s, names[i].name)) { k.k = names[i].k; *out = k; return 1; }
	/* one character, possibly UTF-8 */
	const unsigned char *u = (const unsigned char *)s;
	uint32_t c;
	if (u[0] < 0x80) { if (u[1]) return 0; c = u[0]; }
	else if ((u[0] & 0xe0) == 0xc0 && u[1] && !u[2]) c = (uint32_t)(u[0] & 0x1f) << 6 | (u[1] & 0x3f);
	else if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2] && !u[3]) c = (uint32_t)(u[0] & 0x0f) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
	else return 0;
	if (!c) return 0;
	/* Ctrl/Alt chords carry the lower-case letter and Shift as a modifier, as the device sends them */
	if ((k.mods & (M_CTRL | M_ALT)) && c >= 'A' && c <= 'Z') { c += 32; k.mods |= M_SHIFT; }
	k.k = c;
	*out = k;
	return 1;
}
