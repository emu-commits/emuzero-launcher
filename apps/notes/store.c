// SPDX-License-Identifier: MIT
// The notes folder (~/Notes, or $NOTES_DIR): one Markdown file per note, named after its title.
//
//   ~/Notes/Groceries.md         the notes (the files are the truth)
//   ~/Notes/.Trash/              Recently Deleted, kept 30 days
//   ~/Notes/.history/            each note as it was when last opened ("Revert")
//   ~/Notes/.notes-state         pins, the note and cursor to reopen, settings; key=value lines
//
// Saving writes .NAME.tmp, fsyncs it, renames it over the note and fsyncs the folder: a crash
// or the power switch at any moment leaves the old file or the new one, never half of each.
#define _GNU_SOURCE
#include "notes.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_FILE (4u << 20)   /* bigger files are refused, never half-loaded */
#define TRASH_DAYS 30

struct state st;
struct note *notes;
int nnotes;
static int cnotes;
char notes_dir[512];

void path_note(char *out, size_t n, const char *file) { snprintf(out, n, "%s/%s", notes_dir, file); }

static int mkdir_p(const char *dir)
{
	char p[512];
	snprintf(p, sizeof(p), "%s", dir);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
	return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

static int has_ext(const char *f)
{
	size_t l = strlen(f);
	return (l > 3 && !strcmp(f + l - 3, ".md")) || (l > 4 && !strcmp(f + l - 4, ".txt"));
}

int store_init(const char *dir)
{
	if (dir) snprintf(notes_dir, sizeof(notes_dir), "%s", dir);
	else {
		const char *e = getenv("NOTES_DIR"), *h = getenv("HOME");
		if (e && *e) snprintf(notes_dir, sizeof(notes_dir), "%s", e);
		else snprintf(notes_dir, sizeof(notes_dir), "%s/Notes", h && *h ? h : ".");
	}
	char p[800];
	if (mkdir_p(notes_dir)) return -1;
	snprintf(p, sizeof(p), "%s/.Trash", notes_dir); mkdir(p, 0755);
	snprintf(p, sizeof(p), "%s/.history", notes_dir); mkdir(p, 0755);
	/* leftovers of a save the power switch cut short */
	DIR *d = opendir(notes_dir);
	for (struct dirent *e; d && (e = readdir(d));) {
		size_t l = strlen(e->d_name);
		if (e->d_name[0] == '.' && l > 4 && !strcmp(e->d_name + l - 4, ".tmp")) {
			snprintf(p, sizeof(p), "%s/%s", notes_dir, e->d_name);
			unlink(p);
		}
	}
	if (d) closedir(d);
	return 0;
}

/* the title (first line with text) and the next line, both without Markdown marks */
void store_note_title(const char *s, size_t n, char *title, size_t tn, char *snip, size_t sn)
{
	title[0] = snip[0] = 0;
	size_t i = 0;
	int got = 0, code = 0;
	while (i < n && got < 2) {
		const char *e = memchr(s + i, '\n', n - i);
		size_t l = e ? (size_t)(e - (s + i)) : n - i;
		size_t ll = l && s[i + l - 1] == '\r' ? l - 1 : l;
		struct linfo li;
		line_info(s + i, ll, code, &li);
		if (li.kind == LK_FENCE) code = !code;
		else if (li.kind != LK_BLANK && li.kind != LK_RULE) {
			char *out = got ? snip : title;
			size_t on = got ? sn : tn;
			if (li.kind == LK_CODE) { size_t k = ll < on - 1 ? ll : on - 1; k = u8_whole(s + i, k); memcpy(out, s + i, k); out[k] = 0; }
			else md_plain_line(s + i, ll, out, on);
			if (li.kind == LK_TASK && got) {   /* show a checklist line as one */
				char tmp[160];
				snprintf(tmp, sizeof(tmp), "%s %s", li.checked ? "☑" : "☐", out);
				snprintf(out, on, "%s", tmp);
			}
			if (out[0]) got++;
		}
		i += l + 1;
	}
}

static void note_free(struct note *n) { free(n->body); free(n->fold); }

int store_find(const char *file)
{
	for (int i = 0; i < nnotes; i++) if (!strcmp(notes[i].file, file)) return i;
	return -1;
}

int store_stat(const char *path, time_t *mtime, off_t *size)
{
	struct stat s;
	if (stat(path, &s)) return -1;
	if (mtime) *mtime = s.st_mtime;
	if (size) *size = s.st_size;
	return 0;
}

/* the list: every .md / .txt in the folder, title and first line read from its start */
void store_scan(void)
{
	DIR *d = opendir(notes_dir);
	struct note *old = notes;
	int nold = nnotes;
	notes = NULL; nnotes = cnotes = 0;
	for (struct dirent *e; d && (e = readdir(d));) {
		if (e->d_name[0] == '.' || !has_ext(e->d_name) || strlen(e->d_name) >= 256) continue;
		char p[800];
		struct stat s;
		snprintf(p, sizeof(p), "%s/%s", notes_dir, e->d_name);
		if (stat(p, &s) || !S_ISREG(s.st_mode)) continue;
		if (nnotes == cnotes) { cnotes = cnotes ? cnotes * 2 : 64; notes = realloc(notes, (size_t)cnotes * sizeof(*notes)); }
		struct note *n = &notes[nnotes++];
		memset(n, 0, sizeof(*n));
		snprintf(n->file, sizeof(n->file), "%s", e->d_name);
		n->mtime = s.st_mtime;
		n->size = s.st_size;
		/* keep the search cache of a note that has not changed */
		for (int k = 0; k < nold; k++)
			if (old[k].file[0] && !strcmp(old[k].file, n->file)) {
				if (old[k].body && old[k].body_mtime == s.st_mtime && old[k].body_size == s.st_size) {
					n->body = old[k].body; n->blen = old[k].blen; n->fold = old[k].fold;
					n->body_mtime = old[k].body_mtime; n->body_size = old[k].body_size;
					old[k].body = old[k].fold = NULL;
				}
				if (old[k].mtime == s.st_mtime && old[k].size == s.st_size) {
					memcpy(n->title, old[k].title, sizeof(n->title));
					memcpy(n->snippet, old[k].snippet, sizeof(n->snippet));
				}
				break;
			}
		if (!n->title[0] && !n->snippet[0]) {
			char head[1024];
			int fd = open(p, O_RDONLY | O_CLOEXEC);
			ssize_t r = fd >= 0 ? read(fd, head, sizeof(head)) : -1;
			if (fd >= 0) close(fd);
			if (r < 0) r = 0;
			size_t hn = (size_t)r;
			if (hn >= 3 && !memcmp(head, "\xef\xbb\xbf", 3)) { memmove(head, head + 3, hn - 3); hn -= 3; }
			store_note_title(head, hn, n->title, sizeof(n->title), n->snippet, sizeof(n->snippet));
		}
	}
	if (d) closedir(d);
	for (int k = 0; k < nold; k++) note_free(&old[k]);
	free(old);
}

int store_read_path(const char *path, char **out, size_t *n, char *err, size_t errn)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) { snprintf(err, errn, "%s", strerror(errno)); return -1; }
	struct stat s;
	if (fstat(fd, &s) || !S_ISREG(s.st_mode)) { close(fd); snprintf(err, errn, "not a file"); return -1; }
	if ((size_t)s.st_size > MAX_FILE) { close(fd); snprintf(err, errn, "too big (%lld KB; the limit is %u KB)", (long long)s.st_size / 1024, MAX_FILE / 1024); return -1; }
	char *b = malloc((size_t)s.st_size + 1);
	size_t got = 0;
	while (b && got < (size_t)s.st_size) {
		ssize_t r = read(fd, b + got, (size_t)s.st_size - got);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		got += (size_t)r;
	}
	close(fd);
	if (!b) { snprintf(err, errn, "out of memory"); return -1; }
	if (memchr(b, 0, got)) { free(b); snprintf(err, errn, "not a text file"); return -1; }
	b[got] = 0;
	*out = b;
	*n = got;
	return 0;
}

