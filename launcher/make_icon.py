#!/usr/bin/env python3
"""Draws the launcher's icon (launcher.ico): Liberty City Stories' comic cover in a rounded square - a made man's black
fedora on a red disc ringed in the pause menu's orange, over a night-blue ground with halftone dots (launcher/ui/theme.h,
screens/backdrop.cpp). And the game's (src/lcs.ico, lcs.exe's): the same fedora on the orange alone, so the two
programs don't look alike in Explorer or the taskbar (DECISIONS.md 2026-11-17: nothing of Vice City's in the look).
Standard library only.

    python launcher/make_icon.py            writes launcher/launcher.ico (16, 20, 24, 32, 40, 48, 64, 256 px),
                                            launcher/launcher.png (256 px: the Linux .desktop entry's icon) and
                                            src/lcs.ico (the same sizes)
    python launcher/make_icon.py --png DIR  also writes each size as DIR/launcher_<size>.png and DIR/game_<size>.png
    python launcher/make_icon.py --header OUT.h   only the X11 window icon (_NET_WM_ICON: 32, 64, 128 px as
                                            ARGB) as a C header - launcher/CMakeLists.txt runs it on Linux
    python launcher/make_icon.py --icns OUT.icns  only the macOS app icon (PNG entries 16 - 256 px) - the
                                            launcher's CMakeLists.txt runs it on macOS for LCSLauncher.app

Drawn here rather than kept as artwork, so the repository holds the source of every byte. Small sizes draw less (no
windows or stars below 128 px, no towers below 24) and up to 64 px put the scene's edges and the sun's stripes on whole
pixels so they stay sharp.
"""
import math
import os
import struct
import sys
import zlib


def rgb(v):
    return ((v >> 16 & 255) / 255, (v >> 8 & 255) / 255, (v & 255) / 255)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


WHITE = rgb(0xFFFFFF)


class Palette:
    def __init__(self, top, bottom, disc, ring, dots, band, disc_on=True):
        self.top, self.bottom, self.disc, self.ring = rgb(top), rgb(bottom), rgb(disc), rgb(ring)
        self.dots, self.band, self.disc_on = rgb(dots), rgb(band), disc_on


INK = rgb(0x07080D)
# The launcher: night blue, a red disc ringed in orange
DUSK = Palette(top=0x1E2F63, bottom=0x05070F, disc=0xB8141E, ring=0xF6A623, dots=0x2D5C9E, band=0x7A0C13)
# The game (lcs.exe): the orange square itself, no disc
DAY = Palette(top=0xFFC65E, bottom=0xE8901A, disc=0xB8141E, ring=0xF6A623, dots=0xFFE3A6, band=0xB8141E, disc_on=False)


def hash01(n):
    """backdrop.cpp's hash: the same 'random' numbers every time, in [0, 1)."""
    n &= 0xFFFFFFFF
    n ^= n >> 16
    n = (n * 0x7FEB352D) & 0xFFFFFFFF
    n ^= n >> 15
    n = (n * 0x846CA68B) & 0xFFFFFFFF
    n ^= n >> 16
    return (n & 0xFFFFFF) / 16777216


def in_poly(poly, x, y):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


class Scene:
    """The icon's shapes for one size, in [0, 1) units."""

    def __init__(self, size, pal=DUSK):
        self.size = size
        self.p = pal
        self.radius = 0.19  # the rounded square's corners
        self.disc = (0.5, 0.5, 0.40)
        self.ring = 0.045 if size > 32 else 0.07
        self.edge = max(0.014, 1.2 / size)  # the ring's black outline
        # the fedora: a crown pinched at the top (its dent), a band, a wide brim
        y = 0.04 if pal.disc_on else 0.0
        self.crown = [(0.30, 0.62 + y), (0.345, 0.36 + y), (0.44, 0.31 + y), (0.50, 0.345 + y), (0.56, 0.31 + y),
                      (0.655, 0.36 + y), (0.70, 0.62 + y)]
        self.band = (0.55 + y, 0.615 + y)
        self.brim = (0.5, 0.64 + y, 0.37 if not pal.disc_on else 0.33, 0.06)  # centre, half-width, half-height
        self.dots = size >= 64


