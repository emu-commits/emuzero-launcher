// SPDX-License-Identifier: MIT
// What the Wiki remembers: recently read articles, bookmarks, the text size. One small text file,
//   ~/.local/state/emuzero/wiki/state   ($XDG_STATE_HOME/emuzero/wiki/state)
// written whole through a temporary file and a rename (Notes' store_write_path), so the power
// switch never leaves half a file. Entries are "pack name, title": ids change when a pack is
// rebuilt, titles do not.
#include "wiki.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct wstate ws = { .size = 1 };

static void state_path(char *p, size_t n, int mk)
{
	const char *x = getenv("XDG_STATE_HOME"), *h = getenv("HOME");
	char d[512];
	if (x && *x) snprintf(d, sizeof(d), "%s/emuzero/wiki", x);
	else snprintf(d, sizeof(d), "%s/.local/state/emuzero/wiki", h && *h ? h : "/tmp");
	if (mk) {
		char t[512];
		scopy(t, sizeof(t), d);
		for (char *s = t + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(t, 0700); *s = '/'; }
		mkdir(t, 0700);
	}
	snprintf(p, n, "%s/state", d);
}

static void put(struct entry *e, const char *pack, const char *title)
{
	scopy(e->pack, sizeof(e->pack), pack);
	scopy(e->title, sizeof(e->title), title);
}

void wstate_load(void)
{
	char p[600], *s, err[128];
	size_t n;
	state_path(p, sizeof(p), 0);
	if (store_read_path(p, &s, &n, err, sizeof(err))) return;
	for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
		char *a = strchr(line, '\t'), *b = a ? strchr(a + 1, '\t') : NULL;
		if (!strncmp(line, "size ", 5)) ws.size = atoi(line + 5);
		else if (a && b && !strncmp(line, "recent\t", 7) && ws.nrecent < 40) { *b = 0; put(&ws.recent[ws.nrecent++], a + 1, b + 1); }
		else if (a && b && !strncmp(line, "mark\t", 5) && ws.nmarks < 100) { *b = 0; put(&ws.marks[ws.nmarks++], a + 1, b + 1); }
	}
	free(s);
}

void wstate_save(void)
{
	char p[600], err[128];
	state_path(p, sizeof(p), 1);
	size_t cap = 64 + (size_t)(ws.nrecent + ws.nmarks) * 400, o = 0;
	char *b = malloc(cap);
	o += (size_t)snprintf(b + o, cap - o, "size %d\n", ws.size);
	for (int i = 0; i < ws.nrecent; i++) o += (size_t)snprintf(b + o, cap - o, "recent\t%s\t%s\n", ws.recent[i].pack, ws.recent[i].title);
	for (int i = 0; i < ws.nmarks; i++) o += (size_t)snprintf(b + o, cap - o, "mark\t%s\t%s\n", ws.marks[i].pack, ws.marks[i].title);
	if (store_write_path(p, b, o, err, sizeof(err))) fprintf(stderr, "emuzero-wiki: can't save %s: %s\n", p, err);
	free(b);
}

static int find(struct entry *l, int n, const char *pack, const char *title)
{
	for (int i = 0; i < n; i++) if (!strcmp(l[i].pack, pack) && !strcmp(l[i].title, title)) return i;
	return -1;
}

void recent_add(const char *pack, const char *title)
{
	int i = find(ws.recent, ws.nrecent, pack, title);
	if (i == 0) return;
	if (i < 0) i = ws.nrecent < 40 ? ws.nrecent++ : 39;
	memmove(ws.recent + 1, ws.recent, sizeof(ws.recent[0]) * (size_t)i);
	put(&ws.recent[0], pack, title);
	wstate_save();
}

int bookmarked(const char *pack, const char *title) { return find(ws.marks, ws.nmarks, pack, title) >= 0; }

void bookmark_set(const char *pack, const char *title, int on)
{
	int i = find(ws.marks, ws.nmarks, pack, title);
	if (on && i < 0 && ws.nmarks < 100) {
		memmove(ws.marks + 1, ws.marks, sizeof(ws.marks[0]) * (size_t)ws.nmarks);
		put(&ws.marks[0], pack, title);
		ws.nmarks++;
	} else if (!on && i >= 0) {
		memmove(ws.marks + i, ws.marks + i + 1, sizeof(ws.marks[0]) * (size_t)(ws.nmarks - i - 1));
		ws.nmarks--;
	} else return;
	wstate_save();
}