int store_read(const char *file, char **out, size_t *n)
{
	char p[800], err[128];
	path_note(p, sizeof(p), file);
	return store_read_path(p, out, n, err, sizeof(err));
}

static int fsync_dir(const char *path)
{
	char d[800];
	snprintf(d, sizeof(d), "%s", path);
	char *sl = strrchr(d, '/');
	if (sl) *sl = 0; else snprintf(d, sizeof(d), ".");
	int fd = open(d[0] ? d : "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return -1;
	int r = fsync(fd);
	close(fd);
	return r;
}

int store_write_path(const char *path, const char *s, size_t n, char *err, size_t errn)
{
	char tmp[900];
	const char *sl = strrchr(path, '/');
	if (sl) snprintf(tmp, sizeof(tmp), "%.*s/.%s.tmp", (int)(sl - path), path, sl + 1);
	else snprintf(tmp, sizeof(tmp), ".%s.tmp", path);
	struct stat old;
	mode_t mode = 0644;
	if (!stat(path, &old)) mode = old.st_mode & 07777;
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, mode);
	if (fd < 0) { snprintf(err, errn, "%s", strerror(errno)); return -1; }
	size_t done = 0;
	while (done < n) {
		ssize_t w = write(fd, s + done, n - done);
		if (w < 0 && errno == EINTR) continue;
		if (w < 0) { snprintf(err, errn, "%s", strerror(errno)); close(fd); unlink(tmp); return -1; }
		done += (size_t)w;
	}
	if (fsync(fd)) { snprintf(err, errn, "%s", strerror(errno)); close(fd); unlink(tmp); return -1; }
	close(fd);
	if (rename(tmp, path)) { snprintf(err, errn, "%s", strerror(errno)); unlink(tmp); return -1; }
	fsync_dir(path);
	return 0;
}

