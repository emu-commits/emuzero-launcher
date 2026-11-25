// SPDX-License-Identifier: MIT
// emuZero Notes: everything the modules share. See NOTES-APP-PLAN.md (cardputerzero-os) for
// the design; README.md in this folder for how to build, run and test.
#pragma once
#include "nplat.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define BAR_H 16           /* status bar */
#define MARGIN 6           /* text margin, left and right */

/* ---- textbuf.c: gap buffer + UTF-8 ---- */
struct tb { char *b; size_t cap, gs, ge; };
void tb_init(struct tb *t);
void tb_free(struct tb *t);
size_t tb_len(const struct tb *t);
char tb_at(const struct tb *t, size_t i);
int tb_insert(struct tb *t, size_t pos, const char *s, size_t n);
void tb_delete(struct tb *t, size_t pos, size_t n);
const char *tb_range(struct tb *t, size_t start, size_t end);   /* contiguous; valid until the next edit */
char *tb_copy(const struct tb *t, size_t start, size_t end);     /* malloc'd, NUL-terminated */
void tb_set(struct tb *t, const char *s, size_t n);
size_t tb_next(const struct tb *t, size_t i);                    /* next / previous character boundary */
size_t tb_prev(const struct tb *t, size_t i);
uint32_t u8_dec(const char *s, size_t n, size_t *i);
int u8_enc(uint32_t c, char *out);
size_t u8_cut(const char *s, size_t n, size_t max);
size_t u8_whole(const char *s, size_t n);

/* ---- md.c: Markdown lines and inline marks ---- */
enum lkind { LK_TEXT, LK_BLANK, LK_HEAD, LK_BULLET, LK_NUM, LK_TASK, LK_QUOTE, LK_RULE, LK_FENCE, LK_CODE };
struct linfo { int kind, lead, mlen, level, checked, num; };
void line_info(const char *s, size_t n, int code, struct linfo *li);
enum { ST_BOLD = 1, ST_ITAL = 2, ST_CODE = 4, ST_STRIKE = 8, ST_LINK = 16 };
void md_inline(const char *s, size_t n, size_t base, int style, void (*emit)(void *, size_t off, size_t len, int style, int hidden), void *u);
size_t md_plain_line(const char *s, size_t n, char *out, size_t max);

/* ---- doc.c: the text being edited, its lines, undo ---- */
struct para {
	size_t start, len;
	uint8_t code;      /* 0 outside a ``` block, 1 inside, 2 its closing fence */
	uint32_t ver;      /* changes whenever the line does: layouts are kept per version */
	void *lay;         /* view.c's layout of this line, or NULL */
};
struct undo_op { uint8_t ins; size_t pos; char *s; size_t n; uint32_t group; };
struct doc {
	struct tb tb;
	struct para *p;
	int np, pcap;
	uint32_t ver;
	struct undo_op *u;
	int nu, utop, ucap;          /* done: [0, nu); can be redone: [nu, utop) */
	uint32_t group, last_ms;
	size_t ubytes;
	int merge, batch;
	int dirty;                   /* changed since the last save */
	unsigned changes;            /* counts every change (for "has it changed since") */
};
void doc_init(struct doc *d);
void doc_free(struct doc *d);
void doc_load(struct doc *d, const char *s, size_t n);
void doc_reindex(struct doc *d);
int doc_para_at(const struct doc *d, size_t pos);
const char *doc_para_text(struct doc *d, int i, size_t *n);
void doc_edit(struct doc *d, size_t pos, size_t del, const char *ins, size_t n, int merge, uint32_t now);
void doc_group(struct doc *d);
void doc_batch(struct doc *d, int on);   /* edits between on and off undo as one */
int doc_undo(struct doc *d, size_t *cur);
int doc_redo(struct doc *d, size_t *cur);
char *doc_text(struct doc *d, size_t *n);

