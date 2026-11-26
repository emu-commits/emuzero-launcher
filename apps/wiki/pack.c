// SPDX-License-Identifier: MIT
// The packs: every *.wpack in the pack folders, opened read-only. An article's body is Markdown,
// zlib-compressed one article at a time. A small pack (the survival guides) is searched in
// memory; a large one (Wikipedia) by its own word index in plain tables (the image's libsqlite3
// has no FTS): for each word, the ids of the articles that hold it, ascending varint deltas.
// Ids follow the score (1 = best), so the first ids that match are the best matches. One to
// three letters typed are answered from the pack's top lists. See tools/build-pack.py.
#define _GNU_SOURCE
#include "wiki.h"
#include "paths.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

struct pack packs[MAX_PACKS];
int npacks;
char pack_errors[512];

uint32_t real_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

void timing_log(const char *what, uint32_t t0)
{
	static int on = -1;
	if (on < 0) on = getenv("WIKI_TIMING") != NULL;
	if (on) fprintf(stderr, "emuzero-wiki: %s %u ms\n", what, (unsigned)(real_ms() - t0));
}

/* copy s into d (n bytes), cut short at a character boundary */
void scopy(char *d, size_t n, const char *s)
{
	size_t l = u8_cut(s, strlen(s), n - 1);
	memcpy(d, s, l);
	d[l] = 0;
}

/* ---- folding: build-pack.py's fold(), compact (no filler bytes) ---- */
size_t fold_key(const char *s, char *out, size_t max)
{
	static const char lat[33] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
	size_t o = 0, n = strlen(s);
	int space = 1;
	for (size_t i = 0; i < n && o + 4 < max;) {
		size_t j = i;
		uint32_t c = u8_dec(s, n, &j);
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (!space) out[o++] = ' ';
			space = 1;
			i = j;
			continue;
		}
		space = 0;
		if (c >= 'A' && c <= 'Z') out[o++] = (char)(c + 32);
		else if (c >= 0xc0 && c <= 0xff && lat[c & 0x1f] != '/') out[o++] = lat[c & 0x1f];
		else if (c >= 0xc0 && c <= 0xdf) o += (size_t)u8_enc(c + 32, out + o);   /* × and ß-like stay */
		else { memcpy(out + o, s + i, j - i); o += j - i; }
		i = j;
	}
	while (o && out[o - 1] == ' ') o--;
	out[o] = 0;
	return o;
}

static int word_byte(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c >= 0x80; }

/* the words of q as build-pack.py's words() splits them; returns the count. The last word keeps
 * a trailing space mark: w[i][63] = 1 when the word was followed by a space */
int query_words(const char *q, char w[][64], int max)
{
	char f[512];
	fold_key(q, f, sizeof(f));
	int n = 0;
	for (char *s = f; *s && n < max;) {
		while (*s && !word_byte((unsigned char)*s)) s++;
		if (!*s) break;
		size_t l = 0;
		while (s[l] && word_byte((unsigned char)s[l])) l++;
		size_t k = l < 62 ? l : u8_cut(s, l, 62);
		memcpy(w[n], s, k);
		w[n][k] = 0;
		w[n][63] = s[l] != 0;
		n++;
		s += l;
	}
	return n;
}

static const char *stops[] = { "a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "from", "had", "has", "have",
	"he", "her", "his", "how", "i", "if", "in", "into", "is", "it", "its", "of", "on", "or", "she", "so", "than", "that",
	"the", "their", "them", "then", "there", "these", "they", "this", "to", "was", "we", "were", "what", "when", "where",
	"which", "who", "why", "will", "with", "you", "your", "do", "can", "my", "me", NULL };

int is_stop_word(const char *w)
{
	for (int i = 0; stops[i]; i++) if (!strcmp(stops[i], w)) return 1;
	return 0;
}

/* ---- opening ---- */

