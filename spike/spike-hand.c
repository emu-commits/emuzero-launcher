// SPDX-License-Identifier: MIT
// Renderer spike B: a small hand-written renderer. RGB565 back buffer copied to /dev/fb0,
// FreeType for text, libpng for icons (scaled once at load), poll() on the keyboard with a
// timeout that ends when the clock text next changes.
//
//   spike-hand [--kbd /dev/input/eventN] [--idle SECS] [--bench N]
#include "common.h"
#include <fcntl.h>
#include <poll.h>
#include <png.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define W 320
#define H 170
static uint16_t back[W * H];
static uint8_t *fbmem; static int fb_stride;

static inline uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f); }
static inline void blend(int x, int y, uint32_t c, int a)
{
	if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
	uint16_t *p = &back[y * W + x];
	if (a >= 255) { *p = rgb(c); return; }
	int r0 = (*p >> 11) << 3, g0 = ((*p >> 5) & 63) << 2, b0 = (*p & 31) << 3;
	int r = (r0 * (255 - a) + ((c >> 16) & 255) * a) / 255, g = (g0 * (255 - a) + ((c >> 8) & 255) * a) / 255, b = (b0 * (255 - a) + (c & 255) * a) / 255;
	*p = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
static void fill(int x0, int y0, int w, int h, uint32_t c) { uint16_t v = rgb(c); for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) if (x >= 0 && y >= 0 && x < W && y < H) back[y * W + x] = v; }
static void round_rect(int x0, int y0, int w, int h, int r, uint32_t c, int alpha)
{
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		int dx = x < r ? r - x : x >= w - r ? x - (w - r - 1) : 0, dy = y < r ? r - y : y >= h - r ? y - (h - r - 1) : 0;
		if (dx * dx + dy * dy <= r * r) blend(x0 + x, y0 + y, c, alpha);
	}
}

/* ---- text ---- */
static FT_Library ft; static FT_Face face, bold;
struct glyph { int w, h, left, top, adv; uint8_t *bmp; };
static struct glyph cache[2][4][96]; static const int sizes[4] = { 11, 13, 16, 28 };
static struct glyph *glyph(int b, int si, int ch)
{
	if (ch < 32 || ch > 127) ch = '?';
	struct glyph *g = &cache[b][si][ch - 32];
	if (!g->adv) {
		FT_Face f = b ? bold : face;
		FT_Set_Pixel_Sizes(f, 0, sizes[si]);
		if (FT_Load_Char(f, ch, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return g;
		FT_Bitmap *bm = &f->glyph->bitmap;
		g->w = bm->width; g->h = bm->rows; g->left = f->glyph->bitmap_left; g->top = f->glyph->bitmap_top; g->adv = f->glyph->advance.x >> 6;
		g->bmp = malloc(g->w * g->h + 1);
		for (int y = 0; y < g->h; y++) memcpy(g->bmp + y * g->w, bm->buffer + y * bm->pitch, g->w);
	}
	return g;
}
static int text_w(int b, int si, const char *s) { int w = 0; for (; *s; s++) w += glyph(b, si, (unsigned char)*s)->adv; return w; }
static void text(int x, int base, int b, int si, const char *s, uint32_t c, int alpha)
{
	for (; *s; s++) {
		struct glyph *g = glyph(b, si, (unsigned char)*s);
		for (int y = 0; y < g->h; y++) for (int i = 0; i < g->w; i++) blend(x + g->left + i, base - g->top + y, c, g->bmp[y * g->w + i] * alpha / 255);
		x += g->adv;
	}
}

/* ---- icons: RGBA, scaled once to the two sizes used ---- */
struct icon { int w, h; uint8_t *px; };
static struct icon big[MAX_APPS], small[MAX_APPS];
static int load_png(const char *path, struct icon *out, int size)
{
	png_image im = { .version = PNG_IMAGE_VERSION };
	if (!png_image_begin_read_from_file(&im, path)) return -1;
	im.format = PNG_FORMAT_RGBA;
	uint8_t *src = malloc(PNG_IMAGE_SIZE(im));
	if (!png_image_finish_read(&im, NULL, src, 0, NULL)) { free(src); return -1; }
	out->w = out->h = size; out->px = malloc(size * size * 4);
	for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {      /* box filter */
		int x0 = x * im.width / size, x1 = (x + 1) * im.width / size, y0 = y * im.height / size, y1 = (y + 1) * im.height / size, n = 0;
		unsigned s[4] = { 0 };
		for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++, n++) for (int k = 0; k < 4; k++) s[k] += src[(yy * im.width + xx) * 4 + k];
		for (int k = 0; k < 4; k++) out->px[(y * size + x) * 4 + k] = n ? s[k] / n : 0;
	}
	free(src); return 0;
}
static void draw_icon(int i, struct icon *ic, int size, int cx, int cy, int alpha)
{
	int x0 = cx - size / 2, y0 = cy - size / 2;
	if (ic->px) {
		for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
			uint8_t *p = &ic->px[(y * size + x) * 4];
			blend(x0 + x, y0 + y, (p[0] << 16) | (p[1] << 8) | p[2], p[3] * alpha / 255);
		}
		return;
	}
	static const uint32_t pal[] = { 0x3d7be0, 0xe0703d, 0x3db07a, 0xb05ad0, 0xd0b03d, 0x3db0c0 };
	unsigned h = 0; for (const char *s = apps[i].name; *s; s++) h = h * 31 + *s;
	char l[2] = { apps[i].name[0], 0 };
	round_rect(x0, y0, size, size, size / 5, pal[h % 6], alpha);
	int si = size >= 48 ? 3 : 2;
	text(cx - text_w(1, si, l) / 2, cy + sizes[si] * 36 / 100, 1, si, l, 0xffffff, alpha);
}