/* ---- fonts.c + draw.c ---- */
enum { F_REG, F_BOLD, F_ITAL, F_BOLDITAL, F_MONO, F_H1, F_H2, F_H3, F_UI, F_UIBOLD, F_UISMALL, F_COUNT };
struct palette {
	uint32_t bg, fg, dim, line, accent;
	uint32_t sel_bg, sel_fg, sel_dim;   /* the theme's selected row (lists, menus) and dim text on it */
	uint32_t text_sel, focus;           /* selected text in a note; View mode's highlighted line */
	uint32_t bar_bg, bar_fg, bar_line, title_fg, code_bg, mark_bg, warn, ok, shadow_c;
};
extern struct palette pal;
extern int text_size;                 /* 0 small, 1 medium, 2 large */
int fonts_init(const char *theme_id);
void fonts_set_size(int size);
const lv_font_t *font(int f);
int font_lh(int f);
int font_asc(int f);                  /* line height above the baseline */
int text_w(int f, const char *s, size_t n);
int glyph_w(int f, uint32_t c, uint32_t next);
lv_color_t C(uint32_t rgb);
void draw_begin(lv_layer_t *l);       /* once per draw event: resets the text arena */
void d_rect(int x, int y, int w, int h, uint32_t c);
void d_frame(int x, int y, int w, int h, int bw, uint32_t c);
void d_rrect(int x, int y, int w, int h, int r, uint32_t c);
void d_line(int x1, int y1, int x2, int y2, int width, uint32_t c);
int d_text(int f, int x, int y, uint32_t c, const char *s, size_t n);      /* returns the width */
int d_text_fit(int f, int x, int y, int maxw, uint32_t c, const char *s);   /* cut with … to fit */
int d_text_deco(int f, int x, int y, uint32_t c, const char *s, size_t n, int strike, int underline);
int d_clip_rows(int y, int h);        /* is [y, y+h) inside what is being drawn? */
void d_check(int x, int y, int size, int ticked);

/* ---- view.c: laying out and drawing a note ---- */
enum { MODE_VIEW, MODE_EDIT };
struct vrow { uint32_t start; int16_t x0; int16_t h; };
struct vdraw {
	int x, y, w, h;               /* the text area */
	int top_para, top_row;        /* first row shown */
	int mode;
	size_t cur; int show_cur;
	size_t s0, s1;                /* selection, s0 < s1 when there is one */
	const size_t *marks; int nmarks;   /* find matches: start/end pairs */
	int focus;                    /* View mode: highlighted line, -1 none */
	int plain;                    /* plain file: no title styling */
};
void view_invalidate_all(void);       /* sizes, mode or width changed */
int view_rows(struct doc *d, int i, int mode, int w, int plain, const struct vrow **rows);
int view_para_h(struct doc *d, int i, int mode, int w, int plain);
int view_row_h(struct doc *d, int i, int r, int mode, int w, int plain);
int view_nrows(struct doc *d, int i, int mode, int w, int plain);
void view_pos(struct doc *d, int mode, int w, int plain, size_t pos, int *para, int *row, int *x);
size_t view_hit(struct doc *d, int mode, int w, int plain, int para, int row, int x);
size_t view_snap(struct doc *d, size_t pos, int dir);   /* step out of a checkbox's hidden marks */
void view_draw(struct doc *d, const struct vdraw *v);

/* ---- store.c: the notes folder, saving, the state file ---- */
struct note {
	char file[256];
	char title[128];
	char snippet[160];
	time_t mtime;
	off_t size;
	char *body; size_t blen; time_t body_mtime; off_t body_size;   /* search cache */
	char *fold;                                                       /* folded body, for search */
};
struct trash_ent { char file[256]; time_t when; };
struct state {
	char last[256];              /* the note open when the app last stopped ("" = the list) */
	size_t cur, anc;
	int mode, top_para, top_row, focus;
	int size;                    /* text size */
	int ticked_bottom;           /* move ticked items to the bottom on their own */
	char pins[64][256]; int npins;
	struct trash_ent trash[256]; int ntrash;
};
extern struct state st;
extern struct note *notes; extern int nnotes;
extern char notes_dir[512];
int store_init(const char *dir);
void store_scan(void);
int store_find(const char *file);
int store_read_path(const char *path, char **out, size_t *n, char *err, size_t errn);
int store_read(const char *file, char **out, size_t *n);
int store_write_path(const char *path, const char *s, size_t n, char *err, size_t errn);
int store_write(const char *file, const char *s, size_t n, char *err, size_t errn);
void store_name_for(const char *text, size_t n, const char *current, char *out, size_t outn);
int store_rename(const char *from, const char *to);
int store_trash(const char *file);
int store_restore(int trash_index, char *out_file, size_t n);
int store_purge(int trash_index);
void store_purge_old(void);
int store_history(const char *file);        /* keep the note as it was when opened */
int store_history_read(const char *file, char **out, size_t *n);
int store_stat(const char *path, time_t *mtime, off_t *size);
void store_note_title(const char *s, size_t n, char *title, size_t tn, char *snip, size_t sn);
int is_pinned(const char *file);
void set_pinned(const char *file, int on);
void state_load(void);
void state_save(void);
void path_note(char *out, size_t n, const char *file);

