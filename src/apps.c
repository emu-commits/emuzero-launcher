// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "apps.h"
#include "paths.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct app apps[APPS_MAX];
int napps;

/* Where apps come from. APPLaunch entries (M5's launcher and the Store) give icons relative to
 * /usr/share/APPLaunch; entries elsewhere follow the freedesktop rules. Earlier directories win
 * when two hold the same id. */
static const struct { const char *dir, *icon_base; } sources[] = {
	{ "/usr/local/share/applications", NULL },
	{ "/usr/share/applications", NULL },
	{ "/usr/share/APPLaunch/applications", "/usr/share/APPLaunch" },
};

/* "Desktop;emuZero;" contains the word emuZero */
static int list_has(const char *list, const char *word)
{
	size_t n = strlen(word);
	for (const char *p = list; (p = strstr(p, word)); p += n)
		if ((p == list || p[-1] == ';') && (p[n] == ';' || p[n] == 0)) return 1;
	return 0;
}

static int readable(const char *p) { return p[0] && !access(p, R_OK); }

/* Icon= is an absolute file, a file relative to the source's base, or a theme icon name */
static void find_icon(struct app *a, const char *icon, const char *base)
{
	char p[512];
	a->icon[0] = 0;
	if (!icon[0]) return;
	if (icon[0] == '/') {
		path_join(p, sizeof(p), icon);
		if (readable(p)) snprintf(a->icon, sizeof(a->icon), "%s", p);
		return;
	}
	if (base) {
		char rel[512];
		snprintf(rel, sizeof(rel), "%s/%s", base, icon);
		path_join(p, sizeof(p), rel);
		if (readable(p)) { snprintf(a->icon, sizeof(a->icon), "%s", p); return; }
	}
	/* a theme icon name: the PNG sizes we can use, largest first, then pixmaps */
	static const char *sizes[] = { "128x128", "96x96", "64x64", "48x48" };
	const char *ext = strchr(icon, '.') ? "" : ".png";
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		char rel[512];
		snprintf(rel, sizeof(rel), "/usr/share/icons/hicolor/%s/apps/%s%s", sizes[i], icon, ext);
		path_join(p, sizeof(p), rel);
		if (readable(p)) { snprintf(a->icon, sizeof(a->icon), "%s", p); return; }
	}
	char rel[512];
	snprintf(rel, sizeof(rel), "/usr/share/pixmaps/%s%s", icon, ext);
	path_join(p, sizeof(p), rel);
	if (readable(p)) snprintf(a->icon, sizeof(a->icon), "%s", p);
}

static int known(const char *id)
{
	for (int i = 0; i < napps; i++)
		if (!strcmp(apps[i].id, id)) return 1;
	return 0;
}

static void read_entry(const char *file, const char *id, const char *base)
{
	FILE *f = fopen(file, "r");
	if (!f) return;
	struct app a = { 0 };
	char line[768], icon[512] = "";
	int in_entry = 0, hidden = 0, is_app = 1;
	snprintf(a.id, sizeof(a.id), "%s", id);
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '[') { in_entry = !strcmp(line, "[Desktop Entry]"); continue; }
		if (!in_entry || line[0] == '#') continue;   /* [Desktop Action ...] groups have their own Name/Exec */
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		const char *k = line, *v = eq + 1;
		if (!strcmp(k, "Name")) snprintf(a.name, sizeof(a.name), "%s", v);
		else if (!strcmp(k, "Comment")) snprintf(a.comment, sizeof(a.comment), "%s", v);
		else if (!strcmp(k, "Exec")) snprintf(a.exec, sizeof(a.exec), "%s", v);
		else if (!strcmp(k, "Icon")) snprintf(icon, sizeof(icon), "%s", v);
		else if (!strcmp(k, "Terminal")) a.terminal = !strcmp(v, "true");
		else if (!strcmp(k, "Type")) is_app = !strcmp(v, "Application");
		else if ((!strcmp(k, "Hidden") || !strcmp(k, "NoDisplay")) && !strcmp(v, "true")) hidden = 1;
		else if (!strcmp(k, "OnlyShowIn") && !list_has(v, "emuZero")) hidden = 1;
		else if (!strcmp(k, "NotShowIn") && list_has(v, "emuZero")) hidden = 1;
	}
	fclose(f);
	if (hidden || !is_app || !a.name[0] || !a.exec[0]) return;
	find_icon(&a, icon, base);
	apps[napps++] = a;
}

/* sort key: lower case, with the accented Latin letters (U+00C0..U+00FF) as their base letter,
 * so "Éclair" sorts among the E's */
static void sort_key(char *out, size_t n, const char *s)
{
	static const char base[65] = "aaaaaaaceeeeiiiidnooooo/ouuuuyts" "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && o + 1 < n; p++) {
		if (p[0] == 0xc3 && p[1] >= 0x80 && p[1] <= 0xbf) { out[o++] = base[(p[1] - 0x80) % 32]; p++; }
		else out[o++] = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
	}
	out[o] = 0;
}

static int by_name(const void *x, const void *y)
{
	const struct app *a = x, *b = y;
	char ka[64], kb[64];
	sort_key(ka, sizeof(ka), a->name);
	sort_key(kb, sizeof(kb), b->name);
	int c = strcmp(ka, kb);
	return c ? c : strcmp(a->id, b->id);
}

static int is_desktop(const struct dirent *e)
{
	size_t l = strlen(e->d_name);
	return l > 8 && !strcmp(e->d_name + l - 8, ".desktop");
}

int apps_scan(void)
{
	napps = 0;
	for (size_t s = 0; s < sizeof(sources) / sizeof(sources[0]); s++) {
		char dir[512];
		struct dirent **list;
		path_join(dir, sizeof(dir), sources[s].dir);
		int n = scandir(dir, &list, is_desktop, alphasort);
		if (n < 0) continue;
		for (int i = 0; i < n; i++) {
			char file[1024], id[64];
			snprintf(id, sizeof(id), "%.*s", (int)strlen(list[i]->d_name) - 8, list[i]->d_name);
			snprintf(file, sizeof(file), "%s/%s", dir, list[i]->d_name);
			if (napps < APPS_MAX && !known(id)) read_entry(file, id, sources[s].icon_base);
			free(list[i]);
		}
		free(list);
	}
	qsort(apps, napps, sizeof(apps[0]), by_name);
	return napps;
}