int store_write(const char *file, const char *s, size_t n, char *err, size_t errn)
{
	char p[800];
	path_note(p, sizeof(p), file);
	return store_write_path(p, s, n, err, errn);
}

/* a file name from the note's first line: "Groceries.md"; current keeps its own name */
void store_name_for(const char *text, size_t n, const char *current, char *out, size_t outn)
{
	char title[128], snip[8], base[96];
	store_note_title(text, n, title, sizeof(title), snip, sizeof(snip));
	size_t o = 0;
	for (size_t i = 0; title[i] && o < 60; i++) {
		unsigned char c = (unsigned char)title[i];
		if (c < 32 || strchr("/\\:*?\"<>|", c)) continue;
		if (o == 0 && (c == '.' || c == ' ')) continue;
		base[o++] = (char)c;
	}
	o = u8_whole(base, o);
	if (title[o] && title[o] != ' ') {   /* cut short: at a word's end when there is one */
		size_t k = o;
		while (k > 30 && base[k - 1] != ' ') k--;
		if (k > 30) o = k;
	}
	while (o && (base[o - 1] == ' ' || base[o - 1] == '.')) o--;
	base[o] = 0;
	if (!o) {
		time_t t = time(NULL);
		strftime(base, sizeof(base), "Note %Y-%m-%d %H%M", localtime(&t));
	}
	for (int k = 1; k < 1000; k++) {
		if (k == 1) snprintf(out, outn, "%s.md", base);
		else snprintf(out, outn, "%s %d.md", base, k);
		if (current && !strcmp(out, current)) return;
		char p[800];
		path_note(p, sizeof(p), out);
		if (access(p, F_OK)) return;
	}
}

/* rename without replacing a file that is already there */
int store_rename(const char *from, const char *to)
{
	char a[800], b[800];
	path_note(a, sizeof(a), from);
	path_note(b, sizeof(b), to);
	if (!link(a, b)) { unlink(a); fsync_dir(a); return 0; }
	if (errno == EEXIST) return -1;
	if (!access(b, F_OK)) return -1;    /* a file system without hard links (FAT) */
	if (rename(a, b)) return -1;
	fsync_dir(a);
	return 0;
}

/* ---- Recently Deleted ---- */