/* ---- the screen ---- */
static int sel;
static void draw(void)
{
	char t[16], b[16];
	fill(0, 0, W, H, 0x101418);
	clock_text(t, sizeof(t)); text(6, 13, 0, 1, t, 0xe8e8e8, 255);
	snprintf(b, sizeof(b), "%d%%", battery_pct()); text(W - 6 - text_w(0, 1, b), 13, 0, 1, b, 0xe8e8e8, 255);
	fill(0, 18, W, 1, 0x2a3038);
	if (!napps) return;
	int l = (sel + napps - 1) % napps, r = (sel + 1) % napps;
	draw_icon(l, &small[l], 40, 70, 64, 110);
	draw_icon(r, &small[r], 40, 250, 64, 110);
	round_rect(160 - 40, 24, 80, 80, 14, 0x2a3442, 255);
	draw_icon(sel, &big[sel], 64, 160, 64, 255);
	text(160 - text_w(0, 2, apps[sel].name) / 2, 128, 0, 2, apps[sel].name, 0xffffff, 255);
	snprintf(b, sizeof(b), "%d / %d", sel + 1, napps); text(160 - text_w(0, 0, b) / 2, 158, 0, 0, b, 0x8a96a6, 255);
}
static void present(void) { for (int y = 0; y < H; y++) memcpy(fbmem + y * fb_stride, back + y * W, W * 2); }

int main(int argc, char **argv)
{
	const char *kbd = "/dev/input/by-path/platform-3f804000.i2c-event"; int idle_s = 0, bench = 0;
	for (int i = 1; i < argc - 1; i++) {
		if (!strcmp(argv[i], "--kbd")) kbd = argv[++i];
		else if (!strcmp(argv[i], "--idle")) idle_s = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--bench")) bench = atoi(argv[++i]);
	}
	double t_main = since_exec_ms(), t0 = now_ms();
	int fd = open("/dev/fb0", O_RDWR); struct fb_fix_screeninfo fi;
	if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fi)) { perror("fb0"); return 1; }
	fb_stride = fi.line_length;
	fbmem = mmap(NULL, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	FT_Init_FreeType(&ft);
	if (FT_New_Face(ft, "/usr/share/fonts/dejavu/DejaVuSans.ttf", 0, &face) || FT_New_Face(ft, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", 0, &bold)) { fprintf(stderr, "font\n"); return 1; }
	double t_init = now_ms();
	scan_apps();
	double t_scan = now_ms();
	/* only the three icons on screen are needed for the first frame; the rest load after it */
	int first[3] = { napps - 1, 0, 1 };
	for (int k = 0; k < 3 && napps; k++) { int i = (first[k] + napps) % napps; if (apps[i].icon[0] && !big[i].px) { load_png(apps[i].icon, &big[i], 64); load_png(apps[i].icon, &small[i], 40); } }
	draw(); present();
	double t_frame = now_ms();
	printf("hand first frame: %.1f ms since exec (loader+libs %.1f, fb+fonts %.1f, scan %.1f, icons+draw %.1f), %d apps\n",
	       since_exec_ms(), t_main, t_init - t0, t_scan - t_init, t_frame - t_scan, napps);
	for (int i = 0; i < napps; i++) if (apps[i].icon[0] && !big[i].px) { load_png(apps[i].icon, &big[i], 64); load_png(apps[i].icon, &small[i], 40); }
	fflush(stdout);

	if (bench) {
		double b0 = now_ms();
		for (int i = 0; i < bench; i++) { sel = (sel + 1) % napps; draw(); present(); }
		printf("hand redraw: %.2f ms a frame (full screen, %d frames)\n", (now_ms() - b0) / bench, bench);
		sel = 0; draw(); present();
	}

	int kfd = open(kbd, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	idle_begin();
	double end = idle_s ? now_ms() + idle_s * 1000.0 : 0;
	for (;;) {
		int to = ms_to_next_minute();
		if (end) { double left = end - now_ms(); if (left <= 0) break; if (left < to) to = (int)left + 1; }
		struct pollfd p = { kfd, POLLIN, 0 };
		int n = poll(&p, kfd >= 0 ? 1 : 0, to);
		if (n == 0) { draw(); present(); continue; }      /* clock tick */
		struct input_event ev; int dirty = 0, quit = 0;
		while (read(kfd, &ev, sizeof(ev)) == sizeof(ev)) {
			if (ev.type != EV_KEY || ev.value == 0) continue;
			if (ev.code == KEY_RIGHT || ev.code == KEY_DOWN) { sel = (sel + 1) % napps; dirty = 1; }
			if (ev.code == KEY_LEFT || ev.code == KEY_UP) { sel = (sel + napps - 1) % napps; dirty = 1; }
			if (ev.code == KEY_ESC) quit = 1;
		}
		if (quit) break;
		if (dirty) { draw(); present(); }
	}
	idle_end("hand");
	return 0;
}
