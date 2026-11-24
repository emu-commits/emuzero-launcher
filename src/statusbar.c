// SPDX-License-Identifier: MIT
// Updates come at each new minute (with the clock), and whenever the home screen asks: on a
// key, after an app, after the screen wakes. Nothing polls in between, so the launcher keeps
// sleeping while the device sits idle; a value that did not change is not redrawn.
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

static lv_obj_t *row, *usb, *wifi, *bars[4], *clock_badge, *bat;
static struct status shown = { -2, -2, -2, -2 };
static char shown_time[8];

static lv_obj_t *badge(lv_obj_t *parent, uint32_t fill, uint32_t text)
{
	lv_obj_t *l = ui_label(parent, ui_font(11, 1), text, "");
	lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(l, lv_color_hex(fill), 0);
	lv_obj_set_style_radius(l, 4, 0);
	lv_obj_set_style_pad_hor(l, 4, 0);
	lv_obj_set_style_pad_ver(l, 1, 0);
	return l;
}

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
			lv_obj_set_style_bg_color(bat, lv_color_hex(c), 0);
			lv_label_set_text_fmt(bat, "%s%d%%", s.charging ? "⚡" : "", s.battery);   /* lightning: on the charger */
		}
	}
	if (s.wifi != shown.wifi) {
		show(wifi, s.wifi >= 0);
		for (int i = 0; i < 4; i++)
			lv_obj_set_style_bg_color(bars[i], lv_color_hex(i < s.wifi ? theme.fg : theme.line), 0);
	}
	if (s.usb != shown.usb) show(usb, s.usb);
	shown = s;
}

static void minute_cb(lv_timer_t *t)
{
	statusbar_update();
	lv_timer_set_period(t, ms_to_next_minute());
}

void statusbar_create(lv_obj_t *scr)
{
	lv_obj_t *name = ui_label(scr, ui_font(12, 1), theme.accent, "emuZero");
	lv_obj_set_pos(name, 6, 2);

	row = lv_obj_create(scr);
	lv_obj_remove_style_all(row);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(row, LV_SIZE_CONTENT, BAR_H - 2);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* START: a content-sized row only counts what lies right of its left edge */
	lv_obj_set_style_pad_column(row, 5, 0);
	lv_obj_align(row, LV_ALIGN_TOP_RIGHT, -3, 1);

	usb = ui_label(row, ui_font(10, 1), theme.dim, "USB");
	lv_obj_set_style_border_width(usb, 1, 0);
	lv_obj_set_style_border_color(usb, lv_color_hex(theme.dim), 0);
	lv_obj_set_style_radius(usb, 3, 0);
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
		lv_obj_set_style_radius(bars[i], 1, 0);
	}

	clock_badge = badge(row, theme.line, theme.fg);
	bat = badge(row, 0x5fcf7a, 0x101418);

	lv_obj_t *line = lv_obj_create(scr);
	lv_obj_remove_style_all(line);
	lv_obj_set_size(line, SCREEN_W, 1);
	lv_obj_set_pos(line, 0, BAR_H);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(line, lv_color_hex(theme.line), 0);

	statusbar_update();
	lv_timer_create(minute_cb, ms_to_next_minute(), NULL);
}