static int meta(struct pack *p, const char *key, char *out, size_t n)
{
	sqlite3_stmt *st;
	out[0] = 0;
	if (sqlite3_prepare_v2(p->db, "SELECT value FROM meta WHERE key = ?", -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
	int ok = sqlite3_step(st) == SQLITE_ROW;
	if (ok) scopy(out, n, col(st, 0));
	sqlite3_finalize(st);
	return ok ? 0 : -1;
}

int pack_meta_int(int p, const char *key)
{
	char b[32];
	return meta(&packs[p], key, b, sizeof(b)) ? 0 : atoi(b);
}

static char *dup_col(sqlite3_stmt *st, int c)
{
	const char *s = (const char *)sqlite3_column_text(st, c);
	return strdup(s ? s : "");
}

static char *fold_dup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *f = malloc(n * 2 + 8);
	fold_key(s, f, n * 2 + 8);
	return f;
}

static void load_memory(struct pack *p)
{
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(p->db, "SELECT id, shelf, coalesce(category, 0), ord, title, tags, summary, coalesce(slug, '') FROM articles ORDER BY id",
	                       -1, &st, NULL) != SQLITE_OK) return;
	int cap = 0;
	while (sqlite3_step(st) == SQLITE_ROW) {
		if (p->narts == cap) { cap = cap ? cap * 2 : 64; p->arts = realloc(p->arts, (size_t)cap * sizeof(*p->arts)); }
		struct mem_article *a = &p->arts[p->narts++];
		memset(a, 0, sizeof(*a));
		a->id = sqlite3_column_int(st, 0);
		a->shelf = sqlite3_column_int(st, 1);
		a->category = sqlite3_column_int(st, 2);
		a->ord = sqlite3_column_int(st, 3);
		a->title = dup_col(st, 4);
		a->tags = dup_col(st, 5);
		a->summary = dup_col(st, 6);
		a->slug = dup_col(st, 7);
		a->ftitle = fold_dup(a->title);
		char all[1024];
		snprintf(all, sizeof(all), "%s | %s | %s", a->title, a->tags, a->summary);
		a->fsearch = fold_dup(all);
	}
	sqlite3_finalize(st);
}

static uint32_t hex_colour(const char *s)
{
	if (*s == '#') s++;
	return (uint32_t)strtoul(s, NULL, 16);
}

static int open_one(const char *path)
{
	if (npacks == MAX_PACKS) return -1;
	struct pack *p = &packs[npacks];
	memset(p, 0, sizeof(*p));
	scopy(p->path, sizeof(p->path), path);
	if (sqlite3_open_v2(path, &p->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) goto bad;
	sqlite3_exec(p->db, "PRAGMA cache_size = -2048; PRAGMA mmap_size = 0; PRAGMA query_only = 1;", NULL, NULL, NULL);
	char ver[16], idx[16], b[32];
	if (meta(p, "version", ver, sizeof(ver)) || meta(p, "name", p->name, sizeof(p->name))) goto bad;
	if (atoi(ver) != 1) goto bad;
	for (int i = 0; i < npacks; i++) if (!strcmp(packs[i].name, p->name)) { sqlite3_close(p->db); return -1; }   /* the same pack twice */
	meta(p, "index", idx, sizeof(idx));
	p->memory = !strcmp(idx, "memory");
	meta(p, "footer", p->footer, sizeof(p->footer));
	meta(p, "licence", p->licence, sizeof(p->licence));
	meta(p, "source", p->source, sizeof(p->source));
	meta(p, "notice", p->notice, sizeof(p->notice));
	p->order = meta(p, "order", b, sizeof(b)) ? 5 : atoi(b);
	p->bodies = !meta(p, "bodies", b, sizeof(b)) && atoi(b);
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(p->db, "SELECT id, name, kind, about FROM shelves ORDER BY ord", -1, &st, NULL) != SQLITE_OK) goto bad;
	while (sqlite3_step(st) == SQLITE_ROW && p->nshelves < 4) {
		struct shelf *s = &p->shelves[p->nshelves++];
		s->id = sqlite3_column_int(st, 0);
		scopy(s->name, sizeof(s->name), col(st, 1));
		scopy(s->kind, sizeof(s->kind), col(st, 2));
		scopy(s->about, sizeof(s->about), col(st, 3));
	}
	sqlite3_finalize(st);
	if (sqlite3_prepare_v2(p->db, "SELECT id, slug, name, colour, about FROM categories ORDER BY ord", -1, &st, NULL) == SQLITE_OK) {
		while (sqlite3_step(st) == SQLITE_ROW && p->ncats < 24) {
			struct category *c = &p->cats[p->ncats++];
			c->id = sqlite3_column_int(st, 0);
			scopy(c->slug, sizeof(c->slug), col(st, 1));
			scopy(c->name, sizeof(c->name), col(st, 2));
			c->colour = hex_colour(col(st, 3));
			scopy(c->about, sizeof(c->about), col(st, 4));
		}
		sqlite3_finalize(st);
	}
	p->narticles = meta(p, "articles", b, sizeof(b)) ? 0 : atoi(b);
	if (p->memory) { load_memory(p); p->narticles = p->narts; }
	if (!p->narticles) goto bad;
	npacks++;
	return 0;
bad:
	if (p->db) sqlite3_close(p->db);
	p->db = NULL;
	size_t l = strlen(pack_errors);
	const char *base = strrchr(path, '/');
	if (l && l + 2 < sizeof(pack_errors)) { memcpy(pack_errors + l, ", ", 3); l += 2; }
	scopy(pack_errors + l, sizeof(pack_errors) - l, base ? base + 1 : path);
	return -1;
}

static int by_order(const void *a, const void *b)
{
	const struct pack *x = a, *y = b;
	return x->order != y->order ? x->order - y->order : strcmp(x->name, y->name);
}