static void trash_add(const char *file, time_t when)
{
	if (st.ntrash == (int)(sizeof(st.trash) / sizeof(st.trash[0]))) {
		memmove(st.trash, st.trash + 1, sizeof(st.trash[0]) * (size_t)(st.ntrash - 1));
		st.ntrash--;
	}
	snprintf(st.trash[st.ntrash].file, sizeof(st.trash[0].file), "%s", file);
	st.trash[st.ntrash++].when = when;
}

int store_trash(const char *file)
{
	char a[800], b[800], name[256];
	path_note(a, sizeof(a), file);
	snprintf(name, sizeof(name), "%s", file);
	for (int k = 2; k < 1000; k++) {
		snprintf(b, sizeof(b), "%s/.Trash/%s", notes_dir, name);
		if (access(b, F_OK)) break;
		char stem[240];
		snprintf(stem, sizeof(stem), "%.*s", (int)(strlen(file) - 3), file);
		snprintf(name, sizeof(name), "%s %d.md", stem, k);
	}
	if (rename(a, b)) return -1;
	fsync_dir(a);
	trash_add(name, time(NULL));
	set_pinned(file, 0);
	state_save();
	return 0;
}

int store_restore(int ti, char *out_file, size_t n)
{
	if (ti < 0 || ti >= st.ntrash) return -1;
	char a[800], e[128];
	snprintf(a, sizeof(a), "%s/.Trash/%s", notes_dir, st.trash[ti].file);
	char *s;
	size_t len;
	if (store_read_path(a, &s, &len, e, sizeof(e))) return -1;
	char name[256];
	store_name_for(s, len, NULL, name, sizeof(name));
	free(s);
	char b[800];
	path_note(b, sizeof(b), name);
	if (rename(a, b)) return -1;
	fsync_dir(b);
	snprintf(out_file, n, "%s", name);
	memmove(st.trash + ti, st.trash + ti + 1, sizeof(st.trash[0]) * (size_t)(st.ntrash - ti - 1));
	st.ntrash--;
	state_save();
	return 0;
}

int store_purge(int ti)
{
	if (ti < 0 || ti >= st.ntrash) return -1;
	char a[800];
	snprintf(a, sizeof(a), "%s/.Trash/%s", notes_dir, st.trash[ti].file);
	unlink(a);
	memmove(st.trash + ti, st.trash + ti + 1, sizeof(st.trash[0]) * (size_t)(st.ntrash - ti - 1));
	st.ntrash--;
	state_save();
	return 0;
}

void store_purge_old(void)
{
	time_t now = time(NULL);
	int changed = 0;
	for (int i = st.ntrash - 1; i >= 0; i--) {
		char a[800];
		snprintf(a, sizeof(a), "%s/.Trash/%s", notes_dir, st.trash[i].file);
		if (access(a, F_OK) || now - st.trash[i].when > TRASH_DAYS * 86400) {
			unlink(a);
			memmove(st.trash + i, st.trash + i + 1, sizeof(st.trash[0]) * (size_t)(st.ntrash - i - 1));
			st.ntrash--;
			changed = 1;
		}
	}
	/* files put in .Trash by hand (or by an older state file) start their 30 days now */
	DIR *d;
	char p[800];
	snprintf(p, sizeof(p), "%s/.Trash", notes_dir);
	if ((d = opendir(p))) {
		for (struct dirent *e; (e = readdir(d));) {
			if (e->d_name[0] == '.' || !has_ext(e->d_name) || strlen(e->d_name) >= 256) continue;
			int known = 0;
			for (int i = 0; i < st.ntrash && !known; i++) known = !strcmp(st.trash[i].file, e->d_name);
			if (!known) { trash_add(e->d_name, now); changed = 1; }
		}
		closedir(d);
	}
	if (changed) state_save();
}

/* ---- history: the note as it was when opened ---- */

int store_history(const char *file)
{
	char *s; size_t n; char err[128], p[800];
	if (store_read(file, &s, &n)) return -1;
	snprintf(p, sizeof(p), "%s/.history/%s", notes_dir, file);
	int r = store_write_path(p, s, n, err, sizeof(err));
	free(s);
	return r;
}

