// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl/lvgl.h"
#include "theme.h"

/* the theme (from the settings) and its fonts, through FreeType; returns 0 on success */
int ui_fonts(void);
/* DejaVu Sans at px pixels, regular or bold; made on first use and kept */
lv_font_t *ui_font(int px, int bold);
/* a 3-D frame w pixels wide (BEVEL_RAISED or BEVEL_SUNKEN) in the theme's light and shadow;
 * o must have no padding and its final size */
void ui_bevel(lv_obj_t *o, int style, int w);
/* a label in font f and colour c */
lv_obj_t *ui_label(lv_obj_t *parent, lv_font_t *f, uint32_t c, const char *text);

/* layout by name ("carousel", "grid") or theme for this run, before ui_fonts; 0 if known */
int ui_set_layout(const char *name);
void ui_set_theme(const char *id);
/* where pages (App info, Theme) go: the screen without the status bar */
struct ui_rect { int x, y, w, h; };
struct ui_rect ui_page_rect(void);
/* their background: the window's colour in a theme that has one (its text colours go with it) */
uint32_t ui_page_bg(void);
/* change theme and layout ("" = the theme's own) and draw everything again; ui_save keeps them */
void ui_apply(const char *theme_id, const char *layout_name);
void ui_save(void);
const struct settings *ui_settings(void);
/* build the home screen and take key events through group g */
void ui_create(lv_group_t *g);

/* start-up timing (main.c): note a step; the list so far ("main 12, display 30, ...") */
void startup_mark(const char *what);
const char *startup_marks(void);
