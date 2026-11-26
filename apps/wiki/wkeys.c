// SPDX-License-Identifier: MIT
// The one key table. Every screen looks its keys up here; the keys sheet (Fn+H) and the action
// menu (a tap of Alt) are made from it, so what they show is what the keys do.
//
// As in Notes: the arrows are on the Fn layer (Fn+F up, Fn+X down, Fn+Z left, Fn+C right, Fn+K
// Home, Fn+N End, Fn+L PgUp, Fn+M PgDn). Where there is nothing to type (the reader, lists, the
// triage questions) the plain letters move too.
#include "wiki.h"

#define H X_HOME
#define LI X_LIST
#define R X_READ
#define T X_TRIAGE
#define P X_PIC
#define S M_SHIFT
#define CT M_CTRL

const struct wbind wbinds[] = {
	{ H | LI | R | T, NK_UP, 0, W_UP, "Fn+F" },
	{ H | LI | R | T, NK_DOWN, 0, W_DOWN, "Fn+X" },
	{ P, NK_UP, 0, W_UP, "Fn+F" },
	{ P, NK_DOWN, 0, W_DOWN, "Fn+X" },
	{ P, NK_LEFT, 0, W_LEFT, "Fn+Z" },
	{ P, NK_RIGHT, 0, W_RIGHT, "Fn+C" },
	{ H, NK_TAB, 0, W_DOWN, "Tab" },
	{ H, NK_TAB, S, W_UP, "Shift+Tab" },
	{ LI | R | T, 'f', 0, W_UP, "F" },
	{ LI | R | T, 'x', 0, W_DOWN, "X" },
	{ P, 'f', 0, W_UP, "F" },
	{ P, 'x', 0, W_DOWN, "X" },
	{ P, 'z', 0, W_LEFT, "Z" },
	{ P, 'c', 0, W_RIGHT, "C" },
	{ H | LI | R, NK_PGUP, 0, W_PGUP, "Fn+L" },
	{ H | LI | R, NK_PGDN, 0, W_PGDN, "Fn+M" },
	{ LI | R, 'l', 0, W_PGUP, "L" },
	{ LI | R, 'm', 0, W_PGDN, "M" },
	{ R, ' ', 0, W_PGDN, "Space" },
	{ R, ' ', S, W_PGUP, "Shift+Space" },
	{ H | LI | R, NK_HOME, 0, W_TOP, "Fn+K" },
	{ H | LI | R, NK_END, 0, W_BOTTOM, "Fn+N" },
	{ LI | R, 'k', 0, W_TOP, "K" },
	{ LI | R, 'n', 0, W_BOTTOM, "N" },

	{ H | LI | R | T, NK_ENTER, 0, W_OPEN, "Enter" },
	{ H, NK_ENTER, CT, W_INSIDE, "Ctrl+Enter" },
	{ H, NK_ENTER, S, W_INSIDE, "Shift+Enter" },
	{ H, NK_ESC, 0, W_CLEAR, "Esc" },
	{ LI | R | T | P, NK_ESC, 0, W_BACK, "Esc" },
	{ LI | R | T | P, NK_BS, 0, W_BACK, "Backspace" },
	{ R, 'c', 0, W_CONTENTS, "C" },
	{ R, '/', 0, W_FIND, "/" },
	{ R, 'f', CT, W_FIND, "Ctrl+F" },
	{ R, 'b', 0, W_BOOKMARK, "B" },
	{ R, 'c', CT, W_COPY, "Ctrl+C" },
	{ P, '+', 0, W_ZOOM, "+" },
	{ P, '=', 0, W_ZOOM, "=" },
	{ P, '-', 0, W_ZOOM, "-" },
	{ P, NK_ENTER, 0, W_ZOOM, "Enter" },
	{ LI, NK_DEL, 0, W_DELETE, "Fn+Backspace" },

	{ H | LI | R | T, 't', CT, W_TRIAGE, "Ctrl+T" },
	{ H | LI | R | T, 0, 0, W_GUIDES, "" },
	{ H | LI | R | T, 0, 0, W_MANUAL, "" },
	{ H | LI | R | T, 0, 0, W_RECENT, "" },
	{ H | LI | R | T, 0, 0, W_MARKS, "" },
	{ H | LI | R | T, 'r', CT, W_RANDOM, "Ctrl+R" },
	{ LI | R | T | P, 's', CT, W_SEARCH, "Ctrl+S" },
	{ H | LI | R | T, 0, 0, W_ABOUT, "" },
	{ H | LI | R | T | P, NK_F1, 0, W_SIZE, "Fn+1" },
	{ H | LI | R | T | P, NK_ALT_TAP, 0, W_MENU, "Alt" },
	{ H | LI | R | T | P, NK_HELP, 0, W_HELP, "Fn+H" },
	{ H | LI | R | T | P, 'q', CT, W_QUIT, "Ctrl+Q" },
	{ 0, 0, 0, 0, NULL }
};

