// SPDX-License-Identifier: MIT
// The status bar: the theme's title on the left; USB, Wi-Fi, time and battery on the right.
// Top or bottom, height and look come from the theme (a height of 0: no bar).
#pragma once
#include "lvgl/lvgl.h"

/* build the bar on scr (again after a theme change); it then updates itself each minute */
void statusbar_create(lv_obj_t *scr);
/* read everything again now (after a key, an app, waking the screen); redraws only changes */
void statusbar_update(void);
/* rows the bar takes, with its line */
int statusbar_height(void);
