// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl/lvgl.h"

/* fonts (DejaVu through FreeType); returns 0 on success */
int ui_fonts(void);
/* build the first screen and take key events through group g */
void ui_create(lv_group_t *g);
