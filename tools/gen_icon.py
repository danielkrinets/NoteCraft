#!/usr/bin/env python3
"""Draws the NoteCraft icon (a book and quill, 32x32 pixel art) and writes
assets/icon-32.png, icon-256.png, icon-512.png, notecraft.svg and
src/icon_data.h. Pure Python (zlib + struct), no dependencies."""
import math, os, struct, zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
N = 32
T = None
C = {
    "ol":   (0x1d, 0x12, 0x0a), "cover": (0x74, 0x4a, 0x24), "cover_hi": (0x9a, 0x6b, 0x38),
    "cover_lo": (0x4a, 0x2d, 0x15), "page": (0xe9, 0xdb, 0xb2), "page_sh": (0xc9, 0xb5, 0x84),
    "page_edge": (0xf5, 0xec, 0xcb), "text": (0x8a, 0x75, 0x50),
    "fe": (0xf6, 0xf3, 0xea), "fe_sh": (0xb9, 0xbd, 0xcf), "fe_ol": (0x2c, 0x2f, 0x45),
    "shaft": (0xd9, 0xc6, 0x8f), "nib": (0x24, 0x1c, 0x4a), "ink": (0x3a, 0x36, 0x9c),
}
px = [[T] * N for _ in range(N)]

def put(x, y, c):
    if 0 <= x < N and 0 <= y < N: px[y][x] = C[c] if isinstance(c, str) else c

def rect(x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1): put(x, y, c)

# --- open book -------------------------------------------------------------
rect(1, 11, 30, 28, "ol")                      # outline
rect(2, 12, 29, 27, "cover")
rect(2, 12, 29, 12, "cover_hi")                # lit top edge
rect(2, 27, 29, 27, "cover_lo")                # shaded bottom edge
rect(3, 13, 28, 25, "page_sh")                 # page block edge
rect(4, 13, 15, 24, "page")                    # left page
rect(16, 13, 27, 24, "page")                   # right page
rect(4, 13, 27, 13, "page_edge")
rect(14, 13, 15, 24, "page_sh")                # spine shadow (left)
rect(16, 13, 17, 24, "page_sh")                # spine shadow (right)
rect(15, 12, 16, 26, "cover_lo")               # spine
for y in (16, 18, 20, 22):                     # lines of text
    rect(6, y, 12 if y != 22 else 9, y, "text")
for y in (16, 18, 20):
    rect(19, y, 25 if y != 20 else 22, y, "text")
rect(3, 26, 28, 26, "page_edge")               # lower page stack

# --- quill -------------------------------------------------------------------
P0, P1 = (5.0, 30.0), (28.0, 3.0)
dx, dy = P1[0] - P0[0], P1[1] - P0[1]
L = math.hypot(dx, dy)
ux, uy = dx / L, dy / L
nx, ny = -uy, ux
feather = {}
for y in range(N):
    for x in range(N):
        fx, fy = x + 0.5 - P0[0], y + 0.5 - P0[1]
        t = (fx * ux + fy * uy) / L
        s = fx * nx + fy * ny
        if t < 0.0 or t > 1.0: continue
        w = 0.0
        if t > 0.30:
            k = (t - 0.30) / 0.70
            w = 4.6 * math.sin(math.pi * min(1.0, k * 0.92 + 0.04)) ** 0.8
            if k > 0.85: w *= 1.0 - (k - 0.85) * 2.0
        if abs(s) <= max(w, 0.55) and not (t < 0.26 and abs(s) > 0.55):
            if abs(s) < 0.62: feather[(x, y)] = "shaft" if t > 0.2 else "nib"
            else: feather[(x, y)] = "fe_sh" if s > 0 else "fe"
if (5, 29) not in feather: feather[(5, 29)] = "nib"
feather[(4, 30)] = "nib"; feather[(5, 30)] = "nib"
for (x, y), c in feather.items():
    pass
fill = set(feather)
for (x, y) in list(fill):                      # outline
    for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        q = (x + ddx, y + ddy)
        if q not in fill: put(q[0], q[1], "fe_ol")
for (x, y), c in feather.items(): put(x, y, c)
# ink drop at the nib
put(3, 31, "ink"); put(4, 31, "ink"); put(5, 31, "ink")

# --- outputs -----------------------------------------------------------------
def png_bytes(w, h, rows):
    raw = b"".join(b"\x00" + b"".join(bytes(p if p else (0, 0, 0)) + (b"\xff" if p else b"\x00") for p in r) for r in rows)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def png(path, w, h, rows):
    with open(path, "wb") as f:
        f.write(png_bytes(w, h, rows))

def scaled(k):
    return [[px[y // k][x // k] for x in range(N * k)] for y in range(N * k)]

os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
png(os.path.join(ROOT, "assets/icon-32.png"), N, N, px)
png(os.path.join(ROOT, "assets/icon-256.png"), N * 8, N * 8, scaled(8))
png(os.path.join(ROOT, "assets/icon-512.png"), N * 16, N * 16, scaled(16))

# SVG: merge horizontal runs of equal colour
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" width="256" height="256" shape-rendering="crispEdges">']
for y in range(N):
    x = 0
    while x < N:
        c = px[y][x]
        if c is None: x += 1; continue
        x2 = x
        while x2 + 1 < N and px[y][x2 + 1] == c: x2 += 1
        svg.append('<rect x="%d" y="%d" width="%d" height="1" fill="#%02x%02x%02x"/>' % (x, y, x2 - x + 1, *c))
        x = x2 + 1
svg.append("</svg>")
open(os.path.join(ROOT, "assets/notecraft.svg"), "w").write("\n".join(svg) + "\n")

# C header for the window icon (RGBA bytes)
vals = []
for y in range(N):
    for x in range(N):
        p = px[y][x]
        vals += [p[0], p[1], p[2], 255] if p else [0, 0, 0, 0]
lines = ["/* generated by tools/gen_icon.py - do not edit */", "#define ICON_W 32", "#define ICON_H 32",
         "static const unsigned char icon_pixels[] = {"]
for i in range(0, len(vals), 16):
    lines.append("    " + ", ".join(str(v) for v in vals[i:i + 16]) + ",")
lines.append("};")
open(os.path.join(ROOT, "src/icon_data.h"), "w").write("\n".join(lines) + "\n")
# macOS .icns (PNG-compressed icon elements)
elems = b""
for typ, k in ((b"icp5", 1), (b"icp6", 2), (b"ic07", 4), (b"ic08", 8), (b"ic09", 16), (b"ic10", 32)):
    data = png_bytes(N * k, N * k, scaled(k))
    elems += typ + struct.pack(">I", len(data) + 8) + data
open(os.path.join(ROOT, "assets/NoteCraft.icns"), "wb").write(b"icns" + struct.pack(">I", len(elems) + 8) + elems)
print("icon written")
