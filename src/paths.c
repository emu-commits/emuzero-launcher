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

const char *path_font(int bold)
{
	/* the device's DejaVu first (under the root), then where desktop distributions keep it */
	static const char *dirs[] = { "/usr/share/fonts/dejavu", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF" };
	static char found[2][512];
	const char *file = bold ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf";
	if (found[bold][0]) return found[bold];
	for (int rooted = 1; rooted >= 0; rooted--)
		for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
			snprintf(found[bold], sizeof(found[bold]), "%s%s/%s", rooted ? path_root() : "", dirs[i], file);
			if (!access(found[bold], R_OK)) return found[bold];
		}
	found[bold][0] = 0;
	return NULL;
}
