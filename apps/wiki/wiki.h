// SPDX-License-Identifier: MIT
// emuZero Wiki: everything its modules share. It is built on Notes' modules (screen and keys,
// fonts, drawing, the document and its View-mode renderer), so it includes notes.h for them.
// Design: WIKI-APP-PLAN.md in the cardputerzero-os folder; README.md here for building and testing.
#pragma once
#include "notes.h"
#include <sqlite3.h>

#define MAX_PACKS 8
#define MAX_RESULTS 60

/* ---- pack.c: the .wpack files ---- */
struct shelf { int id; char name[64]; char kind[16]; char about[96]; };
struct category { int id; char slug[32]; char name[48]; uint32_t colour; char about[96]; };
struct mem_article {               /* a small pack's articles, kept in memory for searching */
	int id, shelf, category, ord;
	char *title, *tags, *summary, *slug;
	char *ftitle, *fsearch;        /* folded: the title; title + tags + summary */
	char *fbody; size_t fblen;     /* folded text, loaded for the first "search inside" */
};
struct pack {
	sqlite3 *db;
	char path[512];
	char name[64], footer[160], licence[200], source[200], notice[300];
	int memory;                    /* searched in memory (small) or by its word index */
	int bodies;                    /* has the word index of article text */
	int order;                     /* survival first, Wikipedia after */
	int narticles;
	struct shelf shelves[4]; int nshelves;
	struct category cats[24]; int ncats;
	struct mem_article *arts; int narts;
};
extern struct pack packs[MAX_PACKS];
extern int npacks;
extern char pack_errors[512];

struct result {
	int pack, id;
	char title[160];
	char tag[64];                   /* right-hand tag: category or "FM 21-76" */
	uint32_t tag_colour;
	int section;                    /* which header it goes under: 0 survival, 1 Wikipedia */
	char snip[200]; int hl[6][2]; int nhl;   /* "search inside": the line that matched */
	int has_snip;
};

void packs_open(void);              /* every *.wpack in the pack folders */
void packs_close(void);
int pack_by_name(const char *name);
int pack_article(int p, int id, char **md, size_t *n, char *title, size_t tn);  /* malloc'd Markdown */
int pack_find_title(int p, const char *title);       /* id, or 0 */
int pack_find_slug(int p, const char *slug);
int pack_meta_int(int p, const char *key);
int pack_title_of(int p, int id, char *title, size_t tn, int *shelf, int *category);
const struct category *pack_category(int p, int id);
int pack_image(int p, const char *name, uint8_t **data, size_t *n);   /* malloc'd JPEG */
int pack_random(int p);
int search_titles(const char *q, struct result *out, int max);
int search_inside(const char *q, struct result *out, int max, int *too_common);
void result_snippet(struct result *r, const char *q);  /* the line of the article that matched */
size_t fold_key(const char *s, char *out, size_t max);
void scopy(char *d, size_t n, const char *s);           /* cut at a character boundary */
static inline const char *col(sqlite3_stmt *st, int c) { const char *s = (const char *)sqlite3_column_text(st, c); return s ? s : ""; }  /* build-pack.py's fold() */
int query_words(const char *q, char w[][64], int max);
int is_stop_word(const char *w);
uint32_t real_ms(void);
void timing_log(const char *what, uint32_t t0);       /* WIKI_TIMING=1: to stderr */

/* ---- triage.c: "What is happening?" ---- */
struct tri_node { char id[24]; char q[160]; char label[5][64]; char to[5][24]; int n; };
int triage_load(void);              /* from the first pack that has it; the count */
const struct tri_node *triage_node(const char *id);
int triage_pack(void);

/* ---- image.c ---- */
extern const struct view_pic_hook wiki_pics;
int pic_size(int p, const char *name, int *w, int *h);
void pic_draw_fit(int p, const char *name, int x, int y, int w, int h, int full);
void pics_clear(void);

/* ---- state.c: recently read, bookmarks, settings ---- */
struct entry { char pack[64]; char title[320]; };
struct wstate {
	int size;
	struct entry recent[40]; int nrecent;
	struct entry marks[100]; int nmarks;
};
extern struct wstate ws;
void wstate_load(void);
void wstate_save(void);
void recent_add(const char *pack, const char *title);
int bookmarked(const char *pack, const char *title);
void bookmark_set(const char *pack, const char *title, int on);

/* ---- wkeys.c: the key table ---- */
enum { X_HOME = 1, X_LIST = 2, X_READ = 4, X_TRIAGE = 8, X_PIC = 16, X_ALL = 31 };
enum wcmd {
	W_NONE,
	W_UP, W_DOWN, W_LEFT, W_RIGHT, W_PGUP, W_PGDN, W_TOP, W_BOTTOM,
	W_OPEN, W_BACK, W_INSIDE, W_CLEAR, W_CONTENTS, W_FIND, W_BOOKMARK, W_COPY, W_SIZE,
	W_ZOOM, W_RANDOM, W_TRIAGE, W_GUIDES, W_MANUAL, W_RECENT, W_MARKS, W_SEARCH, W_ABOUT,
	W_DELETE, W_MENU, W_HELP, W_QUIT,
	W_COUNT
};
struct wbind { uint8_t ctx; uint32_t key; uint8_t mods; uint8_t cmd; const char *label; };
extern const struct wbind wbinds[];
const char *wcmd_name(int cmd);
int wcmd_lookup(int ctx, struct nkey k);
const char *wcmd_keys(int ctx, int cmd);
const uint8_t *wcmd_menu(int ctx);

/* ---- wui.c: status bar, overlays. It gives notes.h's bar_draw, toast, menu_open, confirm_open,
 * help_open (with Wiki's contexts), overlay_key/draw and ui_mods, made from Notes' ui.c ---- */
void text_marked(int f, int x, int y, int maxw, uint32_t c, uint32_t mark, const char *s, const int (*hl)[2], int nhl);

/* ---- screens: home.c (search, lists, triage), reader.c, picture in image.c ---- */
enum { S_HOME, S_LIST, S_TRIAGE, S_READ, S_PIC };
enum { L_CATEGORIES, L_CATEGORY, L_CHAPTERS, L_RECENT, L_MARKS, L_INSIDE, L_GET };
struct scr {
	int kind;
	int list, param;                   /* S_LIST: which list; its category */
	char node[24];                     /* S_TRIAGE */
	int pack, id;                      /* S_READ, S_PIC */
	char name[64];                     /* S_PIC: the picture */
	char q[200];                       /* L_INSIDE */
	int sel, top;
	int focus, top_para, top_row;      /* S_READ, kept when another screen goes on top */
};
extern struct scr stack[32];
extern int nstack;
struct scr *cur_scr(void);
void push_scr(const struct scr *s);
void pop_scr(void);
void go_home(void);
uint32_t wnow(void);
void wiki_cmd(int cmd);
int cur_ctx(void);
void trace(const char *fmt, ...);   /* WIKI_TRACE=FILE: what each screen shows, for the checks */

void home_enter(void);               /* (re)build the screen on top of the stack */
void home_key(struct nkey k);
int home_cmd(int cmd);
void home_draw(void);
void home_menu(void);
void open_article(int pack, int id);
void open_list(int list, int param);
void open_triage(const char *node);
void open_link(int pack, const char *url);

void read_enter(void);
void read_leave(void);              /* keep the place in stack before another screen goes on top */
void read_key(struct nkey k);
int read_cmd(int cmd);
void read_draw(void);
void read_menu(void);
const char *read_title(void);

void pic_enter(void);
void pic_key(struct nkey k);
void pic_draw(void);
