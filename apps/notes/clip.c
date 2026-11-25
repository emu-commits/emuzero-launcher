// SPDX-License-Identifier: MIT
// The emuZero clipboard, shared with other programs through one file:
//   /run/user/UID/emuzero-clipboard   (or /tmp/emuzero-clipboard-UID without /run/user/UID)
// nz-clip reads and writes the same file from a shell ("echo hi | nz-clip", "nz-clip -o").
// The last 20 copies are kept in ~/.local/state/emuzero/clipboard/history (entries end with
// an ASCII record separator, 0x1e), so they survive a restart.
#define _GNU_SOURCE
#include "notes.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HIST 20

static char *hist[HIST];
static size_t hlen[HIST];
static int nh, loaded;

static void clip_file(char *p, size_t n)
{
	char d[64];
	struct stat s;
	snprintf(d, sizeof(d), "/run/user/%u", (unsigned)getuid());
	if (!stat(d, &s) && S_ISDIR(s.st_mode) && s.st_uid == getuid()) snprintf(p, n, "%s/emuzero-clipboard", d);
	else snprintf(p, n, "/tmp/emuzero-clipboard-%u", (unsigned)getuid());
}

static void hist_file(char *p, size_t n, int mk)
{
	const char *x = getenv("XDG_STATE_HOME"), *h = getenv("HOME");
	char d[512];
	if (x && *x) snprintf(d, sizeof(d), "%s/emuzero/clipboard", x);
	else snprintf(d, sizeof(d), "%s/.local/state/emuzero/clipboard", h && *h ? h : "/tmp");
	if (mk) {
		char t[512];
		snprintf(t, sizeof(t), "%s", d);
		for (char *s = t + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(t, 0700); *s = '/'; }
		mkdir(t, 0700);
	}
	snprintf(p, n, "%s/history", d);
}

static void push(const char *s, size_t n)
{
	if (nh && hlen[0] == n && !memcmp(hist[0], s, n)) return;
	for (int i = 1; i < nh; i++)   /* copied again: move it to the top */
		if (hlen[i] == n && !memcmp(hist[i], s, n)) {
			char *t = hist[i];
			memmove(hist + 1, hist, sizeof(hist[0]) * (size_t)i);
			memmove(hlen + 1, hlen, sizeof(hlen[0]) * (size_t)i);
			hist[0] = t; hlen[0] = n;
			return;
		}
	if (nh == HIST) free(hist[--nh]);
	memmove(hist + 1, hist, sizeof(hist[0]) * (size_t)nh);
	memmove(hlen + 1, hlen, sizeof(hlen[0]) * (size_t)nh);
	hist[0] = malloc(n + 1);
	memcpy(hist[0], s, n);
	hist[0][n] = 0;
	hlen[0] = n;
	nh++;
}

static void load(void)
{
	if (loaded) return;
	loaded = 1;
	char p[600], *s; size_t n;
	char err[64];
	hist_file(p, sizeof(p), 0);
	if (store_read_path(p, &s, &n, err, sizeof(err))) return;
	/* oldest last in the file order? no: newest first; push in reverse */
	char *ent[HIST]; size_t el[HIST]; int ne = 0;
	for (size_t i = 0; i < n && ne < HIST;) {
		char *e = memchr(s + i, 0x1e, n - i);
		size_t l = e ? (size_t)(e - (s + i)) : n - i;
		ent[ne] = s + i; el[ne++] = l;
		i += l + 1;
	}
	for (int k = ne - 1; k >= 0; k--) push(ent[k], el[k]);
	free(s);
}

static void save_hist(void)
{
	char p[600], err[64];
	size_t tot = 0;
	for (int i = 0; i < nh; i++) tot += hlen[i] + 1;
	char *b = malloc(tot + 1), *o = b;
	for (int i = 0; i < nh; i++) { memcpy(o, hist[i], hlen[i]); o += hlen[i]; *o++ = 0x1e; }
	hist_file(p, sizeof(p), 1);
	store_write_path(p, b, tot, err, sizeof(err));
	free(b);
}

void clip_set(const char *s, size_t n)
{
	load();
	char p[128], err[64];
	clip_file(p, sizeof(p));
	store_write_path(p, s, n, err, sizeof(err));
	chmod(p, 0600);
	push(s, n);
	save_hist();
}

/* the current clip: the shared file first (another program may have set it) */
char *clip_get(size_t *n)
{
	load();
	char p[128], err[64], *s;
	clip_file(p, sizeof(p));
	if (!store_read_path(p, &s, n, err, sizeof(err))) {
		if (*n && (!nh || hlen[0] != *n || memcmp(hist[0], s, *n))) { push(s, *n); save_hist(); }
		if (*n) return s;
		free(s);
	}
	if (!nh) return NULL;
	*n = hlen[0];
	s = malloc(*n + 1);
	memcpy(s, hist[0], *n + 1);
	return s;
}

int clip_history(char **items, int max)
{
	size_t n;
	free(clip_get(&n));   /* picks up a clip another program made */
	int k = nh < max ? nh : max;
	for (int i = 0; i < k; i++) items[i] = hist[i];
	return k;
}