int store_history_read(const char *file, char **out, size_t *n)
{
	char p[800], err[128];
	snprintf(p, sizeof(p), "%s/.history/%s", notes_dir, file);
	return store_read_path(p, out, n, err, sizeof(err));
}

/* ---- pins and the state file ---- */

int is_pinned(const char *file)
{
	for (int i = 0; i < st.npins; i++) if (!strcmp(st.pins[i], file)) return 1;
	return 0;
}

void set_pinned(const char *file, int on)
{
	for (int i = 0; i < st.npins; i++)
		if (!strcmp(st.pins[i], file)) {
			if (on) return;
			memmove(st.pins + i, st.pins + i + 1, sizeof(st.pins[0]) * (size_t)(st.npins - i - 1));
			st.npins--;
			return;
		}
	if (on && st.npins < (int)(sizeof(st.pins) / sizeof(st.pins[0])))
		snprintf(st.pins[st.npins++], sizeof(st.pins[0]), "%s", file);
}

void state_load(void)
{
	char p[800], line[600];
	memset(&st, 0, sizeof(st));
	st.size = 1;
	st.focus = -1;
	snprintf(p, sizeof(p), "%s/.notes-state", notes_dir);
	FILE *f = fopen(p, "r");
	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		char *v = strchr(line, '=');
		if (!v) continue;
		*v++ = 0;
		if (!strcmp(line, "last")) snprintf(st.last, sizeof(st.last), "%s", v);
		else if (!strcmp(line, "cursor")) st.cur = strtoul(v, NULL, 10);
		else if (!strcmp(line, "anchor")) st.anc = strtoul(v, NULL, 10);
		else if (!strcmp(line, "mode")) st.mode = !strcmp(v, "edit") ? MODE_EDIT : MODE_VIEW;
		else if (!strcmp(line, "top")) sscanf(v, "%d %d", &st.top_para, &st.top_row);
		else if (!strcmp(line, "focus")) st.focus = atoi(v);
		else if (!strcmp(line, "size")) st.size = atoi(v);
		else if (!strcmp(line, "ticked_bottom")) st.ticked_bottom = atoi(v);
		else if (!strcmp(line, "pin")) set_pinned(v, 1);
		else if (!strcmp(line, "trash")) {
			char *sp = strchr(v, ' ');
			if (sp && st.ntrash < (int)(sizeof(st.trash) / sizeof(st.trash[0]))) {
				*sp = 0;
				st.trash[st.ntrash].when = (time_t)strtoll(v, NULL, 10);
				snprintf(st.trash[st.ntrash++].file, sizeof(st.trash[0].file), "%s", sp + 1);
			}
		}
	}
	if (f) fclose(f);
	if (st.anc == 0 && st.cur) st.anc = st.cur;
}

void state_save(void)
{
	char p[800], buf[64 * 1024];
	size_t o = 0;
#define OUT(...) do { int k = snprintf(buf + o, sizeof(buf) - o, __VA_ARGS__); if (k > 0 && (size_t)k < sizeof(buf) - o) o += (size_t)k; } while (0)
	OUT("# emuZero Notes: pins, the note to reopen, settings. Deleting this file loses nothing else.\n");
	OUT("last=%s\ncursor=%zu\nanchor=%zu\nmode=%s\ntop=%d %d\nfocus=%d\nsize=%d\nticked_bottom=%d\n",
	    st.last, st.cur, st.anc, st.mode == MODE_EDIT ? "edit" : "view", st.top_para, st.top_row, st.focus, st.size, st.ticked_bottom);
	for (int i = 0; i < st.npins; i++) OUT("pin=%s\n", st.pins[i]);
	for (int i = 0; i < st.ntrash; i++) OUT("trash=%lld %s\n", (long long)st.trash[i].when, st.trash[i].file);
#undef OUT
	char err[128];
	snprintf(p, sizeof(p), "%s/.notes-state", notes_dir);
	store_write_path(p, buf, o, err, sizeof(err));
}
