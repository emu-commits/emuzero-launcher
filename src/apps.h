// SPDX-License-Identifier: MIT
// The installed apps, read from desktop entry files (freedesktop.org Desktop Entry format).
// M5's apps (APPLaunch) and NimbleZero's apps are listed together, sorted by name.
#pragma once

#define APPS_MAX 128

struct app {
	char id[64];        /* file name without .desktop; the same id in two places is listed once */
	char name[64];
	char comment[96];
	char exec[256];     /* Exec= without its %f/%u field codes; run with /bin/sh -c */
	char path[128];     /* Path=: working directory, or "" */
	char file[320];     /* the entry file on the device (without the root prefix) */
	char icon[512];     /* readable icon file (with the root prefix), or "" for a drawn tile */
	int terminal;       /* Terminal=true: runs on the text console */
};

extern struct app apps[APPS_MAX];
extern int napps;

/* (re)read every app directory; returns the number of apps */
int apps_scan(void);
/* index of the app with this id, or -1 */
int apps_find(const char *id);
