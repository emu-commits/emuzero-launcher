# SPDX-License-Identifier: MIT
# Draws the small folder pictures in the title bars of the desktop themes, pixel by pixel at
# the size each title bar shows them (no scaling):
#   python3 tools/draw-title-icons.py themes
# Our own drawings; no image library needed.
import struct
import sys
import zlib

OUT = sys.argv[1]


def save(path, rows, colours):
    h, w = len(rows), len(rows[0])
    raw = b""
    for r in rows:
        assert len(r) == w, r
        raw += b"\0" + b"".join(bytes(colours[c]) for c in r)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


CLEAR = (0, 0, 0, 0)

# Desktop 95: 12 x 12 (a 16-px title bar), black outline, flat yellows
save(f"{OUT}/desktop95/folder.png", [
    "............",
    ".kkkk.......",
    "kyyyyk......",
    "kyyyyykkkkk.",
    "kyyyyyyyyyyk",
    "kywwwwwwwwwk",
    "kwlllllllllk",
    "kwlllllllllk",
    "kwlllllllllk",
    "kwlllllllllk",
    "kkkkkkkkkkkk",
    "............",
], {".": CLEAR, "k": (0, 0, 0, 255), "y": (192, 160, 0, 255), "w": (255, 255, 200, 255),
    "l": (255, 224, 64, 255)})

# Desktop 2001: 14 x 14 (an 18-px title bar), soft brown outline, a lighter front
save(f"{OUT}/desktop2001/folder.png", [
    "..............",
    ".ooooo........",
    "oyyyyyo.......",
    "oyyyyyyooooo..",
    "oyyyyyyyyyyyo.",
    "oyyyyyyyyyyyyo",
    "owwwwwwwwwwwwo",
    "owllllllllllwo",
    "owllllllllllmo",
    "owlllllllllmmo",
    "owllllllllmmmo",
    "omllmmmmmmmmmo",
    ".oooooooooooo.",
    "..............",
], {".": CLEAR, "o": (168, 124, 32, 255), "y": (230, 186, 70, 255), "w": (255, 246, 196, 255),
    "l": (255, 222, 120, 255), "m": (240, 196, 84, 255)})
