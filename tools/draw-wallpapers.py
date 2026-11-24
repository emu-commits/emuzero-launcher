# SPDX-License-Identifier: MIT
# Draws the wallpapers of the themes that have one, 320 x 170 like the screen:
#   python3 tools/draw-wallpapers.py themes
# All original drawings: shapes and colours only, nothing traced or copied.
import math
import random
import sys
from PIL import Image, ImageDraw, ImageFilter

W, H = 320, 170
OUT = sys.argv[1]


def save(im, theme):
    im.convert("RGB").save(f"{OUT}/{theme}/wallpaper.png", optimize=True)


def vgrad(d, x0, y0, x1, y1, top, bottom):
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        d.line([(x0, y), (x1 - 1, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))


# ---- Desktop 2001: blue sky over a rolling green hill -------------------------------------
S = 4
im = Image.new("RGB", (W * S, H * S))
d = ImageDraw.Draw(im)
vgrad(d, 0, 0, W * S, H * S, (38, 104, 214), (160, 206, 244))
clouds = Image.new("L", (W * S, H * S), 0)
c = ImageDraw.Draw(clouds)
random.seed(7)
for cx, cy, n in ((60, 34, 7), (210, 22, 9), (290, 58, 5), (130, 70, 4)):
    for _ in range(n):
        x = cx + random.randint(-22, 22)
        y = cy + random.randint(-5, 5)
        r = random.randint(8, 15)
        c.ellipse([(x - r * 1.6) * S, (y - r) * S, (x + r * 1.6) * S, (y + r) * S], fill=170)
clouds = clouds.filter(ImageFilter.GaussianBlur(9 * S))
im.paste((250, 252, 255), (0, 0), clouds)
hill = Image.new("L", (W * S, H * S), 0)
h = ImageDraw.Draw(hill)
pts = [(x * S, (112 - 26 * math.sin((x + 40) / 320 * math.pi) + 6 * math.sin(x / 45)) * S) for x in range(0, W + 1, 2)]
h.polygon(pts + [(W * S, H * S), (0, H * S)], fill=255)
green = Image.new("RGB", (W * S, H * S))
vgrad(ImageDraw.Draw(green), 0, 60 * S, W * S, H * S, (120, 196, 64), (38, 120, 30))
im.paste(green, (0, 0), hill)
save(im.resize((W, H), Image.LANCZOS), "desktop2001")

# ---- Super Handheld: a console border around the screen -----------------------------------
# the screen (the window) sits at x 30..290, y 14..142; the status bar along the bottom
im = Image.new("RGB", (W, H), (86, 78, 104))
d = ImageDraw.Draw(im)
for y in range(0, H, 3):                                   # fine pinstripes in the plastic
    d.line([(0, y), (W, y)], fill=(80, 72, 98))
sx0, sy0, sx1, sy1 = 30, 14, 290, 142
d.rectangle([sx0 - 9, sy0 - 7, sx1 + 8, sy1 + 6], fill=(58, 52, 72))           # recess
d.rectangle([sx0 - 9, sy0 - 7, sx1 + 8, sy0 - 6], fill=(40, 35, 52))
d.rectangle([sx0 - 9, sy1 + 5, sx1 + 8, sy1 + 6], fill=(112, 104, 132))
d.rectangle([sx0 - 3, sy0 - 3, sx1 + 2, sy1 + 2], fill=(24, 20, 32))           # screen edge
for i, col in enumerate(((216, 64, 96), (240, 176, 64), (72, 168, 216), (120, 88, 200))):
    y = sy1 + 11 + i * 2                                   # four coloured stripes under the screen
    d.line([(sx0 - 9, y), (sx0 + 52, y)], fill=col)
    d.line([(sx1 - 52, y), (sx1 + 8, y)], fill=col)
for x, col in ((10, (216, 64, 96)), (16, (240, 176, 64)), (W - 17, (72, 168, 216)), (W - 11, (120, 88, 200))):
    d.ellipse([x - 2, sy0 + 10 - 2, x + 2, sy0 + 10 + 2], fill=col)            # corner lamps
    d.ellipse([x - 2, sy1 - 10 - 2, x + 2, sy1 - 10 + 2], fill=col)
for i in range(4):                                         # speaker slots at the sides
    y = (sy0 + sy1) // 2 - 12 + i * 8
    d.rounded_rectangle([7, y, 20, y + 3], radius=1, fill=(58, 52, 72))
    d.rounded_rectangle([W - 21, y, W - 8, y + 3], radius=1, fill=(58, 52, 72))
save(im, "superhandheld")

# ---- Field Index: the red shell of a pocket field guide ----------------------------------
S = 4
im = Image.new("RGB", (W * S, H * S), (206, 16, 44))
d = ImageDraw.Draw(im)
vgrad(d, 0, 30 * S, W * S, H * S, (204, 14, 42), (168, 8, 32))
# the lid's edge: a step down from the right to the left, under the lens and lamps
edge = [(0, 33), (96, 33), (116, 25), (W, 25)]
d.polygon([(x * S, y * S) for x, y in edge] + [(W * S, 0), (0, 0)], fill=(222, 26, 54))
d.line([(x * S, y * S) for x, y in edge], fill=(110, 0, 20), width=2 * S)
d.line([(x * S, (y + 2) * S) for x, y in edge], fill=(240, 80, 100), width=S)
# hinge down the left side
d.rectangle([0, 33 * S, 6 * S, H * S], fill=(150, 4, 28))
for y in range(44, H, 22):
    d.rectangle([0, y * S, 6 * S, (y + 12) * S], fill=(120, 0, 22))
d.line([(6 * S, 33 * S), (6 * S, H * S)], fill=(110, 0, 20), width=S)
# the big lens
lx, ly = 17, 15


def disc(x, y, r, fill):
    d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=fill)


