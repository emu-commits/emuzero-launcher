// SPDX-License-Identifier: MIT
// App icons at the sizes the layouts draw them. PNG icons are decoded once per size and scaled
// down with an area average (sharper than scaling at draw time, and no work on each redraw);
// apps without an icon get a drawn tile: a coloured square with the name's first letter.
#pragma once
#include "lvgl/lvgl.h"
#include "apps.h"

/* a size x size object showing the app's icon */
lv_obj_t *icon_create(lv_obj_t *parent, const struct app *a, int size);
/* drop decoded icons (after a rescan) */
void icon_cache_clear(void);
