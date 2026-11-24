// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl/lvgl.h"

/* fonts (DejaVu through FreeType); returns 0 on success */
int ui_fonts(void);
/* DejaVu Sans at px pixels, regular or bold; made on first use and kept */
lv_font_t *ui_font(int px, int bold);
/* a label in font f and colour c */
lv_obj_t *ui_label(lv_obj_t *parent, lv_font_t *f, uint32_t c, const char *text);

/* layout by name ("carousel", "grid") before ui_create; returns 0 if known */
int ui_set_layout(const char *name);
/* build the home screen and take key events through group g */
void ui_create(lv_group_t *g);
