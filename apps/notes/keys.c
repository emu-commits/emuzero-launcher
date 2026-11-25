// SPDX-License-Identifier: MIT
// The one key table. Every screen looks its keys up here, and the keys sheet (Fn+H) and the
// action menu (a tap of Alt) are made from it, so what they show is always what the keys do.
//
// On the CardputerZero the arrows are on the Fn layer: Fn+F up, Fn+X down, Fn+Z left, Fn+C
// right, Fn+K Home, Fn+N End, Fn+L PgUp, Fn+M PgDn, Fn+Backspace Delete, Fn+H Help. Tap Fn for
// one key, double-tap it to lock. View mode has nothing to type into, so there the plain
// letters move too, as on the launcher's home screen.
#include "notes.h"
#include <string.h>

#define L CTX_LIST
#define V CTX_VIEW
#define E CTX_EDIT
#define S M_SHIFT
#define CT M_CTRL
#define A M_ALT

const struct bind binds[] = {
	/* moving: the list and both note modes */
	{ L | V | E, NK_UP, 0, C_UP, "Fn+F" },
	{ L | V | E, NK_DOWN, 0, C_DOWN, "Fn+X" },
	{ E, NK_LEFT, 0, C_LEFT, "Fn+Z" },
	{ E, NK_RIGHT, 0, C_RIGHT, "Fn+C" },
	{ L | V | E, NK_PGUP, 0, C_PGUP, "Fn+L" },
	{ L | V | E, NK_PGDN, 0, C_PGDN, "Fn+M" },
	{ E, NK_HOME, 0, C_HOME, "Fn+K" },
	{ E, NK_END, 0, C_END, "Fn+N" },
	{ L | V, NK_HOME, 0, C_TOP, "Fn+K" },
	{ L | V, NK_END, 0, C_BOTTOM, "Fn+N" },
	{ L, NK_TAB, 0, C_DOWN, "Tab" },
	{ L, NK_TAB, S, C_UP, "Shift+Tab" },
	{ V, 'f', 0, C_UP, "F" },
	{ V, 'x', 0, C_DOWN, "X" },
	{ V, 'l', 0, C_PGUP, "L" },
	{ V, 'm', 0, C_PGDN, "M" },
	{ V, 'k', 0, C_TOP, "K" },
	{ V, 'n', 0, C_BOTTOM, "N" },
	{ E, NK_LEFT, CT, C_WORD_L, "Ctrl+Fn+Z" },
	{ E, NK_RIGHT, CT, C_WORD_R, "Ctrl+Fn+C" },
	{ E, NK_HOME, CT, C_TOP, "Ctrl+Fn+K" },
	{ E, NK_END, CT, C_BOTTOM, "Ctrl+Fn+N" },
	{ V | E, NK_UP, CT, C_TOP, "Ctrl+Fn+F" },
	{ V | E, NK_DOWN, CT, C_BOTTOM, "Ctrl+Fn+X" },

	/* editing */
	{ E, NK_BS, 0, C_BS, "Backspace" },
	{ E, NK_DEL, 0, C_DEL, "Fn+Backspace" },
	{ E, NK_BS, CT, C_DEL_WORD_L, "Ctrl+Backspace" },
	{ E, NK_DEL, CT, C_DEL_WORD_R, "Ctrl+Fn+Backspace" },
	{ E, NK_ENTER, 0, C_ENTER, "Enter" },
	{ E, NK_TAB, 0, C_TAB, "Tab" },
	{ E, NK_TAB, S, C_OUTDENT, "Shift+Tab" },
	{ E, 'z', CT, C_UNDO, "Ctrl+Z" },
	{ E, 'y', CT, C_REDO, "Ctrl+Y" },
	{ E, 'z', CT | S, C_REDO, "Ctrl+Shift+Z" },
	{ L, 'z', CT, C_UNDO, "Ctrl+Z" },
	{ V, 'z', CT, C_UNDO, "Ctrl+Z" },

	/* checklists and formatting */
	{ E, 'l', CT, C_CHECKLIST, "Ctrl+L" },
	{ V | E, NK_ENTER, CT, C_TICK, "Ctrl+Enter" },
	{ V, ' ', 0, C_TICK, "Space" },
	{ E, NK_UP, A, C_MOVE_UP, "Alt+Fn+F" },
	{ E, NK_DOWN, A, C_MOVE_DOWN, "Alt+Fn+X" },
	{ E, 'h', CT, C_HEADING, "Ctrl+H" },
	{ E, 'b', CT, C_BOLD, "Ctrl+B" },
	{ E, 'i', CT, C_ITALIC, "Ctrl+I" },
	{ V | E, 0, 0, C_TICKED_BOTTOM, "" },

	/* selecting, copy and paste */
	{ E, ' ', CT, C_SEL_MORE, "Ctrl+Space" },
	{ E, 'a', CT, C_SEL_ALL, "Ctrl+A" },
	{ V | E, 'c', CT, C_COPY, "Ctrl+C" },
	{ E, 'x', CT, C_CUT, "Ctrl+X" },
	{ E, 'v', CT, C_PASTE, "Ctrl+V" },
	{ E, 'v', CT | S, C_PASTE_HIST, "Ctrl+Shift+V" },

	/* notes */
	{ L, NK_ENTER, 0, C_OPEN, "Enter" },
	{ V, NK_ENTER, 0, C_EDIT_HERE, "Enter" },
	{ V, 'e', 0, C_EDIT_HERE, "E" },
	{ V | E, 'e', CT, C_MODE, "Ctrl+E" },
	{ L | V | E, 'n', CT, C_NEW, "Ctrl+N" },
	{ L | V | E, NK_ESC, 0, C_BACK, "Esc" },
	{ E, 's', CT, C_SAVE, "Ctrl+S" },
	{ V | E, 'f', CT, C_FIND, "Ctrl+F" },
	{ L | V, 'p', CT, C_PIN, "Ctrl+P" },
	{ L | V, NK_DEL, 0, C_DELETE_NOTE, "Fn+Backspace" },
	{ L | V | E, 0, 0, C_DUPLICATE, "" },
	{ V | E, 0, 0, C_REVERT, "" },
	{ L, 0, 0, C_TRASH, "" },
	{ L | V | E, NK_F1, 0, C_SIZE, "Fn+1" },
	{ L | V | E, 0, 0, C_AUTO_TICKED, "" },
	{ L | V | E, 'g', CT, C_GUIDE, "Ctrl+G" },
	{ L | V | E, NK_ALT_TAP, 0, C_MENU, "Alt" },
	{ L | V | E, NK_HELP, 0, C_HELP, "Fn+H" },
	{ L | V | E, 'q', CT, C_QUIT, "Ctrl+Q" },
	{ 0, 0, 0, 0, NULL }
};