static void scan_dir(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d) return;
	char names[32][256];
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < 32) {
		size_t l = strlen(e->d_name);
		if (l > 6 && !strcmp(e->d_name + l - 6, ".wpack") && e->d_name[0] != '.') snprintf(names[n++], 256, "%s", e->d_name);
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof(names[0]), (int (*)(const void *, const void *))strcmp);
	for (int i = 0; i < n; i++) {
		char p[800];
		size_t dl = strlen(dir), nl = strlen(names[i]);
		if (dl + nl + 2 > sizeof(p)) continue;
		memcpy(p, dir, dl); p[dl] = '/'; memcpy(p + dl + 1, names[i], nl + 1);
		open_one(p);
	}
}

void packs_open(void)
{
	char p[600];
	const char *e = getenv("WIKI_DIR"), *h = getenv("HOME");
	if (e && *e) {   /* tests and other folders: WIKI_DIR=dir1:dir2 */
		char b[1024];
		scopy(b, sizeof(b), e);
		for (char *s = strtok(b, ":"); s; s = strtok(NULL, ":")) scan_dir(s);
	} else {
		scan_dir(path_join(p, sizeof(p), "/usr/share/emuzero/wiki"));
		if (h && *h) { snprintf(p, sizeof(p), "%s/Wiki", h); scan_dir(p); }
		scan_dir(path_join(p, sizeof(p), "/data/wiki"));
		if (!npacks) scan_dir("apps/wiki/packs");   /* a run from the source tree */
	}
	qsort(packs, (size_t)npacks, sizeof(packs[0]), by_order);
}

void packs_close(void)
{
	for (int i = 0; i < npacks; i++) {
		struct pack *p = &packs[i];
		for (int k = 0; k < p->narts; k++) {
			struct mem_article *a = &p->arts[k];
			free(a->title); free(a->tags); free(a->summary); free(a->slug);
			free(a->ftitle); free(a->fsearch); free(a->fbody);
		}
		free(p->arts);
		sqlite3_close(p->db);
	}
	npacks = 0;
}

int pack_by_name(const char *name)
{
	for (int i = 0; i < npacks; i++) if (!strcmp(packs[i].name, name)) return i;
	return -1;
}

const struct category *pack_category(int p, int id)
{
	for (int i = 0; i < packs[p].ncats; i++) if (packs[p].cats[i].id == id) return &packs[p].cats[i];
	return NULL;
}

/* ---- articles ---- */

int pack_article(int p, int id, char **md, size_t *n, char *title, size_t tn)
{
	sqlite3_stmt *st;
	*md = NULL;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT title, blen, body FROM articles WHERE id = ?", -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_int(st, 1, id);
	int r = -1;
	if (sqlite3_step(st) == SQLITE_ROW) {
		if (title) scopy(title, tn, col(st, 0));
		uLongf len = (uLongf)sqlite3_column_int(st, 1);
		const Bytef *z = sqlite3_column_blob(st, 2);
		uLong zn = (uLong)sqlite3_column_bytes(st, 2);
		char *b = malloc(len + 1);
		if (b && uncompress((Bytef *)b, &len, z, zn) == Z_OK) {
			b[len] = 0;
			*md = b; *n = len;
			r = 0;
		} else free(b);
	}
	sqlite3_finalize(st);
	return r;
}

int pack_title_of(int p, int id, char *title, size_t tn, int *shelf, int *category)
{
	if (packs[p].memory) {
		for (int i = 0; i < packs[p].narts; i++)
			if (packs[p].arts[i].id == id) {
				snprintf(title, tn, "%s", packs[p].arts[i].title);
				if (shelf) *shelf = packs[p].arts[i].shelf;
				if (category) *category = packs[p].arts[i].category;
				return 0;
			}
		return -1;
	}
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT title, shelf, coalesce(category, 0) FROM articles WHERE id = ?", -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_int(st, 1, id);
	int r = -1;
	if (sqlite3_step(st) == SQLITE_ROW) {
		scopy(title, tn, col(st, 0));
		if (shelf) *shelf = sqlite3_column_int(st, 1);
		if (category) *category = sqlite3_column_int(st, 2);
		r = 0;
	}
	sqlite3_finalize(st);
	return r;
}

static int id_where(int p, const char *sql, const char *arg)
{
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(packs[p].db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
	sqlite3_bind_text(st, 1, arg, -1, SQLITE_TRANSIENT);
	int id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
	sqlite3_finalize(st);
	return id;
}

int pack_find_title(int p, const char *title)
{
	char k[400];
	fold_key(title, k, sizeof(k));
	return id_where(p, "SELECT id FROM articles WHERE key = ? ORDER BY id LIMIT 1", k);
}

int pack_find_slug(int p, const char *slug) { return id_where(p, "SELECT id FROM articles WHERE slug = ?", slug); }

int pack_random(int p)
{
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT id FROM articles WHERE id >= abs(random()) % (SELECT max(id) FROM articles) + 1 ORDER BY id LIMIT 1",
	                       -1, &st, NULL) != SQLITE_OK) return 0;
	int id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
	sqlite3_finalize(st);
	return id;
}

