// SPDX-License-Identifier: MIT
#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

const char *path_root(void)
{
	const char *r = getenv("EMUZERO_ROOT");
	return r ? r : "";
}

char *path_join(char *buf, size_t n, const char *p)
{
	snprintf(buf, n, "%s%s", path_root(), p);
	return buf;
}

/* a font file by name: the device's font folder first (under the root), then where desktop
 * distributions keep DejaVu; the result stays valid until the next call */
const char *path_font_named(const char *file)
{
	static const char *dirs[] = { "/usr/share/fonts/dejavu", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF" };
	static char found[512];
	for (int rooted = 1; rooted >= 0; rooted--)
		for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
			snprintf(found, sizeof(found), "%s%s/%s", rooted ? path_root() : "", dirs[i], file);
			if (!access(found, R_OK)) return found;
		}
	return NULL;
}

const char *path_font(int bold)
{
	static char found[2][512];
	if (!found[bold][0]) {
		const char *f = path_font_named(bold ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf");
		if (!f) return NULL;
		snprintf(found[bold], sizeof(found[bold]), "%s", f);
	}
	return found[bold];
}