static const char *names[C_COUNT] = {
	[C_UP] = "Up", [C_DOWN] = "Down", [C_LEFT] = "Left", [C_RIGHT] = "Right",
	[C_HOME] = "Start of line", [C_END] = "End of line", [C_PGUP] = "Page up", [C_PGDN] = "Page down",
	[C_WORD_L] = "Word left", [C_WORD_R] = "Word right", [C_TOP] = "Top", [C_BOTTOM] = "Bottom",
	[C_BS] = "Delete back", [C_DEL] = "Delete forward", [C_DEL_WORD_L] = "Delete word back",
	[C_DEL_WORD_R] = "Delete word forward", [C_ENTER] = "New line", [C_TAB] = "Indent",
	[C_OUTDENT] = "Outdent", [C_UNDO] = "Undo", [C_REDO] = "Redo",
	[C_CHECKLIST] = "Checklist on/off", [C_TICK] = "Tick / untick", [C_MOVE_UP] = "Move line up",
	[C_MOVE_DOWN] = "Move line down", [C_HEADING] = "Heading / body", [C_BOLD] = "Bold", [C_ITALIC] = "Italic",
	[C_SEL_MORE] = "Select more", [C_SEL_ALL] = "Select all", [C_COPY] = "Copy", [C_CUT] = "Cut",
	[C_PASTE] = "Paste", [C_PASTE_HIST] = "Paste from history", [C_TICKED_BOTTOM] = "Move ticked to bottom",
	[C_OPEN] = "Open", [C_NEW] = "New note", [C_BACK] = "Back", [C_SAVE] = "Save now",
	[C_FIND] = "Find in note", [C_MODE] = "View / Edit", [C_EDIT_HERE] = "Edit here",
	[C_PIN] = "Pin / unpin", [C_DELETE_NOTE] = "Delete note", [C_DUPLICATE] = "Duplicate note",
	[C_REVERT] = "Revert to when opened", [C_TRASH] = "Recently Deleted", [C_SIZE] = "Text size",
	[C_AUTO_TICKED] = "Ticked go to bottom", [C_GUIDE] = "Markdown guide", [C_MENU] = "Action menu",
	[C_HELP] = "Keys", [C_QUIT] = "Quit",
};