int pack_image(int p, const char *name, uint8_t **data, size_t *n)
{
	sqlite3_stmt *st;
	*data = NULL;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT data FROM images WHERE name = ?", -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
	int r = -1;
	if (sqlite3_step(st) == SQLITE_ROW) {
		*n = (size_t)sqlite3_column_bytes(st, 0);
		*data = malloc(*n ? *n : 1);
		memcpy(*data, sqlite3_column_blob(st, 0), *n);
		r = 0;
	}
	sqlite3_finalize(st);
	return r;
}

/* ---- word lists ---- */

struct ids { uint32_t *v; int n, cap; };

static void ids_push(struct ids *l, uint32_t x)
{
	if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 256; l->v = realloc(l->v, (size_t)l->cap * sizeof(uint32_t)); }
	l->v[l->n++] = x;
}

static void unpost(const uint8_t *b, int n, struct ids *out)
{
	uint32_t cur = 0, v = 0;
	int sh = 0;
	for (int i = 0; i < n; i++) {
		v |= (uint32_t)(b[i] & 0x7f) << sh;
		if (b[i] & 0x80) { sh += 7; continue; }
		cur += v;
		ids_push(out, cur);
		v = 0; sh = 0;
	}
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

static void sort_unique(struct ids *l)
{
	if (l->n < 2) return;
	qsort(l->v, (size_t)l->n, sizeof(uint32_t), cmp_u32);
	int o = 1;
	for (int i = 1; i < l->n; i++) if (l->v[i] != l->v[o - 1]) l->v[o++] = l->v[i];
	l->n = o;
}

/* the ids of word w in table t (tw or bw); with prefix, of every word that starts with w */
static int word_ids(int p, const char *t, const char *w, int prefix, struct ids *out)
{
	char sql[160], hi[72];
	sqlite3_stmt *st;
	if (prefix) {
		snprintf(sql, sizeof(sql), "SELECT ids FROM %s WHERE word >= ?1 AND word < ?2", t);
		size_t l = strlen(w);
		memcpy(hi, w, l + 1);
		while (l && (unsigned char)hi[l - 1] == 0xff) l--;   /* the first string after every w... */
		if (!l) prefix = 0;
		else { hi[l - 1] = (char)((unsigned char)hi[l - 1] + 1); hi[l] = 0; }
	}
	if (!prefix) snprintf(sql, sizeof(sql), "SELECT ids FROM %s WHERE word = ?1", t);
	if (sqlite3_prepare_v2(packs[p].db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(st, 1, w, -1, SQLITE_STATIC);
	if (prefix) sqlite3_bind_text(st, 2, hi, -1, SQLITE_STATIC);
	int words = 0;
	while (sqlite3_step(st) == SQLITE_ROW) {
		unpost(sqlite3_column_blob(st, 0), sqlite3_column_bytes(st, 0), out);
		words++;
	}
	sqlite3_finalize(st);
	if (words > 1) sort_unique(out);
	return 0;
}

static void intersect(struct ids *a, const struct ids *b)
{
	int o = 0, j = 0;
	for (int i = 0; i < a->n; i++) {
		while (j < b->n && b->v[j] < a->v[i]) j++;
		if (j < b->n && b->v[j] == a->v[i]) a->v[o++] = a->v[i];
	}
	a->n = o;
}

static void ids_free(struct ids *l) { free(l->v); memset(l, 0, sizeof(*l)); }

/* does the title have a word starting with w (folded)? */
static int title_has_prefix(const char *ftitle, const char *w)
{
	size_t l = strlen(w);
	for (const char *s = ftitle; *s;) {
		while (*s && !word_byte((unsigned char)*s)) s++;
		if (!strncmp(s, w, l)) return 1;
		while (*s && word_byte((unsigned char)*s)) s++;
	}
	return 0;
}

static void add_result(struct result *out, int *n, int max, int p, int id, const char *title, int section)
{
	for (int i = 0; i < *n; i++) if (out[i].pack == p && out[i].id == id) return;
	if (*n >= max) return;
	struct result *r = &out[(*n)++];
	memset(r, 0, sizeof(*r));
	r->pack = p; r->id = id; r->section = section;
	scopy(r->title, sizeof(r->title), title);
}

static void tag_for(struct result *r)
{
	struct pack *p = &packs[r->pack];
	if (!p->memory) return;
	for (int i = 0; i < p->narts; i++) {
		struct mem_article *a = &p->arts[i];
		if (a->id != r->id) continue;
		const struct category *c = a->category ? pack_category(r->pack, a->category) : NULL;
		if (c) { scopy(r->tag, sizeof(r->tag), c->name); r->tag_colour = c->colour; }
		else snprintf(r->tag, sizeof(r->tag), "FM 21-76 · %d", a->ord);
		return;
	}
}

/* a small pack: every word of q in the title, tags or summary (titles that start with the
 * query first, then title matches, then the rest) */
static void search_memory(int p, char w[][64], int nw, const char *fq, struct result *out, int *n, int max)
{
	struct pack *pk = &packs[p];
	int short_q = strlen(fq) < 3, cap = *n + (short_q ? 5 : 10);   /* leave room for Wikipedia below */
	if (cap < max) max = cap;
	for (int pass = 0; pass < (short_q ? 2 : 3); pass++) {
		for (int i = 0; i < pk->narts; i++) {
			struct mem_article *a = &pk->arts[i];
			int ok = 1;
			if (pass == 0) ok = !strncmp(a->ftitle, fq, strlen(fq));
			for (int k = 0; k < nw && ok; k++) {
				if (pass <= 1) ok = title_has_prefix(a->ftitle, w[k]);
				else ok = title_has_prefix(a->fsearch, w[k]);
			}
			if (ok) add_result(out, n, max, p, a->id, a->title, 0);
		}
	}
}

static void title_from_db(int p, struct result *out, int *n, int max, const struct ids *l, int limit, const char (*w)[64], int nw, int check_from)
{
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT title, key FROM articles WHERE id = ?", -1, &st, NULL) != SQLITE_OK) return;
	int added = 0;
	for (int i = 0; i < l->n && added < limit && *n < max; i++) {
		sqlite3_bind_int(st, 1, (int)l->v[i]);
		if (sqlite3_step(st) == SQLITE_ROW) {
			const char *key = col(st, 1);
			int ok = 1;
			for (int k = check_from; k < nw && ok; k++) ok = title_has_prefix(key, w[k]);
			if (ok) { add_result(out, n, max, p, (int)l->v[i], col(st, 0), 1); added++; }
		}
		sqlite3_reset(st);
	}
	sqlite3_finalize(st);
}

static int count_words(const char *s)
{
	int n = 0;
	for (; *s;) {
		while (*s && !word_byte((unsigned char)*s)) s++;
		if (*s) n++;
		while (*s && word_byte((unsigned char)*s)) s++;
	}
	return n;
}

struct cand { uint32_t id; double rank; };
static int by_rank(const void *a, const void *b)
{
	const struct cand *x = a, *y = b;
	return x->rank < y->rank ? -1 : x->rank > y->rank;
}

/* titles that start with what was typed: through the key index alone (key and id), the first
 * 2000 in key order, ranked by score with longer titles pushed down (build-pack.py ranks the
 * top lists the same way) */
static void starts_with(int p, const char *fq, struct result *out, int *n, int max, int limit)
{
	char hi[260];
	size_t l = strlen(fq);
	if (!l || l >= sizeof(hi)) return;
	memcpy(hi, fq, l + 1);
	while (l && (unsigned char)hi[l - 1] == 0xff) l--;
	if (!l) return;
	hi[l - 1] = (char)((unsigned char)hi[l - 1] + 1);
	hi[l] = 0;
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(packs[p].db, "SELECT id, key FROM articles WHERE key >= ?1 AND key < ?2 LIMIT 2000", -1, &st, NULL) != SQLITE_OK) return;
	sqlite3_bind_text(st, 1, fq, -1, SQLITE_STATIC);
	sqlite3_bind_text(st, 2, hi, -1, SQLITE_STATIC);
	static struct cand c[2000];
	int nc = 0, qw = count_words(fq);
	while (sqlite3_step(st) == SQLITE_ROW && nc < 2000) {
		int extra = count_words(col(st, 1)) - qw;
		c[nc].id = (uint32_t)sqlite3_column_int(st, 0);
		c[nc].rank = c[nc].id * (1.0 + 0.5 * (extra > 0 ? extra : 0));
		nc++;
	}
	sqlite3_finalize(st);
	qsort(c, (size_t)nc, sizeof(c[0]), by_rank);
	struct ids l2 = { 0 };
	for (int i = 0; i < nc && i < limit; i++) ids_push(&l2, c[i].id);
	title_from_db(p, out, n, max, &l2, limit, NULL, 0, 0);
	ids_free(&l2);
}

/* a large pack: the exact title first, then titles that start with the query, then titles
 * with every word (the last as a prefix) */
static void search_words(int p, char w[][64], int nw, const char *fq, struct result *out, int *n, int max)
{
	int limit = 30;
	int exact = id_where(p, "SELECT id FROM articles WHERE key = ? ORDER BY id LIMIT 1", fq);
	if (exact) {
		char t[160];
		if (!pack_title_of(p, exact, t, sizeof(t), NULL, NULL)) add_result(out, n, max, p, exact, t, 1);
	}
	struct ids l = { 0 };
	int last_prefix = !w[nw - 1][63];
	if (!(nw == 1 && last_prefix && strlen(w[0]) <= 3)) starts_with(p, fq, out, n, max, 15);
	if (nw == 1 && last_prefix && strlen(w[0]) <= 3) {   /* one to three letters: the top list */
		sqlite3_stmt *st;
		if (sqlite3_prepare_v2(packs[p].db, "SELECT ids FROM top WHERE prefix = ?", -1, &st, NULL) == SQLITE_OK) {
			sqlite3_bind_text(st, 1, w[0], -1, SQLITE_STATIC);
			if (sqlite3_step(st) == SQLITE_ROW) {
				const uint8_t *b = sqlite3_column_blob(st, 0);
				int k = sqlite3_column_bytes(st, 0) / 4;
				for (int i = 0; i < k; i++) ids_push(&l, (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 | (uint32_t)b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24);
			}
			sqlite3_finalize(st);
		}
		title_from_db(p, out, n, max, &l, limit, (const char (*)[64])w, nw, nw);
		ids_free(&l);
		return;
	}
	/* the index answers for whole words and prefixes of 3+ letters; a shorter last word is
	 * checked against each candidate's title instead */
	int have = 0, check_from = nw;
	for (int k = 0; k < nw; k++) {
		int pre = k == nw - 1 && last_prefix;
		if (pre && strlen(w[k]) < 3 && nw > 1) { check_from = k; continue; }
		struct ids m = { 0 };
		word_ids(p, "tw", w[k], pre, &m);
		if (!have) { l = m; have = 1; }
		else { intersect(&l, &m); ids_free(&m); }
		if (!l.n) break;
	}
	title_from_db(p, out, n, max, &l, limit, (const char (*)[64])w, nw, check_from);
	ids_free(&l);
}

int search_titles(const char *q, struct result *out, int max)
{
	uint32_t t0 = real_ms();
	char w[8][64], fq[256];
	int nw = query_words(q, w, 8);
	fold_key(q, fq, sizeof(fq));
	int n = 0;
	if (!nw) return 0;
	for (int p = 0; p < npacks; p++) {
		if (packs[p].memory) search_memory(p, w, nw, fq, out, &n, max);
		else search_words(p, w, nw, fq, out, &n, max);
	}
	for (int i = 0; i < n; i++) tag_for(&out[i]);
	timing_log("titles", t0);
	return n;
}

/* ---- search inside articles ---- */

static void load_bodies(int p)
{
	struct pack *pk = &packs[p];
	for (int i = 0; i < pk->narts; i++) {
		struct mem_article *a = &pk->arts[i];
		if (a->fbody) continue;
		char *md; size_t n;
		if (pack_article(p, a->id, &md, &n, NULL, 0)) { a->fbody = strdup(""); continue; }
		a->fbody = malloc(n * 2 + 8);
		a->fblen = fold_key(md, a->fbody, n * 2 + 8);
		free(md);
	}
}

static int has_word(const char *hay, const char *w)
{
	size_t l = strlen(w);
	for (const char *s = hay; (s = strstr(s, w)); s++) {
		int before = s == hay || !word_byte((unsigned char)s[-1]);
		int after = !word_byte((unsigned char)s[l]) || (s[l] == 's' && !word_byte((unsigned char)s[l + 1]));   /* plural too */
		if (before && after) return 1;
	}
	return 0;
}

static int count_word(const char *hay, const char *w)
{
	size_t l = strlen(w);
	int n = 0;
	for (const char *s = hay; (s = strstr(s, w)); s += l) {
		int before = s == hay || !word_byte((unsigned char)s[-1]);
		int after = !word_byte((unsigned char)s[l]) || (s[l] == 's' && !word_byte((unsigned char)s[l + 1]));
		if (before && after) n++;
	}
	return n;
}

/* how well a folded text answers the words: how often each is there (each capped, so one word
 * repeated cannot carry it), more when they share a line */
static double relevance(const char *f, char w[][64], int nw, int *same_line)
{
	double r = 0;
	*same_line = 0;
	for (int k = 0; k < nw; k++) { int c = count_word(f, w[k]); r += c > 8 ? 8 : c; if (!c) r -= 20; }
	if (nw > 1) {
		for (const char *line = f; *line;) {
			const char *e = strchr(line, '\n');
			size_t len = e ? (size_t)(e - line) : strlen(line);
			char buf[2048];
			if (len < sizeof(buf)) {
				memcpy(buf, line, len); buf[len] = 0;
				int all = 1;
				for (int k = 0; k < nw && all; k++) all = count_word(buf, w[k]) > 0;
				if (all) { r += 6; *same_line = 1; break; }
			}
			if (!e) break;
			line = e + 1;
		}
	}
	return r;
}

#define RERANK 60   /* text matches read and reordered: each is an article unpacked */
struct rel { uint32_t id; double r; };
static int by_rel(const void *a, const void *b)
{
	const struct rel *x = a, *y = b;
	if (x->r != y->r) return x->r > y->r ? -1 : 1;
	return x->id < y->id ? -1 : x->id > y->id;
}

/* the first n ids, reordered by relevance (their texts unpacked and read) */
static void rerank(int p, struct ids *l, int n, char w[][64], int nw)
{
	if (n > l->n) n = l->n;
	struct rel *r = malloc(sizeof(*r) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		char *md; size_t len;
		r[i].id = l->v[i];
		r[i].r = 0;
		if (pack_article(p, (int)l->v[i], &md, &len, NULL, 0)) continue;
		char *f = malloc(len * 2 + 8);
		fold_key(md, f, len * 2 + 8);
		int line;
		r[i].r = relevance(f, w, nw, &line);
		free(f); free(md);
	}
	qsort(r, (size_t)n, sizeof(*r), by_rel);
	for (int i = 0; i < n; i++) l->v[i] = r[i].id;
	free(r);
}

int search_inside(const char *q, struct result *out, int max, int *too_common)
{
	uint32_t t0 = real_ms();
	char w[8][64], keep[8][64];
	int nw = query_words(q, w, 8), nk = 0;
	*too_common = 0;
	for (int k = 0; k < nw; k++) if (!is_stop_word(w[k]) && strlen(w[k]) > 1) memcpy(keep[nk++], w[k], 64);
	if (!nk) {   /* only common words: search them anyway, they are all the query has */
		for (int k = 0; k < nw; k++) if (strlen(w[k]) > 1) memcpy(keep[nk++], w[k], 64);
		*too_common = nk > 0;
	}
	if (!nk) return 0;
	/* in this order: survival titles, Wikipedia titles, survival texts where the words share a
	 * line (or one word comes often), Wikipedia texts by relevance, the other survival texts */
	struct result *tier[5];
	int tn[5] = { 0 };
	for (int k = 0; k < 5; k++) tier[k] = malloc(sizeof(struct result) * MAX_RESULTS);
	for (int p = 0; p < npacks; p++) {
		struct pack *pk = &packs[p];
		if (pk->memory) {
			load_bodies(p);
			struct rel *r = malloc(sizeof(*r) * (size_t)pk->narts);
			int nr = 0;
			for (int i = 0; i < pk->narts; i++) {
				struct mem_article *a = &pk->arts[i];
				int ok = 1, in_title = 1;
				for (int k = 0; k < nk && ok; k++) {
					int t = has_word(a->ftitle, keep[k]);
					in_title &= t;
					ok = t || has_word(a->fbody, keep[k]) || has_word(a->fsearch, keep[k]);
				}
				if (!ok) continue;
				int line, in_tags = 1;
				double rel = relevance(a->fbody, keep, nk, &line);
				for (int k = 0; k < nk; k++) in_tags &= has_word(a->fsearch, keep[k]);
				int strong = in_tags || (nk > 1 ? line : count_word(a->fbody, keep[0]) >= 3);
				int k = in_title ? 0 : strong ? 2 : 4;
				r[nr].id = (uint32_t)i;
				r[nr++].r = k * -10000.0 + rel;   /* the tier first, then relevance */
			}
			qsort(r, (size_t)nr, sizeof(*r), by_rel);
			for (int i = 0; i < nr; i++) {
				int k = r[i].r > -5000 ? 0 : r[i].r > -25000 ? 2 : 4;
				add_result(tier[k], &tn[k], MAX_RESULTS, p, pk->arts[r[i].id].id, pk->arts[r[i].id].title, 0);
			}
			free(r);
			continue;
		}
		/* titles with every word (score order), then texts with every word: the first ones
		 * reordered by how well they answer */
		struct ids t = { 0 }, b = { 0 };
		int have = 0;
		for (int k = 0; k < nk; k++) {
			struct ids m = { 0 };
			word_ids(p, "tw", keep[k], 0, &m);
			if (!have) { t = m; have = 1; } else { intersect(&t, &m); ids_free(&m); }
		}
		title_from_db(p, tier[1], &tn[1], MAX_RESULTS, &t, 10, NULL, 0, 0);
		if (pk->bodies) {
			have = 0;
			for (int k = 0; k < nk; k++) {
				struct ids m = { 0 };
				word_ids(p, "bw", keep[k], 0, &m);
				if (!have) { b = m; have = 1; } else { intersect(&b, &m); ids_free(&m); }
				if (!b.n) break;
			}
			uint32_t t1 = real_ms();
			rerank(p, &b, RERANK, keep, nk);
			timing_log("rerank", t1);
			title_from_db(p, tier[3], &tn[3], MAX_RESULTS, &b, MAX_RESULTS, NULL, 0, 0);
		}
		ids_free(&t); ids_free(&b);
	}
	int n = 0;
	for (int k = 0; k < 5; k++) {
		for (int i = 0; i < tn[k]; i++) add_result(out, &n, max, tier[k][i].pack, tier[k][i].id, tier[k][i].title, tier[k][i].section);
		free(tier[k]);
	}
	for (int i = 0; i < n; i++) tag_for(&out[i]);
	timing_log("inside", t0);
	return n;
}

/* the first line of the article where a word of q is, as plain text, the words marked */
void result_snippet(struct result *r, const char *q)
{
	r->has_snip = 1;
	char *md; size_t n;
	if (pack_article(r->pack, r->id, &md, &n, NULL, 0)) return;
	char w[8][64];
	int nw = query_words(q, w, 8), nk = 0;
	char keep[8][64];
	for (int k = 0; k < nw; k++) if (!is_stop_word(w[k]) && strlen(w[k]) > 1) memcpy(keep[nk++], w[k], 64);
	if (!nk) for (int k = 0; k < nw; k++) memcpy(keep[nk++], w[k], 64);
	char *f = malloc(n + 1);
	fold_text(md, n, f);   /* one byte per byte: offsets line up with md */
	const char *best = NULL;
	for (int k = 0; k < nk && !best; k++) {
		size_t l = strlen(keep[k]);
		for (const char *s = f; (s = strstr(s, keep[k])); s++) {
			if ((s == f || !word_byte((unsigned char)s[-1])) && !word_byte((unsigned char)s[l]) && s[l] != 1) { best = s; break; }
			if ((s == f || !word_byte((unsigned char)s[-1])) && s[l] == 's') { best = s; break; }
		}
	}
	if (best) {
		size_t at = (size_t)(best - f), ls = at, le = at;
		while (ls > 0 && md[ls - 1] != '\n') ls--;
		while (le < n && md[le] != '\n') le++;
		char plain[400], fp[400];
		/* links show their text only: drop "(id:N)" and the brackets */
		char line[400];
		size_t o = 0;
		for (size_t i = ls; i < le && o + 1 < sizeof(line); i++) {
			if (md[i] == '(' && i > ls && md[i - 1] == ']' && !strncmp(md + i, "(id:", 4)) { while (i < le && md[i] != ')') i++; continue; }
			if ((md[i] == '[' || md[i] == ']') && (i == ls || md[i - 1] != '\\')) continue;
			line[o++] = md[i];
		}
		line[o] = 0;
		md_plain_line(line, o, plain, sizeof(plain));
		size_t pl = strlen(plain), start = 0;
		fold_text(plain, pl, fp);
		const char *f0 = NULL;
		for (int k = 0; k < nk && !f0; k++) f0 = strstr(fp, keep[k]);
		if (f0 && f0 - fp > 24) {
			start = (size_t)(f0 - fp) - 18;
			while (start > 0 && plain[start] != ' ') start--;
			while (start < pl && ((unsigned char)plain[start] & 0xc0) == 0x80) start++;
		}
		size_t so = 0;
		if (start) { memcpy(r->snip, "…", 3); so = 3; }
		size_t len = pl - start;
		if (len > sizeof(r->snip) - 1 - so) len = u8_whole(plain + start, sizeof(r->snip) - 1 - so);
		memcpy(r->snip + so, plain + start, len);
		r->snip[so + len] = 0;
		for (int k = 0; k < nk && r->nhl < 6; k++) {
			const char *m = strstr(fp + start, keep[k]);
			if (m && (size_t)(m - (fp + start)) < len) {
				r->hl[r->nhl][0] = (int)(so + (size_t)(m - (fp + start)));
				r->hl[r->nhl][1] = r->hl[r->nhl][0] + (int)strlen(keep[k]);
				r->nhl++;
			}
		}
	}
	if (!best) {   /* matched by title or tags only: the guide's summary, or the first line of text */
		struct pack *pk = &packs[r->pack];
		for (int i = 0; pk->memory && i < pk->narts; i++)
			if (pk->arts[i].id == r->id) scopy(r->snip, sizeof(r->snip), pk->arts[i].summary);
		for (size_t a = 0; !r->snip[0] && a < n;) {
			size_t e = a;
			while (e < n && md[e] != '\n') e++;
			struct linfo li;
			line_info(md + a, e - a, 0, &li);
			if (li.kind == LK_TEXT && md[a] != '!') {
				char plain[400];
				md_plain_line(md + a, e - a, plain, sizeof(plain));
				scopy(r->snip, sizeof(r->snip), plain);
			}
			a = e + 1;
		}
	}
	free(f);
	free(md);
}
