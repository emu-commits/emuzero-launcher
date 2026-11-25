// SPDX-License-Identifier: MIT
// Device build: /dev/fb0 through LVGL's fbdev driver; keys through our own evdev reader, with
// every modifier. The loop is the launcher's measured one: the refresh timer is paused until
// something is invalidated, and the process sleeps in poll() until a key, a signal or a timer.
//
// The built-in keyboard (tca8418) does the Fn layer itself (Fn+F arrives as KEY_UP) and sends
// KEY_DVD while Fn is on and KEY_CD while Sym is on; on the Sym layer each symbol comes as a
// private key code (the pairs below, from the vendor keymap and the keys' printed legends).
// A USB keyboard is read with the ordinary US layout.
#define _GNU_SOURCE
#include "nplat.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_IN 16

static lv_display_t *disp;
static lv_timer_t *refr;
static int ino = -1, ttyfd = -1, kdtty = -1;
static struct termios tio0;
static struct { int fd; int internal; char name[16]; } in[MAX_IN];
static int nin;
static unsigned mods;
static int alt_alone;           /* Alt is down and nothing else was pressed since */
static void (*key_fn)(struct nkey);
static void (*mods_fn)(unsigned);

const char *nplat_name(void) { return "device"; }

uint32_t nplat_tick(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* NOTES_TIMING=1: key to finished frame, logged every 25 keys (stderr: /tmp/emuzero-app.log) */
static int timing;
static uint32_t key_at, t_n, t_sum, t_max;

static void on_invalidate(lv_event_t *e) { (void)e; lv_timer_resume(refr); }
static void on_refr_ready(lv_event_t *e)
{
	(void)e;
	lv_timer_pause(refr);
	if (timing && key_at) {
		uint32_t d = nplat_tick() - key_at;
		key_at = 0;
		t_sum += d; t_n++;
		if (d > t_max) t_max = d;
		if (t_n == 25) { fprintf(stderr, "emuzero-notes: key to frame avg %u ms, max %u ms (25 keys)\n", t_sum / t_n, t_max); t_n = t_sum = t_max = 0; }
	}
}

static void restore_tty(void)
{
	/* everything typed here also reached the console as text: drop it, or a shell that
	 * started us (Terminal) would run the note's lines as commands */
	if (ttyfd >= 0) { tcflush(ttyfd, TCIFLUSH); tcsetattr(ttyfd, TCSANOW, &tio0); }
	if (kdtty >= 0) ioctl(kdtty, KDSETMODE, KD_TEXT);
}

lv_display_t *nplat_display(const struct nplat_opts *o)
{
	(void)o;
	disp = lv_linux_fbdev_create();
	if (!disp) return NULL;
	lv_linux_fbdev_set_file(disp, "/dev/fb0");
	refr = lv_display_get_refr_timer(disp);
	timing = getenv("NOTES_TIMING") != NULL;
	lv_display_add_event_cb(disp, on_invalidate, LV_EVENT_INVALIDATE_AREA, NULL);
	lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);

	/* The console still turns keys into text and signals: Ctrl+C would send SIGINT, Ctrl+Z
	 * SIGTSTP and Ctrl+S would stop the console's output until Ctrl+Q. Those are editing keys
	 * here, so switch that off on our controlling terminal while we run. */
	ttyfd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (ttyfd >= 0 && !tcgetattr(ttyfd, &tio0)) {
		struct termios t = tio0;
		t.c_lflag &= ~(ISIG | ICANON | ECHO);
		t.c_iflag &= ~(IXON | IXOFF);
		tcsetattr(ttyfd, TCSANOW, &t);
	} else if (ttyfd >= 0) { close(ttyfd); ttyfd = -1; }
	/* started straight on the console (not from the launcher): graphics mode, no text showing through */
	int mode;
	if (isatty(0) && !ioctl(0, KDGETMODE, &mode) && mode == KD_TEXT) { kdtty = 0; ioctl(kdtty, KDSETMODE, KD_GRAPHICS); }
	atexit(restore_tty);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
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

static const char *only_kbd;

static void open_input(const char *name)
{
	char p[64], dev[128] = "";
	if (strncmp(name, "event", 5) || nin >= MAX_IN) return;
	for (int i = 0; i < nin; i++)
		if (!strcmp(in[i].name, name)) return;
	snprintf(p, sizeof(p), "/dev/input/%s", name);
	int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return;
	if (!is_keyboard(fd)) { close(fd); return; }
	ioctl(fd, EVIOCGNAME(sizeof(dev) - 1), dev);
	in[nin].fd = fd;
	in[nin].internal = strstr(dev, "tca8418") != NULL;
	snprintf(in[nin].name, sizeof(in[nin].name), "%s", name);
	nin++;
}

static void close_input(int i) { close(in[i].fd); in[i] = in[--nin]; }

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

/* the built-in keyboard's Sym layer: private key code -> symbol */
static const struct { unsigned short code; char ch; } sym_keys[] = {
	{ 26, '!' }, { 27, '@' }, { 39, '#' }, { 40, '$' }, { 41, '%' }, { 43, '^' }, { 51, '&' },
	{ 52, '*' }, { 53, '(' }, { 94, ')' }, { 55, '~' }, { 69, '`' }, { 70, '_' }, { 71, '-' },
	{ 72, '+' }, { 73, '=' }, { 74, '[' }, { 75, ']' }, { 76, '{' }, { 77, '}' }, { 79, ';' },
	{ 80, ':' }, { 81, '\'' }, { 82, '"' }, { 83, '<' }, { 85, '>' }, { 86, '\\' }, { 89, '|' },
	{ 90, ',' }, { 91, '.' }, { 92, '/' }, { 93, '?' },
};

/* US layout punctuation: code, plain, shifted */
static const struct { unsigned short code; char c, s; } us_punct[] = {
	{ KEY_MINUS, '-', '_' }, { KEY_EQUAL, '=', '+' }, { KEY_LEFTBRACE, '[', '{' }, { KEY_RIGHTBRACE, ']', '}' },
	{ KEY_SEMICOLON, ';', ':' }, { KEY_APOSTROPHE, '\'', '"' }, { KEY_GRAVE, '`', '~' }, { KEY_BACKSLASH, '\\', '|' },
	{ KEY_COMMA, ',', '<' }, { KEY_DOT, '.', '>' }, { KEY_SLASH, '/', '?' }, { KEY_102ND, '\\', '|' },
	{ KEY_KPMINUS, '-', '-' }, { KEY_KPPLUS, '+', '+' }, { KEY_KPASTERISK, '*', '*' }, { KEY_KPSLASH, '/', '/' },
	{ KEY_KPDOT, '.', '.' },
};

static uint32_t map_key(int code, int internal, int shift)
{
	static const char row1[] = "qwertyuiop", row2[] = "asdfghjkl", row3[] = "zxcvbnm";
	static const char num[] = "1234567890", num_s[] = "!@#$%^&*()";
	switch (code) {
	case KEY_UP: return NK_UP;
	case KEY_DOWN: return NK_DOWN;
	case KEY_LEFT: return NK_LEFT;
	case KEY_RIGHT: return NK_RIGHT;
	case KEY_HOME: return NK_HOME;
	case KEY_END: return NK_END;
	case KEY_PAGEUP: return NK_PGUP;
	case KEY_PAGEDOWN: return NK_PGDN;
	case KEY_ENTER: case KEY_KPENTER: return NK_ENTER;
	case KEY_ESC: return NK_ESC;
	case KEY_BACKSPACE: return NK_BS;
	case KEY_DELETE: return NK_DEL;
	case KEY_TAB: return NK_TAB;
	case KEY_HELP: return NK_HELP;
	case KEY_INSERT: return NK_INSERT;
	case KEY_SPACE: return ' ';
	}
	if (code >= KEY_F1 && code <= KEY_F10) return NK_F1 + (uint32_t)(code - KEY_F1);
	if (internal)
		for (size_t i = 0; i < sizeof(sym_keys) / sizeof(sym_keys[0]); i++)
			if (sym_keys[i].code == code) return (uint32_t)(unsigned char)sym_keys[i].ch;
	char c = 0;
	if (code >= KEY_Q && code <= KEY_P) c = row1[code - KEY_Q];
	else if (code >= KEY_A && code <= KEY_L) c = row2[code - KEY_A];
	else if (code >= KEY_Z && code <= KEY_M) c = row3[code - KEY_Z];
	else if (code >= KEY_1 && code <= KEY_0) c = (shift && !internal) ? num_s[code - KEY_1] : num[code - KEY_1];
	else if (!internal)
		for (size_t i = 0; i < sizeof(us_punct) / sizeof(us_punct[0]); i++)
			if (us_punct[i].code == code) { c = shift ? us_punct[i].s : us_punct[i].c; break; }
	if (c >= 'a' && c <= 'z' && shift) c -= 32;
	return (uint32_t)(unsigned char)c;
}

static void set_mod(unsigned m, int on)
{
	unsigned old = mods;
	if (on) mods |= m; else mods &= ~m;
	if (mods != old && mods_fn) mods_fn(mods);
}

static void on_key(int internal, int code, int value)
{
	switch (code) {
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: set_mod(M_SHIFT, value != 0); return;
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL: set_mod(M_CTRL, value != 0); alt_alone = 0; return;
	case KEY_LEFTALT: case KEY_RIGHTALT:
		if (value == 1) alt_alone = 1;
		else if (value == 0 && alt_alone) { alt_alone = 0; set_mod(M_ALT, 0); key_fn((struct nkey){ NK_ALT_TAP, mods }); return; }
		set_mod(M_ALT, value != 0);
		return;
	case KEY_DVD: set_mod(M_FN, value != 0); return;
	case KEY_CD: set_mod(M_SYM, value != 0); return;
	}
	if (!value) return;          /* presses and auto-repeats only */
	alt_alone = 0;
	uint32_t k = map_key(code, internal, (mods & M_SHIFT) != 0);
	if (!k) return;
	struct nkey e = { k, mods };
	if (timing && !key_at) key_at = nplat_tick();
	/* chords carry the plain lower-case letter */
	if ((mods & (M_CTRL | M_ALT)) && k >= 'A' && k <= 'Z') e.k = k + 32;
	key_fn(e);
}

static void read_input(int i)
{
	struct input_event ev[32];
	for (;;) {
		ssize_t n = read(in[i].fd, ev, sizeof(ev));
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && errno == ENODEV) { close_input(i); return; }
		if (n <= 0) return;
		for (size_t k = 0; k < (size_t)n / sizeof(ev[0]); k++)
			if (ev[k].type == EV_KEY) on_key(in[i].internal, ev[k].code, ev[k].value);
	}
}

