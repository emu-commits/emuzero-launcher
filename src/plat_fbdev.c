// SPDX-License-Identifier: MIT
// Device build: /dev/fb0 through LVGL's fbdev driver; the keys through our own evdev reader.
//
// The loop is the one measured in spike/ ("tuned"): the display refresh timer is paused until
// something is invalidated, and the process sleeps in poll() until a key arrives or a timer
// (the clock) is due.
//
// Keys: every input device that has keyboard keys is read (the built-in keyboard, a USB
// keyboard plugged in later: /dev/input is watched with inotify). LVGL's own evdev driver
// drops letter keys; this reader passes them on as characters.
//
// Apps: plat_exec() hands the screen and keyboard to an app and waits for it. While it runs,
// the launcher still sees every key (no grab), so it can apply the screen timeout and the
// stop chord; when the screen goes dark the keys are grabbed until the waking key is let go,
// so the app never sees that key.
#define _GNU_SOURCE
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_IN 16
#define QLEN 64

static lv_display_t *disp;
static lv_indev_t *kb;
static lv_timer_t *refr;
static int fbfd = -1;          /* our own descriptor, for blanking and panning */
static int tty = -1;           /* the text console we run on (fd 0), or -1 */
static int ino = -1;           /* inotify on /dev/input */
static int chld[2] = { -1, -1 };
static const char *only_kbd;
static struct { int fd; char name[16]; } in[MAX_IN];
static int nin;
static struct { uint32_t key; uint8_t pressed; } q[QLEN];
static int qhead, qlen;
static uint32_t held;          /* the last key LVGL was given */
static uint32_t key_ms;        /* when the last key was pressed while an app runs */
static int shift;

uint32_t plat_exec_end;

const char *plat_name(void) { return "device"; }

static uint32_t now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* ---- the screen ---- */

static void on_invalidate(lv_event_t *e) { (void)e; lv_timer_resume(refr); }
static void on_refr_ready(lv_event_t *e) { (void)e; lv_timer_pause(refr); }

static void console_text(void) { if (tty >= 0) ioctl(tty, KDSETMODE, KD_TEXT); }

static void on_term(int sig)
{
	console_text();   /* leave the console readable for what follows (login prompt, message) */
	signal(sig, SIG_DFL);
	raise(sig);
}

/* backlight and panel: off = backlight off, then the display blanked so the panel driver
 * puts the controller to sleep; on = the reverse */
static void backlight(int on)
{
	DIR *d = opendir("/sys/class/backlight");
	for (struct dirent *e; d && (e = readdir(d));) {
		char p[300];
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof(p), "/sys/class/backlight/%s/bl_power", e->d_name);
		int fd = open(p, O_WRONLY | O_CLOEXEC);
		if (fd >= 0) { if (write(fd, on ? "0\n" : "4\n", 2) < 0) {} close(fd); }
	}
	if (d) closedir(d);
}

/* is a backlight off (bl_power not 0)? then the launcher before us left the screen dark */
static int backlight_off(void)
{
	int off = 0;
	DIR *d = opendir("/sys/class/backlight");
	for (struct dirent *e; d && (e = readdir(d));) {
		char p[300], v[8] = "";
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof(p), "/sys/class/backlight/%s/bl_power", e->d_name);
		int fd = open(p, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) { if (read(fd, v, sizeof(v) - 1) > 0 && v[0] != '0') off = 1; close(fd); }
	}
	if (d) closedir(d);
	return off;
}

static void screen_power(int on)
{
	if (on && fbfd >= 0) ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK);
	backlight(on);
	if (!on && fbfd >= 0) ioctl(fbfd, FBIOBLANK, FB_BLANK_POWERDOWN);
	if (on) lv_obj_invalidate(lv_screen_active());   /* the panel woke up blank: draw it all */
}