/* commands the action menu offers, per screen, in order */
static const uint8_t menu_list[] = { C_OPEN, C_NEW, C_PIN, C_DUPLICATE, C_DELETE_NOTE, C_TRASH, C_SIZE, C_AUTO_TICKED, C_GUIDE, C_HELP, C_QUIT, 0 };
static const uint8_t menu_view[] = { C_EDIT_HERE, C_TICK, C_COPY, C_FIND, C_TICKED_BOTTOM, C_PIN, C_DUPLICATE, C_REVERT, C_DELETE_NOTE, C_NEW, C_SIZE, C_AUTO_TICKED, C_GUIDE, C_HELP, 0 };
static const uint8_t menu_edit[] = { C_COPY, C_CUT, C_PASTE, C_PASTE_HIST, C_SEL_MORE, C_SEL_ALL, C_CHECKLIST, C_TICK, C_TICKED_BOTTOM,
	C_HEADING, C_BOLD, C_ITALIC, C_MOVE_UP, C_MOVE_DOWN, C_FIND, C_MODE, C_UNDO, C_REDO, C_DUPLICATE, C_REVERT, C_NEW, C_SIZE, C_AUTO_TICKED, C_GUIDE, C_HELP, 0 };

const char *cmd_name(int cmd) { return cmd > 0 && cmd < C_COUNT && names[cmd] ? names[cmd] : "?"; }

int cmd_lookup(int ctx, struct nkey k, int *shifted)
{
	unsigned m = k.mods & (M_SHIFT | M_CTRL | M_ALT);
	if (shifted) *shifted = 0;
	/* characters typed with Shift carry it already (an 'A', a '?'): match without it */
	int printable = k.k < 0x110000 && k.k != ' ';
	if (printable && !(m & (M_CTRL | M_ALT))) m = 0;
	for (const struct bind *b = binds; b->label; b++)
		if ((b->ctx & ctx) && b->key && b->key == k.k && b->mods == m) return b->cmd;
	if (m & M_SHIFT)   /* Shift + a moving key: the same move, extending the selection */
		for (const struct bind *b = binds; b->label; b++)
			if ((b->ctx & ctx) && b->key && b->key == k.k && b->mods == (m & ~M_SHIFT)) { if (shifted) *shifted = 1; return b->cmd; }
	return 0;
}

const char *cmd_keys(int ctx, int cmd)
{
	for (const struct bind *b = binds; b->label; b++)
		if ((b->ctx & ctx) && b->cmd == cmd && b->label[0]) return b->label;
	return "";
}

int cmd_in_menu(int ctx, int cmd)
{
	const uint8_t *l = ctx == CTX_LIST ? menu_list : ctx == CTX_VIEW ? menu_view : menu_edit;
	for (; *l; l++) if (*l == cmd) return 1;
	return 0;
}

const uint8_t *cmd_menu(int ctx) { return ctx == CTX_LIST ? menu_list : ctx == CTX_VIEW ? menu_view : menu_edit; }
