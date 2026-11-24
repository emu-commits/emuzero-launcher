#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Draws the test icons (simple shapes; no third-party artwork) as RGBA PNGs.
import math, struct, sys, zlib, os

def png(path, w, h, px):
    raw = b''.join(b'\0' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def draw(path, w, h, shape):
    px = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            c = shape((x + .5) / w, (y + .5) / h)
            if c: px[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(c)
    png(path, w, h, px)

def rounded(u, v, r=.2):
    dx, dy = max(abs(u - .5) - (.5 - r), 0), max(abs(v - .5) - (.5 - r), 0)
    return dx * dx + dy * dy <= r * r

def camera(u, v):   # lens on a body
    if math.hypot(u - .5, v - .55) < .2: return (30, 30, 40, 255) if math.hypot(u - .5, v - .55) < .12 else (200, 210, 220, 255)
    if .1 < u < .9 and .3 < v < .8: return (60, 70, 85, 255)
    if .35 < u < .6 and .2 < v < .31: return (60, 70, 85, 255)

def folder(u, v):
    if .1 < u < .45 and .2 < v < .32: return (230, 170, 60, 255)
    if .1 < u < .9 and .3 < v < .8: return (245, 190, 80, 255)

def wave(u, v):     # transparent background, round
    if math.hypot(u - .5, v - .5) > .48: return None
    return (40, 160, 120, 255) if abs(v - .5 - .15 * math.sin(u * 12)) < .06 else (20, 60, 70, 255)

def music(u, v):    # wide (not square) icon, to check the fit
    if rounded(u, v, .3): return (180, 70, 140, 255) if (u * 6 + v * 2) % 1 < .5 else (120, 40, 100, 255)

root = sys.argv[1]
draw(root + '/usr/share/APPLaunch/share/images/camera.png', 100, 100, camera)
draw(root + '/usr/share/APPLaunch/share/images/files.png', 100, 100, folder)
draw(root + '/usr/share/icons/hicolor/64x64/apps/tide.png', 64, 64, wave)
draw(root + '/usr/share/APPLaunch/share/images/tracker.png', 120, 80, music)