lv_display_t *plat_display(const struct plat_opts *o)
{
	(void)o;
	disp = lv_linux_fbdev_create();
	if (!disp) return NULL;
	lv_linux_fbdev_set_file(disp, "/dev/fb0");
	fbfd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	/* the launcher before us may have left it dark; unblanking a lit panel costs a frame or
	 * two (16-25 ms: the driver commits the display again), so only when it is dark */
	if (backlight_off()) {
		if (fbfd >= 0) ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK);
		backlight(1);
	}
	refr = lv_display_get_refr_timer(disp);
	lv_display_add_event_cb(disp, on_invalidate, LV_EVENT_INVALIDATE_AREA, NULL);
	lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);

	/* our messages go to a log, not onto the console that terminal apps use */
	if (isatty(2)) {
		int log = open("/tmp/emuzero-launcher.log", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
		if (log >= 0) { dup2(log, 2); close(log); }
	}
	/* on the text console: graphics mode, so no console text or cursor shows through */
	int mode;
	if (isatty(0) && !ioctl(0, KDGETMODE, &mode)) {
		tty = 0;
		ioctl(tty, KDSETMODE, KD_GRAPHICS);
		signal(SIGTERM, on_term);
		signal(SIGHUP, on_term);
		signal(SIGINT, on_term);
		/* a crash too: nz-console then shows why another launcher starts */
		signal(SIGSEGV, on_term);
		signal(SIGBUS, on_term);
		signal(SIGABRT, on_term);
		signal(SIGFPE, on_term);
		signal(SIGILL, on_term);
		atexit(console_text);
	}
	signal(SIGTTOU, SIG_IGN);   /* we hand the terminal to apps and take it back */
	return disp;
}

/* ---- keys ---- */

static int is_keyboard(int fd)
{
	unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return 0;
#define HAS(k) (bits[(k) / (8 * sizeof(long))] >> ((k) % (8 * sizeof(long))) & 1)
	return HAS(KEY_ENTER) || HAS(KEY_A);
#undef HAS
}

static void open_input(const char *name)
{
	char p[64];
	if (strncmp(name, "event", 5) || nin >= MAX_IN) return;
	for (int i = 0; i < nin; i++)
		if (!strcmp(in[i].name, name)) return;
	snprintf(p, sizeof(p), "/dev/input/%s", name);
	int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return;   /* not readable yet: udev sets the permissions a moment later (IN_ATTRIB) */
	if (!is_keyboard(fd)) { close(fd); return; }
	in[nin].fd = fd;
	snprintf(in[nin].name, sizeof(in[nin].name), "%s", name);
	nin++;
}

static void close_input(int i)
{
	close(in[i].fd);
	in[i] = in[--nin];
}

static void grab(int on)
{
	for (int i = 0; i < nin; i++) ioctl(in[i].fd, EVIOCGRAB, on);
}

/* new devices under /dev/input */
static void watch_inputs(void)
{
	char buf[1024] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	while ((n = read(ino, buf, sizeof(buf))) > 0)
		for (char *p = buf; p < buf + n;) {
			struct inotify_event *e = (struct inotify_event *)p;
			if (e->len) open_input(e->name);
			p += sizeof(*e) + e->len;
		}
}

/* Linux key code -> LVGL key: arrows and editing keys, letters and digits as characters */
static uint32_t map_key(int code)
{
	static const char row1[] = "qwertyuiop", row2[] = "asdfghjkl", row3[] = "zxcvbnm", num[] = "1234567890";
	switch (code) {
	case KEY_UP: return LV_KEY_UP;
	case KEY_DOWN: return LV_KEY_DOWN;
	case KEY_LEFT: return LV_KEY_LEFT;
	case KEY_RIGHT: return LV_KEY_RIGHT;
	case KEY_ENTER: case KEY_KPENTER: return LV_KEY_ENTER;
	case KEY_ESC: return LV_KEY_ESC;
	case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
	case KEY_DELETE: return LV_KEY_DEL;
	case KEY_TAB: return shift ? LV_KEY_PREV : LV_KEY_NEXT;
	case KEY_HOME: return LV_KEY_HOME;
	case KEY_END: return LV_KEY_END;
	case KEY_SPACE: return ' ';
	case KEY_MINUS: return '-';
	case KEY_COMMA: return ',';
	case KEY_DOT: return '.';
	case KEY_SLASH: return '/';
	case KEY_SEMICOLON: return ';';
	}
	char c = 0;
	if (code >= KEY_Q && code <= KEY_P) c = row1[code - KEY_Q];
	else if (code >= KEY_A && code <= KEY_L) c = row2[code - KEY_A];
	else if (code >= KEY_Z && code <= KEY_M) c = row3[code - KEY_Z];
	else if (code >= KEY_1 && code <= KEY_0) c = num[code - KEY_1];
	if (c >= 'a' && c <= 'z' && shift) c -= 32;
	return (uint32_t)(unsigned char)c;
}