static const char *names[W_COUNT] = {
	[W_UP] = "Up", [W_DOWN] = "Down", [W_LEFT] = "Left", [W_RIGHT] = "Right", [W_PGUP] = "Page up",
	[W_PGDN] = "Page down", [W_TOP] = "Top", [W_BOTTOM] = "End",
	[W_OPEN] = "Open", [W_BACK] = "Back", [W_INSIDE] = "Search inside articles", [W_CLEAR] = "Clear / quit",
	[W_CONTENTS] = "Contents", [W_FIND] = "Find in article", [W_BOOKMARK] = "Bookmark on/off",
	[W_COPY] = "Copy article text", [W_SIZE] = "Text size", [W_ZOOM] = "Zoom", [W_RANDOM] = "Random article",
	[W_TRIAGE] = "What is happening?", [W_GUIDES] = "Survival guides", [W_MANUAL] = "Army Survival Manual",
	[W_RECENT] = "Recently read", [W_MARKS] = "Bookmarks", [W_SEARCH] = "Back to search", [W_ABOUT] = "About and licences",
	[W_DELETE] = "Remove from list", [W_MENU] = "Action menu", [W_HELP] = "Keys", [W_QUIT] = "Quit",
};

static const uint8_t menu_home[] = { W_INSIDE, W_TRIAGE, W_GUIDES, W_MANUAL, W_RECENT, W_MARKS, W_RANDOM, W_SIZE, W_ABOUT, W_HELP, W_QUIT, 0 };
static const uint8_t menu_list[] = { W_OPEN, W_DELETE, W_SEARCH, W_TRIAGE, W_GUIDES, W_MANUAL, W_RECENT, W_MARKS, W_SIZE, W_ABOUT, W_HELP, W_QUIT, 0 };
static const uint8_t menu_read[] = { W_CONTENTS, W_FIND, W_BOOKMARK, W_COPY, W_SEARCH, W_RANDOM, W_SIZE, W_TRIAGE, W_RECENT, W_MARKS, W_ABOUT, W_HELP, W_QUIT, 0 };
static const uint8_t menu_tri[] = { W_SEARCH, W_GUIDES, W_SIZE, W_HELP, W_QUIT, 0 };
static const uint8_t menu_pic[] = { W_ZOOM, W_BACK, W_SEARCH, W_HELP, W_QUIT, 0 };

const char *wcmd_name(int cmd) { return cmd > 0 && cmd < W_COUNT && names[cmd] ? names[cmd] : "?"; }

int wcmd_lookup(int ctx, struct nkey k)
{
	unsigned m = k.mods & (M_SHIFT | M_CTRL | M_ALT);
	int printable = k.k < 0x110000 && k.k != ' ';
	if (printable && !(m & (M_CTRL | M_ALT))) m = 0;   /* a typed 'A' or '?' carries its Shift already */
	for (const struct wbind *b = wbinds; b->label; b++)
		if ((b->ctx & ctx) && b->key && b->key == k.k && b->mods == m) return b->cmd;
	return 0;
}

const char *wcmd_keys(int ctx, int cmd)
{
	for (const struct wbind *b = wbinds; b->label; b++)
		if ((b->ctx & ctx) && b->cmd == cmd && b->label[0]) return b->label;
	return "";
}

const uint8_t *wcmd_menu(int ctx)
{
	switch (ctx) {
	case X_HOME: return menu_home;
	case X_LIST: return menu_list;
	case X_READ: return menu_read;
	case X_TRIAGE: return menu_tri;
	default: return menu_pic;
	}
}
