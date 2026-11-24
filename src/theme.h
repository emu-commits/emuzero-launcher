// SPDX-License-Identifier: MIT
// Look of the home screen. Row 9 loads these from a theme folder; until then the built-in
// values below are the one theme.
#pragma once
#include <stdint.h>

struct theme {
	uint32_t bg, fg, dim, line, accent, sel_bg;
	const char *layout;   /* "carousel" or "grid" */
};

extern struct theme theme;
