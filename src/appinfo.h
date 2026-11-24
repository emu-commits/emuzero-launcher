// SPDX-License-Identifier: MIT
// App info page over the home screen: name, version, package, size on disk; actions Back
// (selected), Update (only when the Store's index has a newer version) and Remove.
#pragma once
#include "lvgl/lvgl.h"
#include "apps.h"

/* open it on scr for app a; done(changed) runs when it closes, changed = an app was removed
 * or updated (the home screen reads the apps again) */
void appinfo_open(lv_obj_t *scr, const struct app *a, void (*done)(int changed));
/* while it is open, the home screen passes every key here */
int appinfo_active(void);
void appinfo_key(uint32_t key);
