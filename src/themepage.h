// SPDX-License-Identifier: MIT
// The Theme page (T, or the Theme tile): choose the theme and the home layout.
#pragma once
#include "lvgl/lvgl.h"

/* open the page on scr; done() runs after it closes */
void themepage_open(lv_obj_t *scr, void (*done)(void));
int themepage_active(void);
/* keys while the page is open */
void themepage_key(uint32_t key);