/* ---- search.c ---- */
struct hit { int note; int score; char snip[160]; int hl[6][2]; int nhl; int title_hit; };
size_t fold_text(const char *s, size_t n, char *out);   /* lower case, accents off; out needs n + 1 */
int search_run(const char *query, struct hit *out, int max);

/* ---- clip.c ---- */
void clip_set(const char *s, size_t n);
char *clip_get(size_t *n);                 /* malloc'd; NULL when empty */
int clip_history(char **items, int max);   /* newest first; the strings stay owned by clip.c */

/* ---- keys.c: the one key table ---- */
enum { CTX_LIST = 1, CTX_VIEW = 2, CTX_EDIT = 4, CTX_ALL = 7 };
enum cmd {
	C_NONE,
	/* moving (Shift extends the selection in Edit mode) */
	C_UP, C_DOWN, C_LEFT, C_RIGHT, C_HOME, C_END, C_PGUP, C_PGDN, C_WORD_L, C_WORD_R, C_TOP, C_BOTTOM,
	/* editing */
	C_BS, C_DEL, C_DEL_WORD_L, C_DEL_WORD_R, C_ENTER, C_TAB, C_OUTDENT, C_UNDO, C_REDO,
	C_CHECKLIST, C_TICK, C_MOVE_UP, C_MOVE_DOWN, C_HEADING, C_BOLD, C_ITALIC,
	C_SEL_MORE, C_SEL_ALL, C_COPY, C_CUT, C_PASTE, C_PASTE_HIST, C_TICKED_BOTTOM,
	/* notes */
	C_OPEN, C_NEW, C_BACK, C_SAVE, C_FIND, C_MODE, C_EDIT_HERE, C_PIN, C_DELETE_NOTE, C_DUPLICATE,
	C_REVERT, C_TRASH, C_SIZE, C_AUTO_TICKED, C_GUIDE, C_MENU, C_HELP, C_QUIT,
	C_COUNT
};
struct bind { uint8_t ctx; uint32_t key; uint8_t mods; uint8_t cmd; const char *label; };
extern const struct bind binds[];
const char *cmd_name(int cmd);
int cmd_lookup(int ctx, struct nkey k, int *shifted);
const char *cmd_keys(int ctx, int cmd);   /* the first key label for cmd, or "" */
int cmd_in_menu(int ctx, int cmd);

/* ---- ui.c: status bar, overlays ---- */
struct mitem { char label[96]; char hint[24]; int id; int off; };
void bar_draw(const char *title, const char *state, uint32_t state_color);
void toast(const char *msg);
int overlay_active(void);
int overlay_key(struct nkey k);
void overlay_draw(void);
void menu_open(const char *title, struct mitem *items, int n, int sel, void (*done)(int id, int index));
void confirm_open(const char *msg, const char *yes, void (*done)(int yes));
void help_open(int ctx);
void redraw(void);
extern unsigned ui_mods;

/* ---- editor.c ---- */
struct ed;
void ed_open(const char *file, int mode, size_t cur, size_t anc, int top_para, int top_row, int focus);
void ed_open_path(const char *path, int readonly);   /* a file outside the notes folder; the guide */
void ed_new(const char *title);
void ed_close(void);              /* save, rename to its title, drop it if empty */
int ed_save(void);
void ed_key(struct nkey k);
void ed_draw(void);
void ed_menu(void);
int ed_is_open(void);
void ed_remember(void);           /* put cursor/mode into st for the next start */

/* ---- list.c ---- */
void list_show(void);
void list_key(struct nkey k);
void list_draw(void);
void list_select_file(const char *file);

/* ---- main.c ---- */
enum { SCR_LIST, SCR_NOTE };
extern int screen;
extern int plain_mode;            /* emuzero-notes FILE: one file, quit when it is closed */
void app_quit(void);
uint32_t now_ms(void);
const char *guide_path(void);
const uint8_t *cmd_menu(int ctx);
void app_cmd(int cmd);