def pixel(S, x, y):
    """RGB of the icon at (x, y) in [0, 1), as floats (None outside the rounded square); supersampled by the caller."""
    r = S.radius
    cx, cy = min(max(x, r), 1 - r), min(max(y, r), 1 - r)
    if (x - cx) ** 2 + (y - cy) ** 2 > r * r:
        return None
    P = S.p
    colour = mix(P.top, P.bottom, y)
    # halftone: dots on a slanted grid, larger toward the top right
    if S.dots:
        cell = 0.05
        row = math.floor(y / cell)
        gx = x / cell - (0.5 if row % 2 else 0.0)
        dx, dy = (gx - math.floor(gx) - 0.5) * cell, (y / cell - row - 0.5) * cell
        dr = cell * 0.42 * max(0.0, 1.0 - math.hypot(x - 0.95, y - 0.05) / 1.1)
        if dx * dx + dy * dy < dr * dr:
            colour = mix(colour, P.dots, 0.55)
    if P.disc_on:
        dcx, dcy, dr = S.disc
        d = math.hypot(x - dcx, y - dcy)
        if d < dr + S.edge:
            colour = INK
        if d < dr:
            colour = P.ring
        if d < dr - S.ring:
            colour = INK
        if d < dr - S.ring - S.edge * 0.6:
            colour = P.disc
    # the fedora
    bx, by, bw, bh = S.brim
    if ((x - bx) / bw) ** 2 + ((y - by) / bh) ** 2 < 1 or in_poly(S.crown, x, y):
        colour = INK
        if S.band[0] <= y < S.band[1] and in_poly(S.crown, x, y) and abs(x - 0.5) < 0.19:
            colour = P.band
    return colour


def render(size, pixels_only=False, pal=DUSK):
    S = Scene(size, pal)
    n = 8 if size <= 64 else 3  # supersampling per side
    rows = []
    for py in range(size):
        row = bytearray([0])  # PNG filter: none
        for px in range(size):
            acc = [0.0, 0.0, 0.0]
            cover = 0
            for sy in range(n):
                for sx in range(n):
                    c = pixel(S, (px + (sx + 0.5) / n) / size, (py + (sy + 0.5) / n) / size)
                    if c is not None:
                        for i in range(3):
                            acc[i] += c[i]
                        cover += 1
            rgb_ = [int(min(1, acc[i] / cover) * 255 + 0.5) if cover else 0 for i in range(3)]
            row += bytes(rgb_ + [int(cover * 255 / (n * n) + 0.5)])
        rows.append(bytes(row))
    raw = b"".join(rows)
    if pixels_only:
        return b"".join(r[1:] for r in rows)  # RGBA, no filter bytes

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_header(path):
    """_NET_WM_ICON's layout: width, height, then ARGB pixels, one per `unsigned long` (Xlib's 32-bit format), for
    each size."""
    words = []
    for s in (32, 64, 128):
        rgba = render(s, pixels_only=True)
        words += [s, s]
        for i in range(0, len(rgba), 4):
            r, g, b, a = rgba[i:i + 4]
            words.append(a << 24 | r << 16 | g << 8 | b)
    lines = ["// Generated by launcher/make_icon.py --header - do not edit.", "#pragma once",
             "static const unsigned long g_launcher_icon[] = {"]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join("0x%08x" % w for w in words[i:i + 8]) + ",")
    lines.append("};")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, path)


def write_icns(path):
    """Apple's icon container: 'icns', its length, then (type, length, PNG) entries."""
    entries = b""
    for kind, size in ((b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128), (b"ic08", 256)):
        data = render(size)
        entries += kind + struct.pack(">I", 8 + len(data)) + data
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(entries)) + entries)
    os.replace(tmp, path)


def main():
    if "--icns" in sys.argv:
        write_icns(sys.argv[sys.argv.index("--icns") + 1])
        return
    if "--header" in sys.argv:
        write_header(sys.argv[sys.argv.index("--header") + 1])
        return
    here = os.path.dirname(os.path.abspath(__file__))
    sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    for name, pal, out, png in (("launcher", DUSK, os.path.join(here, "launcher.ico"), os.path.join(here, "launcher.png")),
                                ("game", DAY, os.path.join(here, "..", "src", "lcs.ico"), None)):
        images = [render(s, pal=pal) for s in sizes]
        if "--png" in sys.argv:
            folder = sys.argv[sys.argv.index("--png") + 1]
            os.makedirs(folder, exist_ok=True)
            for s, data in zip(sizes, images):
                with open(os.path.join(folder, "%s_%d.png" % (name, s)), "wb") as f:
                    f.write(data)
        header = struct.pack("<HHH", 0, 1, len(sizes))
        offset = 6 + 16 * len(sizes)
        entries = b""
        for s, data in zip(sizes, images):
            entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
            offset += len(data)
        out = os.path.normpath(out)
        with open(out, "wb") as f:
            f.write(header + entries + b"".join(images))
        print("wrote " + out)
        if png:
            with open(png, "wb") as f:
                f.write(images[-1])
            print("wrote " + png)


if __name__ == "__main__":
    main()