disc(lx, ly, 13, (110, 0, 20))
disc(lx, ly, 12, (250, 250, 250))
disc(lx, ly, 9.5, (40, 40, 52))
disc(lx, ly, 8.5, (44, 140, 236))
disc(lx + 1, ly + 1.5, 6.5, (30, 112, 212))
disc(lx - 3, ly - 3, 2.6, (210, 236, 255))
# three small lamps
for i, col in enumerate(((250, 50, 50), (250, 210, 40), (60, 200, 70))):
    x, y = 40 + i * 11, 9
    disc(x, y, 4, (60, 0, 12))
    disc(x, y, 3.2, col)
    disc(x - 1, y - 1, 1, (255, 255, 255))
save(im.resize((W, H), Image.LANCZOS), "fieldindex")

# ---- Cyberdeck: a night city over a neon floor grid, with scanlines ----------------------
S = 4
im = Image.new("RGB", (W * S, H * S), (7, 6, 15))
d = ImageDraw.Draw(im)
hz = 100                                                    # horizon
vgrad(d, 0, 0, W * S, hz * S, (7, 6, 15), (40, 10, 52))
glow = Image.new("L", (W * S, H * S), 0)
ImageDraw.Draw(glow).rectangle([0, (hz - 6) * S, W * S, (hz + 3) * S], fill=200)
im.paste((255, 42, 109), (0, 0), glow.filter(ImageFilter.GaussianBlur(10 * S)))
random.seed(3)
x = 0
while x < W:                                                # skyline
    bw = random.randint(10, 26)
    bh = random.randint(14, 52)
    d.rectangle([x * S, (hz - bh) * S, (x + bw - 2) * S, hz * S], fill=(16, 10, 30))
    for wy in range(hz - bh + 4, hz - 2, 5):                 # a few lit windows
        for wx in range(x + 3, x + bw - 4, 4):
            if random.random() < 0.18:
                col = random.choice(((0, 240, 255), (255, 42, 109), (250, 220, 120)))
                d.rectangle([wx * S, wy * S, (wx + 1) * S, (wy + 1) * S], fill=tuple(v // 2 for v in col))
    x += bw
d.rectangle([0, hz * S, W * S, H * S], fill=(10, 4, 22))
for i in range(-14, 15):                                    # floor lines to the vanishing point
    d.line([(W / 2 * S, hz * S), ((W / 2 + i * 34) * S, H * S)], fill=(150, 30, 120), width=S)
for k in range(1, 12):                                      # cross lines, closer together far away
    y = hz + (H - hz) * (k / 11) ** 2
    d.line([(0, y * S), (W * S, y * S)], fill=(150, 30, 120), width=S)
d.line([(0, hz * S), (W * S, hz * S)], fill=(0, 240, 255), width=S)
im = im.resize((W, H), Image.LANCZOS)
d = ImageDraw.Draw(im, "RGBA")
for y in range(0, H, 2):                                    # scanlines
    d.line([(0, y), (W, y)], fill=(0, 0, 0, 70))
save(im, "cyberdeck")
