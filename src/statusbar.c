// SPDX-License-Identifier: MIT
// Updates come at each new minute (with the clock), and whenever the home screen asks: on a
// key, after an app, after the screen wakes. Nothing polls in between, so the launcher keeps
// sleeping while the device sits idle; a value that did not change is not redrawn.
// The theme decides where the bar sits (top or bottom), its height and colours, the title on
// the left and how the badges look.
#include "statusbar.h"
#include "platform.h"
#include "status.h"
#include "theme.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BAT_LOW 15
#define BAT_MID 35

static lv_obj_t *usb, *wifi, *bars[4], *clock_badge, *bat;
static struct status shown;
static char shown_time[8];
static lv_timer_t *minute;

static void badge_style(lv_obj_t *l, uint32_t fill)
{
	int st = theme.badge_style;
	lv_obj_set_style_bg_opa(l, st == BADGE_PLAIN ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(l, lv_color_hex(fill), 0);
	lv_obj_set_style_radius(l, st == BADGE_ROUND ? 4 : 0, 0);
	lv_obj_set_style_pad_hor(l, st == BADGE_PLAIN ? 1 : 4, 0);
	lv_obj_set_style_pad_ver(l, 1, 0);
	if (st == BADGE_SUNKEN) {   /* shade along the top and left edges */
		lv_obj_set_style_border_width(l, 1, 0);
		lv_obj_set_style_border_side(l, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
		lv_obj_set_style_border_color(l, lv_color_hex(theme.shadow), 0);
	}
}

static lv_obj_t *badge(lv_obj_t *parent, uint32_t fill, uint32_t text)
{
	lv_obj_t *l = ui_label(parent, ui_font(11, 1), theme.badge_style == BADGE_PLAIN ? theme.bar_fg : text, "");
	badge_style(l, fill);
	return l;
}

/* a bar in the screen's own colours uses the theme's dim and line; one of its own, a mix */
static int plain_bar(void) { return theme.bar_bg == theme.bg && theme.bar_fg == theme.fg; }

static void show(lv_obj_t *o, int on)
{
	if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
	else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static int ms_to_next_minute(void)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return (int)(60000 - (t.tv_sec % 60) * 1000 - t.tv_nsec / 1000000) + 5;
}

void statusbar_update(void)
{
	if (!clock_badge) return;   /* a theme without a bar */
	char t[8];
	time_t now = time(NULL);
	strftime(t, sizeof(t), "%H:%M", localtime(&now));
	if (strcmp(t, shown_time)) {
		snprintf(shown_time, sizeof(shown_time), "%s", t);
		lv_label_set_text(clock_badge, t);
	}

	struct status s;
	status_read(&s);
	if (s.battery != shown.battery || s.charging != shown.charging) {
		show(bat, s.battery >= 0);
		if (s.battery >= 0) {
			uint32_t c = s.charging ? 0x5fcf7a : s.battery <= BAT_LOW ? 0xe5534b : s.battery <= BAT_MID ? 0xf2c14e : 0x5fcf7a;
			if (theme.badge_style == BADGE_PLAIN) {   /* no fill: the warning colours go to the text */
				if (!s.charging && s.battery <= BAT_MID) lv_obj_set_style_text_color(bat, lv_color_hex(c), 0);
				else lv_obj_set_style_text_color(bat, lv_color_hex(theme.bar_fg), 0);
			} else lv_obj_set_style_bg_color(bat, lv_color_hex(c), 0);
			lv_label_set_text_fmt(bat, "%s%d%%", s.charging ? "⚡" : "", s.battery);   /* lightning: on the charger */
		}
	}
	if (s.wifi != shown.wifi) {
		show(wifi, s.wifi >= 0);
		lv_color_t lit = lv_color_hex(theme.bar_fg), dark = plain_bar() ? lv_color_hex(theme.line) : lv_color_mix(lit, lv_color_hex(theme.bar_bg), LV_OPA_30);
		for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(bars[i], i < s.wifi ? lit : dark, 0);
	}
	if (s.usb != shown.usb) show(usb, s.usb);
	shown = s;
}

static void minute_cb(lv_timer_t *t)
{
	statusbar_update();
	lv_timer_set_period(t, ms_to_next_minute());
}

int statusbar_height(void) { return theme.bar_h ? theme.bar_h + (theme.bar_line ? 1 : 0) : 0; }

void statusbar_create(lv_obj_t *scr)
{
	usb = wifi = clock_badge = bat = NULL;
	memset(&shown, 0xfe, sizeof(shown));   /* nothing shown yet: every value counts as new */
	shown_time[0] = 0;
	if (!minute) minute = lv_timer_create(minute_cb, ms_to_next_minute(), NULL);
	if (!theme.bar_h) return;

	int h = theme.bar_h, y = theme.bar_bottom ? SCREEN_H - h : 0;
	lv_obj_t *bar = lv_obj_create(scr);
	lv_obj_remove_style_all(bar);
	lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_pos(bar, 0, y);
	lv_obj_set_size(bar, SCREEN_W, h);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(bar, lv_color_hex(theme.bar_bg), 0);

	if (theme.title[0]) {
		lv_obj_t *name = ui_label(bar, ui_font(12, 1), theme.title_fg, theme.title);
		lv_obj_align(name, LV_ALIGN_LEFT_MID, 6, 0);
	}

	lv_obj_t *row = lv_obj_create(bar);
	lv_obj_remove_style_all(row);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(row, LV_SIZE_CONTENT, h - 2);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* START: a content-sized row only counts what lies right of its left edge */
	lv_obj_set_style_pad_column(row, 5, 0);
	lv_obj_align(row, LV_ALIGN_RIGHT_MID, -3, 0);

	uint32_t dim = plain_bar() ? theme.dim : lv_color_to_u32(lv_color_mix(lv_color_hex(theme.bar_fg), lv_color_hex(theme.bar_bg), LV_OPA_60)) & 0xffffff;
	usb = ui_label(row, ui_font(10, 1), dim, "USB");
	lv_obj_set_style_border_width(usb, 1, 0);
	lv_obj_set_style_border_color(usb, lv_color_hex(dim), 0);
	lv_obj_set_style_radius(usb, theme.badge_style == BADGE_ROUND ? 3 : 0, 0);
	lv_obj_set_style_pad_hor(usb, 2, 0);

	/* Wi-Fi: four bars, lit up to the signal strength; all dark = on but not connected */
	wifi = lv_obj_create(row);
	lv_obj_remove_style_all(wifi);
	lv_obj_set_size(wifi, 4 * 3 + 3, 13);
	for (int i = 0; i < 4; i++) {
		bars[i] = lv_obj_create(wifi);
		lv_obj_remove_style_all(bars[i]);
		lv_obj_set_size(bars[i], 3, 4 + i * 3);
		lv_obj_align(bars[i], LV_ALIGN_BOTTOM_LEFT, i * 4, 0);
		lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
		lv_obj_set_style_radius(bars[i], theme.badge_style == BADGE_ROUND ? 1 : 0, 0);
	}

	clock_badge = badge(row, theme.badge_bg, theme.badge_fg);
	bat = badge(row, 0x5fcf7a, 0x101418);

	if (theme.bar_line) {
		lv_obj_t *line = lv_obj_create(scr);
		lv_obj_remove_style_all(line);
		lv_obj_set_size(line, SCREEN_W, 1);
		lv_obj_set_pos(line, 0, theme.bar_bottom ? y - 1 : h);
		lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(line, lv_color_hex(theme.bar_line_color), 0);
	}
	statusbar_update();
}
