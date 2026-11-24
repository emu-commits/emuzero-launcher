// SPDX-License-Identifier: MIT
// Shared by both renderer spikes: app list from .desktop files, timing, and the numbers we compare.
#pragma once
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

#define MAX_APPS 64
struct app { char name[64]; char icon[256]; char exec[256]; };
static struct app apps[MAX_APPS];
static int napps;

static int name_cmp(const void *a, const void *b) { return strcasecmp(((const struct app *)a)->name, ((const struct app *)b)->name); }

/* Desktop entries the launcher shows: Name, Icon (relative paths are under /usr/share/APPLaunch), Exec. */
static void scan_dir(const char *dir)
{
	DIR *d = opendir(dir); struct dirent *e;
	if (!d) return;
	while ((e = readdir(d)) && napps < MAX_APPS) {
		size_t l = strlen(e->d_name); char p[512], line[512]; FILE *f; struct app a = { 0 }; int hidden = 0;
		if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		if (!(f = fopen(p, "r"))) continue;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (!strncmp(line, "Name=", 5)) snprintf(a.name, sizeof(a.name), "%s", line + 5);
			else if (!strncmp(line, "Exec=", 5)) snprintf(a.exec, sizeof(a.exec), "%s", line + 5);
			else if (!strncmp(line, "Icon=", 5)) {
				if (line[5] == '/') snprintf(a.icon, sizeof(a.icon), "%s", line + 5);
				else snprintf(a.icon, sizeof(a.icon), "/usr/share/APPLaunch/%s", line + 5);
			} else if (!strcmp(line, "Hidden=true") || !strcmp(line, "NoDisplay=true")) hidden = 1;
			else if (!strncmp(line, "OnlyShowIn=", 11) && !strstr(line, "emuZero")) hidden = 1;
		}
		fclose(f);
		if (a.icon[0] && access(a.icon, R_OK)) a.icon[0] = 0;
		if (!hidden && a.name[0] && a.exec[0]) apps[napps++] = a;
	}
	closedir(d);
}

static void scan_apps(void)
{
	scan_dir("/usr/share/APPLaunch/applications");
	scan_dir("/usr/share/applications");
	qsort(apps, napps, sizeof(apps[0]), name_cmp);
}

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

/* Time since the wrapper (tstart) exec'd us: T0_NS is its CLOCK_MONOTONIC just before execve. */
static double since_exec_ms(void) { const char *s = getenv("T0_NS"); return s ? now_ms() - atof(s) / 1e6 : -1; }

static int vctx(void)
{
	FILE *f = fopen("/proc/self/status", "r"); char l[128]; int v = 0, n = 0;
	if (!f) return -1;
	while (fgets(l, sizeof(l), f)) { if (sscanf(l, "voluntary_ctxt_switches: %d", &n) == 1) v += n; if (sscanf(l, "nonvoluntary_ctxt_switches: %d", &n) == 1) v += n; }
	fclose(f); return v;
}

static double cpu_ms(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_utime.tv_sec * 1e3 + r.ru_utime.tv_usec / 1e3 + r.ru_stime.tv_sec * 1e3 + r.ru_stime.tv_usec / 1e3; }
static long maxrss_kb(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_maxrss; }

/* Idle measurement window: cpu and wake-ups between idle_begin() and idle_end(). */
static double idle_cpu0, idle_t0; static int idle_ctx0;
static void idle_begin(void) { idle_cpu0 = cpu_ms(); idle_ctx0 = vctx(); idle_t0 = now_ms(); }
static void idle_end(const char *who)
{
	double secs = (now_ms() - idle_t0) / 1e3;
	printf("%s idle: %.1f s, cpu %.1f ms (%.3f %%), wakeups %.1f/s, maxrss %ld KB\n", who, secs, cpu_ms() - idle_cpu0,
	       (cpu_ms() - idle_cpu0) / (secs * 10), (vctx() - idle_ctx0) / secs, maxrss_kb());
}

/* Status bar text */
static void clock_text(char *buf, size_t n) { time_t t = time(NULL); strftime(buf, n, "%H:%M", localtime(&t)); }
static int battery_pct(void) { FILE *f = fopen("/sys/class/power_supply/bq27220-0/capacity", "r"); int c = -1; if (f) { if (fscanf(f, "%d", &c) != 1) c = -1; fclose(f); } return c; }
/* ms until the next whole minute (when the clock text changes) */
static int ms_to_next_minute(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (int)(60000 - (t.tv_sec % 60) * 1000 - t.tv_nsec / 1000000) + 5; }