static void push(uint32_t key, int pressed)
{
	if (!key || qlen == QLEN) return;
	q[(qhead + qlen++) % QLEN].key = key;
	q[(qhead + qlen - 1) % QLEN].pressed = (uint8_t)pressed;
}

/* read device i; key events go to fn(code, value) */
static void read_input(int i, void (*fn)(int code, int value))
{
	struct input_event ev[32];
	for (;;) {
		ssize_t n = read(in[i].fd, ev, sizeof(ev));
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && errno == ENODEV) { close_input(i); return; }   /* unplugged */
		if (n <= 0) return;
		for (size_t k = 0; k < (size_t)n / sizeof(ev[0]); k++)
			if (ev[k].type == EV_KEY) fn(ev[k].code, ev[k].value);
	}
}

/* home screen: presses, releases and auto-repeats into LVGL's queue */
static void to_queue(int code, int value)
{
	if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { shift = value != 0; return; }
	uint32_t k = map_key(code);
	if (!k) return;
	if (value == 2) push(k, 0);   /* auto-repeat: one more press */
	push(k, value != 0);
	if (value) held = k;
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *d)
{
	(void)indev;
	if (!qlen) { d->state = LV_INDEV_STATE_RELEASED; d->key = held; return; }
	d->key = q[qhead].key;
	d->state = q[qhead].pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	qhead = (qhead + 1) % QLEN;
	qlen--;
}

lv_indev_t *plat_keypad(const struct plat_opts *o)
{
	only_kbd = o->kbd;
	if (only_kbd) {
		const char *b = strrchr(only_kbd, '/');
		char real[64];
		ssize_t n = readlink(only_kbd, real, sizeof(real) - 1);   /* by-path links point at eventN */
		if (n > 0) { real[n] = 0; b = strrchr(real, '/'); open_input(b ? b + 1 : real); }
		else open_input(b ? b + 1 : only_kbd);
	} else {
		ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_ATTRIB);
		DIR *d = opendir("/dev/input");
		for (struct dirent *e; d && (e = readdir(d));) open_input(e->d_name);
		if (d) closedir(d);
	}
	kb = lv_indev_create();
	lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
	lv_indev_set_read_cb(kb, read_cb);
	lv_indev_set_mode(kb, LV_INDEV_MODE_EVENT);
	return kb;
}

/* pollfds: [0] inotify, [1] child pipe, then the inputs */
static int fill_poll(struct pollfd *p)
{
	p[0] = (struct pollfd){ ino, POLLIN, 0 };
	p[1] = (struct pollfd){ chld[0], POLLIN, 0 };
	for (int i = 0; i < nin; i++) p[2 + i] = (struct pollfd){ in[i].fd, POLLIN, 0 };
	return 2 + nin;
}

/* poll, then read whatever is ready; key events go to fn */
static void wait_keys(int timeout, void (*fn)(int code, int value))
{
	struct pollfd p[2 + MAX_IN];
	int n = fill_poll(p), had = nin;
	if (poll(p, n, timeout) <= 0) return;
	if (p[0].revents) watch_inputs();
	if (p[1].revents) { char b[16]; while (read(chld[0], b, sizeof(b)) > 0) {} }
	for (int i = had - 1; i >= 0; i--)   /* backwards: read_input may drop an unplugged device */
		if (p[2 + i].revents) read_input(i, fn);
}

void plat_quit(void) {}

int plat_run(const struct plat_opts *o)
{
	(void)o;
	lv_refr_now(disp);
	for (;;) {
		uint32_t d = lv_timer_handler();
		wait_keys(d == LV_NO_TIMER_READY ? -1 : (int)d, to_queue);
		while (qlen) lv_indev_read(kb);
	}
	return 0;
}

/* ---- the screen goes dark at home ---- */

static enum { LIT, DARK, WAKING } scr = LIT;
static int wake_code;

/* dark: the first press switches the screen on; we are awake once that key is let go */
static void waking(int code, int value)
{
	if (scr == DARK && value == 1) { scr = WAKING; wake_code = code; screen_power(1); }
	else if (scr == WAKING && code == wake_code && value == 0) scr = LIT;
}

void plat_sleep(void)
{
	screen_power(0);
	scr = DARK;
	while (scr != LIT) wait_keys(-1, waking);
	shift = 0;
	qlen = 0;
}

/* ---- apps ---- */

static void on_child(int sig)
{
	(void)sig;
	int e = errno;
	if (write(chld[1], "", 1) < 0) {}
	errno = e;
}

