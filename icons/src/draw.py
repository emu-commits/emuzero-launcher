# SPDX-License-Identifier: MIT
# Draws emuZero's own icons (the apps M5's set has no picture for), flat in the style of M5's:
# bold shapes in strong colours on a clear background, 100 x 100.
#   python3 icons/src/draw.py icons
# Drawn at 4x and scaled down, so the edges come out smooth.
import math
import sys
from PIL import Image, ImageDraw

S = 4            # drawing scale
N = 100 * S


def canvas():
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def px(v):
    return int(round(v * S))


def box(x0, y0, x1, y1):
    return [px(x0), px(y0), px(x1), px(y1)]


def line(d, pts, color, w):
    pts = [(px(x), px(y)) for x, y in pts]
    d.line(pts, fill=color, width=px(w), joint="curve")
    for p in (pts[0], pts[-1]):   # round ends
        r = px(w) / 2
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=color)


def save(im, name):
    im.resize((100, 100), Image.LANCZOS).save(f"{sys.argv[1]}/{name}.png", optimize=True)


GREEN = (52, 199, 89)
BLUE = (47, 128, 237)
SKY = (56, 189, 248)
PINK = (236, 72, 153)
YELLOW = (250, 204, 21)
RED = (239, 68, 68)
ORANGE = (249, 115, 22)
PURPLE = (139, 92, 246)
TEAL = (20, 184, 166)
WHITE = (245, 245, 245)
GREY = (148, 163, 184)
DARK = (30, 41, 59)

# Diagnostics: a pulse line across a screen
im, d = canvas()
d.rounded_rectangle(box(10, 18, 90, 76), radius=px(10), fill=DARK, outline=TEAL, width=px(6))
line(d, [(20, 50), (36, 50), (43, 34), (52, 64), (59, 44), (64, 50), (80, 50)], PINK, 6)
d.rounded_rectangle(box(36, 80, 64, 86), radius=px(3), fill=TEAL)
save(im, "nz-diag")

# Services: three stacked units with their lights
im, d = canvas()
for i, c in enumerate((GREEN, GREEN, YELLOW)):
    y = 14 + i * 26
    d.rounded_rectangle(box(14, y, 86, y + 20), radius=px(6), fill=PURPLE)
    d.ellipse(box(22, y + 6, 30, y + 14), fill=c)
    d.rounded_rectangle(box(40, y + 8, 76, y + 12), radius=px(2), fill=(196, 181, 253))
save(im, "nz-services")

# System monitor: bars of a chart on a base line
im, d = canvas()
for x, h, c in ((16, 30, SKY), (34, 52, YELLOW), (52, 40, PINK), (70, 64, GREEN)):
    d.rounded_rectangle(box(x, 82 - h, x + 14, 82), radius=px(4), fill=c)
d.rounded_rectangle(box(10, 84, 90, 90), radius=px(3), fill=GREY)
save(im, "htop")

# Editor: a page of text with a pencil across it
im, d = canvas()
d.rounded_rectangle(box(16, 10, 70, 86), radius=px(7), fill=WHITE)
for y, w in ((26, 38), (38, 30), (50, 38), (62, 22)):
    d.rounded_rectangle(box(24, y, 24 + w, y + 5), radius=px(2), fill=GREY)
pen = Image.new("RGBA", (N, N), (0, 0, 0, 0))
p = ImageDraw.Draw(pen)
p.rounded_rectangle(box(44, 42, 56, 92), radius=px(3), fill=YELLOW)        # body
p.rectangle(box(44, 42, 56, 50), fill=PINK)                                # eraser end
p.polygon([(px(44), px(92)), (px(56), px(92)), (px(50), px(104))], fill=(253, 230, 138))
p.polygon([(px(48), px(100)), (px(52), px(100)), (px(50), px(104))], fill=DARK)
pen = pen.rotate(-40, resample=Image.BICUBIC, center=(px(50), px(70)), translate=(px(16), px(-14)))
im.alpha_composite(pen)
save(im, "nano")

# Wi-Fi setup: three arcs over a dot
im, d = canvas()
cx, cy = 50, 78
for r in (18, 34, 50):
    d.arc(box(cx - r, cy - r, cx + r, cy + r), start=225, end=315, fill=BLUE, width=px(8))
    for a in (225, 315):   # round the arc ends
        x, y = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
        d.ellipse(box(x - 4, y - 4, x + 4, y + 4), fill=BLUE)
d.ellipse(box(cx - 7, cy - 7, cx + 7, cy + 7), fill=BLUE)
save(im, "nz-wifi")

# Power off: the power symbol
im, d = canvas()
d.arc(box(16, 18, 84, 86), start=-60, end=240, fill=RED, width=px(9))
for a in (-60, 240):
    x, y = 50 + 34 * math.cos(math.radians(a)) - 0, 52 + 34 * math.sin(math.radians(a))
    r = 4.5
    x -= 4.5 * math.cos(math.radians(a)) * 0
    d.ellipse(box(x - r, y - r, x + r, y + r), fill=RED)
line(d, [(50, 12), (50, 46)], RED, 9)
save(im, "nz-poweroff")

# Theme: a painter's palette
im, d = canvas()
d.ellipse(box(8, 16, 92, 84), fill=(222, 184, 135))
d.ellipse(box(54, 52, 70, 68), fill=(0, 0, 0, 0))                          # thumb hole
for x, y, c in ((26, 34, RED), (44, 26, YELLOW), (63, 30, GREEN), (24, 54, BLUE)):
    d.ellipse(box(x - 7, y - 7, x + 7, y + 7), fill=c)
save(im, "emuzero-theme")