void nplat_keys(const struct nplat_opts *o, void (*key_cb)(struct nkey), void (*mods_cb)(unsigned))
{
	key_fn = key_cb;
	mods_fn = mods_cb;
	only_kbd = o->kbd;
	if (only_kbd) {
		const char *b = strrchr(only_kbd, '/');
		char real[64];
		ssize_t n = readlink(only_kbd, real, sizeof(real) - 1);
		if (n > 0) { real[n] = 0; b = strrchr(real, '/'); open_input(b ? b + 1 : real); }
		else open_input(b ? b + 1 : only_kbd);
	} else {
		ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (ino >= 0) inotify_add_watch(ino, "/dev/input", IN_CREATE | IN_ATTRIB);
		DIR *d = opendir("/dev/input");
		for (struct dirent *e; d && (e = readdir(d));) open_input(e->d_name);
		if (d) closedir(d);
	}
	/* keys held as we start (the launcher's Enter): their releases must not count as presses;
	 * on_key only acts on presses, and the modifiers start from what is really down */
	for (int i = 0; i < nin; i++) {
		unsigned long st[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
		if (ioctl(in[i].fd, EVIOCGKEY(sizeof(st)), st) < 0) continue;
#define DOWN(k) (st[(k) / (8 * sizeof(long))] >> ((k) % (8 * sizeof(long))) & 1)
		if (DOWN(KEY_LEFTSHIFT) || DOWN(KEY_RIGHTSHIFT)) mods |= M_SHIFT;
		if (DOWN(KEY_LEFTCTRL) || DOWN(KEY_RIGHTCTRL)) mods |= M_CTRL;
		if (DOWN(KEY_DVD)) mods |= M_FN;
		if (DOWN(KEY_CD)) mods |= M_SYM;
#undef DOWN
	}
}

int nplat_run(const struct nplat_opts *o, int (*should_quit)(void))
{
	(void)o;
	lv_refr_now(disp);
	while (!should_quit()) {
		uint32_t d = lv_timer_handler();
		if (should_quit()) break;
		struct pollfd p[1 + MAX_IN];
		p[0] = (struct pollfd){ ino, POLLIN, 0 };
		for (int i = 0; i < nin; i++) p[1 + i] = (struct pollfd){ in[i].fd, POLLIN, 0 };
		int had = nin;
		if (poll(p, 1 + nin, d == LV_NO_TIMER_READY ? -1 : (int)d) <= 0) continue;   /* a timer, or a signal */
		if (p[0].revents) watch_inputs();
		for (int i = had - 1; i >= 0; i--)
			if (p[1 + i].revents) read_input(i);
	}
	return 0;
}
