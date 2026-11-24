// SPDX-License-Identifier: MIT
// The status bar along the top: name on the left; USB, Wi-Fi, time and battery on the right.
#pragma once
#include "lvgl/lvgl.h"

#define BAR_H 18   /* bar height; a 1-pixel line under it */

/* build the bar on scr; it then updates itself at each new minute */
void statusbar_create(lv_obj_t *scr);
/* read everything again now (after a key, an app, waking the screen); redraws only changes */
void statusbar_update(void);
