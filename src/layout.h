// SPDX-License-Identifier: MIT
// Home screen layouts. The home screen owns the app list, the selection and the keys; a layout
// only draws the apps into the area below the status bar and says where an arrow key moves
// the selection. Adding a layout = one file with one of these plus an entry in layouts[].
#pragma once
#include "lvgl/lvgl.h"

struct layout {
	const char *name;
	/* build the layout's objects inside area (already empty, sized to the content area) */
	void (*create)(lv_obj_t *area);
	/* show the apps with app sel selected (also called after the app list changes) */
	void (*show)(int sel);
	/* the selection after an arrow key (LV_KEY_UP/DOWN/LEFT/RIGHT) from sel; n apps, n > 0 */
	int (*move)(uint32_t key, int sel, int n);
};

extern const struct layout layout_carousel, layout_grid;
extern const struct layout *const layouts[];   /* NULL-terminated */
const struct layout *layout_find(const char *name);
