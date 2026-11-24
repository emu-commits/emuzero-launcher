// SPDX-License-Identifier: MIT
// App icons at the sizes the layouts draw them. PNG icons are decoded once per size and scaled
// down with an area average (sharper than scaling at draw time, and no work on each redraw);
// apps without an icon get a drawn tile: a coloured square with the name's first letter.
#pragma once
#include "lvgl/lvgl.h"
#include "apps.h"

/* a size x size object showing the app's icon */
lv_obj_t *icon_create(lv_obj_t *parent, const struct app *a, int size);
/* a PNG picture covering w x h (wallpaper); stays valid until the next call; NULL if unreadable */
lv_draw_buf_t *picture_load(const char *path, int w, int h);
/* drop decoded icons (after a rescan) */
void icon_cache_clear(void);