static int ctrl, alt, stop_chord;
static void in_app(int code, int value)
{
	if (scr != LIT) {
		waking(code, value);
		if (scr == LIT) key_ms = now_ms();   /* the idle time starts again from the wake-up */
		return;
	}
	if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) ctrl = value != 0;
	if (code == KEY_LEFTALT || code == KEY_RIGHTALT) alt = value != 0;
	if (value == 1) { key_ms = now_ms(); if (code == KEY_BACKSPACE && ctrl && alt) stop_chord = 1; }
}

int plat_exec(const struct plat_app *a)
{
	if (chld[0] < 0) {
		if (pipe2(chld, O_NONBLOCK | O_CLOEXEC)) return -1;
		struct sigaction sa = { .sa_handler = on_child, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
		sigaction(SIGCHLD, &sa, NULL);
	}
	if (a->terminal) {   /* the text console, empty, with its cursor */
		console_text();
		if (write(1, "\033[2J\033[H\033[?25h", 13) < 0) {}
	}
	pid_t pid = fork();
	if (pid < 0) { if (tty >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS); return -1; }
	if (pid == 0) {
		setpgid(0, 0);
		if (tty >= 0) tcsetpgrp(tty, getpid());   /* every app: graphical ones set up the console too */
		signal(SIGTTOU, SIG_DFL); signal(SIGCHLD, SIG_DFL);
		signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL); signal(SIGINT, SIG_DFL);
		if (a->terminal) dup2(1, 2);   /* our stderr is the log; the app's goes to the console */
		else {                /* a graphical app's output goes to a log, not over its picture */
			int log = open("/tmp/emuzero-app.log", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
			int nul = open("/dev/null", O_RDONLY);
			if (log >= 0) { dup2(log, 1); dup2(log, 2); }
			if (nul >= 0) dup2(nul, 0);
		}
		closefrom(3);
		if (a->dir && a->dir[0] && chdir(a->dir)) {}
		execl("/bin/sh", "sh", "-c", a->exec, (char *)NULL);
		_exit(127);
	}
	setpgid(pid, pid);
	if (tty >= 0) tcsetpgrp(tty, pid);

	int st = 0;
	uint32_t stopped = 0;
	key_ms = now_ms();
	ctrl = alt = stop_chord = 0;
	scr = LIT;
	for (;;) {
		pid_t r = waitpid(pid, &st, WNOHANG);
		if (r == pid || (r < 0 && errno != EINTR)) break;
		uint32_t t = now_ms();
		if (stop_chord && !stopped) { kill(-pid, SIGTERM); stopped = t ? t : 1; }
		if (stopped && t - stopped >= 2000) kill(-pid, SIGKILL);
		if (scr == LIT && a->idle_s > 0 && t - key_ms >= (uint32_t)a->idle_s * 1000) {
			scr = DARK; ctrl = alt = 0;
			screen_power(0);
		}
		if (scr != LIT) grab(1);   /* again each time: a keyboard may have been plugged in */
		else grab(0);
		int timeout = -1;
		if (scr == LIT && a->idle_s > 0) timeout = (int)(key_ms + (uint32_t)a->idle_s * 1000 - t);
		if (stopped) timeout = timeout < 0 || timeout > 250 ? 250 : timeout;
		wait_keys(timeout < 0 ? -1 : timeout, in_app);
	}
	plat_exec_end = lv_tick_get();
	if (scr != LIT) { grab(0); screen_power(1); scr = LIT; }
	if (tty >= 0) { tcsetpgrp(tty, getpgrp()); tcflush(tty, TCIFLUSH); }   /* keys typed in the app reached the tty too */
	if (a->terminal && write(1, "\033[2J\033[H\033[?25l", 13) < 0) {}
	if (tty >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS);

	/* an app may have panned the framebuffer; LVGL draws at the top */
	struct fb_var_screeninfo v;
	if (fbfd >= 0 && !ioctl(fbfd, FBIOGET_VSCREENINFO, &v) && (v.xoffset || v.yoffset)) {
		v.xoffset = v.yoffset = 0;
		ioctl(fbfd, FBIOPAN_DISPLAY, &v);
	}
	/* LVGL saw the press that started the app; give it the release, drop what came after */
	qlen = 0;
	shift = 0;
	push(held, 0);
	lv_obj_invalidate(lv_screen_active());
	return WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1;
}
