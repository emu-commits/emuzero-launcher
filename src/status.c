// SPDX-License-Identifier: MIT
#include "status.h"
#include "paths.h"
#include "sys.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void battery(struct status *s)
{
	char dir[512], p[512], v[32];
	s->battery = -1;
	s->charging = 0;
	DIR *d = opendir(path_join(dir, sizeof(dir), "/sys/class/power_supply"));
	if (!d) return;
	for (struct dirent *e; (e = readdir(d));) {
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", e->d_name);
		if (sys_read(p, v, sizeof(v)) || strcmp(v, "Battery")) continue;
		snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", e->d_name);
		long c = sys_read_long(p, -1);
		if (c < 0) continue;
		s->battery = c > 100 ? 100 : (int)c;
		snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", e->d_name);
		if (!sys_read(p, v, sizeof(v))) s->charging = !strcmp(v, "Charging") ? 1 : !strcmp(v, "Full") ? 2 : 0;
		break;
	}
	closedir(d);
}

static void wifi(struct status *s)
{
	char v[32], line[256], p[512];
	s->wifi = -1;
	if (sys_read("/sys/class/net/wlan0/flags", v, sizeof(v))) return;
	if (!(strtol(v, NULL, 16) & 1)) return;                   /* IFF_UP */
	s->wifi = 0;
	if (sys_read("/sys/class/net/wlan0/operstate", v, sizeof(v)) || strcmp(v, "up")) return;
	/* " wlan0: 0000   63.  -47.  -256 ...": link quality out of 70 */
	int q = 0;
	FILE *f = fopen(path_join(p, sizeof(p), "/proc/net/wireless"), "r");
	while (f && fgets(line, sizeof(line), f)) {
		char *w = strstr(line, "wlan0:");
		if (w) { unsigned st; float lq; if (sscanf(w + 6, "%x %f", &st, &lq) == 2) q = (int)lq; break; }
	}
	if (f) fclose(f);
	s->wifi = q >= 50 ? 4 : q >= 35 ? 3 : q >= 20 ? 2 : 1;
}

static void usb(struct status *s)
{
	char dir[512];
	s->usb = 0;
	DIR *d = opendir(path_join(dir, sizeof(dir), "/sys/bus/platform/drivers/dwc2"));
	if (!d) return;
	for (struct dirent *e; (e = readdir(d));) {
		size_t l = strlen(e->d_name);
		if (l > 4 && !strcmp(e->d_name + l - 4, ".usb")) { s->usb = 1; break; }   /* 3f980000.usb */
	}
	closedir(d);
}

void status_read(struct status *s)
{
	battery(s);
	wifi(s);
	usb(s);
}
