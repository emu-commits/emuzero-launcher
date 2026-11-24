// SPDX-License-Identifier: MIT
// Device build: /dev/fb0 through LVGL's fbdev driver, the keyboard through evdev.
// The loop is the one measured in spike/ ("tuned"): input in event mode with poll() on the
// keyboard, and the display refresh timer paused until something is invalidated, so the
// process sleeps until a key arrives or a timer (the clock) is due.
#include "platform.h"
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

static lv_display_t *disp;
static lv_indev_t *kb;
static lv_timer_t *refr;
static int kfd = -1;

const char *plat_name(void) { return "device"; }

static void on_invalidate(lv_event_t *e) { (void)e; lv_timer_resume(refr); }
static void on_refr_ready(lv_event_t *e) { (void)e; lv_timer_pause(refr); }

lv_display_t *plat_display(const struct plat_opts *o)
{
	(void)o;
	disp = lv_linux_fbdev_create();
	if (!disp) return NULL;
	lv_linux_fbdev_set_file(disp, "/dev/fb0");
	refr = lv_display_get_refr_timer(disp);
	lv_display_add_event_cb(disp, on_invalidate, LV_EVENT_INVALIDATE_AREA, NULL);
	lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
	return disp;
}

lv_indev_t *plat_keypad(const struct plat_opts *o)
{
	kb = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, o->kbd);
	if (!kb) return NULL;
	lv_indev_set_mode(kb, LV_INDEV_MODE_EVENT);
	kfd = open(o->kbd, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	return kb;
}

void plat_quit(void) {}

int plat_run(const struct plat_opts *o)
{
	(void)o;
	lv_refr_now(disp);
	for (;;) {
		uint32_t d = lv_timer_handler();
		struct pollfd p = { kfd, POLLIN, 0 };
		if (poll(&p, kfd >= 0, d == LV_NO_TIMER_READY ? -1 : (int)d) > 0 && kb) lv_indev_read(kb);
	}
	return 0;
}
