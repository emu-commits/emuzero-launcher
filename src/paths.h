// SPDX-License-Identifier: MIT
// Where the launcher finds things. EMUZERO_ROOT (default empty) prefixes every system path, so
// the desktop builds can run against test/root instead of the device's file system.
#pragma once
#include <stddef.h>

const char *path_root(void);
/* root + p into buf; returns buf */
char *path_join(char *buf, size_t n, const char *p);
/* first readable font file for the regular or bold face, or NULL */
const char *path_font(int bold);
/* a font file by name (e.g. "DejaVuSansMono.ttf") in the font folders, or NULL */
const char *path_font_named(const char *file);
