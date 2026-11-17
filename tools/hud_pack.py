#!/usr/bin/env python3
"""A high-resolution HUD texture pack for the game (docs/LCS.md "HUD texture pack").

    python tools/hud_pack.py install PACK_DIR [--icons pack|original] [--maps]
    python tools/hud_pack.py status
    python tools/hud_pack.py remove

PACK_DIR is a pack's folder of PNG images - made for PPSSPP (the PSP version: "GTA LCS HD+ Interface", its
ULES00151 folder or the one above it). Its images are named by the PSP's texture hashes' table, which means nothing to
this game, so they are not looked up by name: `install` converts the HUD's images (Text, HUD with its Radar and
weapons, Radio, Buttons) into <data>/hud_pack - raw RGBA, and for each a probe: the image box-averaged down to the sizes
the game's own texture may have (1/2, 1/4, 1/8) - and the game matches each HUD texture to the probes of its size by
content when Graphics > HD HUD textures is on.

--icons pack (default): the pack's own weapon icons (its HUD/weapons, recognised through its Original Icons folder);
--icons original: the Original Icons themselves. --maps: the radar's map tiles too (the pack redraws them in the PSP's
style; off by default).

The pack's images stay the user's: copied into their data folder, never into this tree. Standard library only.
"""
import argparse
import os
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backup  # noqa: E402  (data_dir)

OUT = "hud_pack"
FOLDERS = ("text", "hud", "radio", "buttons")  # first path part, lower case
ICONS_ORIGINAL = "original icons - replace"
SCALES = (2, 4, 8)


def out_dir():
    return os.path.join(backup.data_dir(), OUT)


# ---- PNG (8 / 16-bit, grey / RGB / palette / grey+alpha / RGBA, non-interlaced) -> rows of RGBA bytes ----------------
def read_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    i, idat, pal, trns = 8, [], None, None
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t, c = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        i += 12 + n
        if t == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", c)
        elif t == b"PLTE":
            pal = c
        elif t == b"tRNS":
            trns = c
        elif t == b"IDAT":
            idat.append(c)
    if il != 0 or bd not in (8, 16) or ct not in (0, 2, 3, 4, 6) or (ct == 3 and bd != 8):
        raise ValueError("unsupported PNG (bit depth %d, colour type %d, interlace %d)" % (bd, ct, il))
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct] * (bd // 8)
    raw = zlib.decompress(b"".join(idat))
    stride = w * ch
    prev = bytearray(stride)
    out = bytearray(w * h * 4)
    k = 0
    for y in range(h):
        f = raw[k]
        line = bytearray(raw[k + 1:k + 1 + stride])
        k += 1 + stride
        if f == 1:
            for x in range(ch, stride):
                line[x] = (line[x] + line[x - ch]) & 255
        elif f == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif f == 3:
            for x in range(stride):
                line[x] = (line[x] + ((line[x - ch] if x >= ch else 0) + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b, c = prev[x], (prev[x - ch] if x >= ch else 0)
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        px = line[::2] if bd == 16 else line  # 16-bit: the high bytes
        o = y * w * 4
        if ct == 6:
            out[o:o + w * 4] = px
        elif ct == 2:
            out[o:o + w * 4:4], out[o + 1:o + w * 4:4], out[o + 2:o + w * 4:4] = px[0::3], px[1::3], px[2::3]
            out[o + 3:o + w * 4:4] = b"\xff" * w
        elif ct == 0:
            out[o:o + w * 4:4] = out[o + 1:o + w * 4:4] = out[o + 2:o + w * 4:4] = px
            out[o + 3:o + w * 4:4] = b"\xff" * w
        elif ct == 4:
            g = px[0::2]
            out[o:o + w * 4:4] = out[o + 1:o + w * 4:4] = out[o + 2:o + w * 4:4] = g
            out[o + 3:o + w * 4:4] = px[1::2]
        else:  # palette
            for x in range(w):
                p = px[x]
                out[o + 4 * x:o + 4 * x + 3] = pal[3 * p:3 * p + 3]
                out[o + 4 * x + 3] = trns[p] if trns and p < len(trns) else 255
    return w, h, out


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    return struct.unpack(">II", d[16:24])


# The image box-averaged down by s: colour weighted by alpha, alpha the mean (gs_hw.cpp compares against this).
def probe(w, h, rgba, s):
    pw, ph = w // s, h // s
    out = bytearray(pw * ph * 4)
    n = s * s
    for y in range(ph):
        for x in range(pw):
            r = g = b = a = 0
            for yy in range(y * s, y * s + s):
                o = (yy * w + x * s) * 4
                for i in range(o, o + 4 * s, 4):
                    al = rgba[i + 3]
                    r += rgba[i] * al
                    g += rgba[i + 1] * al
                    b += rgba[i + 2] * al
                    a += al
            q = (y * pw + x) * 4
            if a:
                out[q:q + 4] = bytes((r // a, g // a, b // a, a // n))
    return out


def images(pack, maps):
    """(relative path, full path) of the HUD's images, the folder that has ULES00151 accepted too."""
    for sub in os.listdir(pack):
        if os.path.isdir(os.path.join(pack, sub, "HUD")) or os.path.isdir(os.path.join(pack, sub, "Text")):
            pack = os.path.join(pack, sub)
            break
    found = []
    for root, _, files in os.walk(pack):
        for f in files:
            if not f.lower().endswith(".png"):
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, pack).replace("\\", "/")
            top = rel.split("/")[0].lower()
            if top in FOLDERS or top == ICONS_ORIGINAL or (maps and top == "map" and f.lower().startswith("map_")):
                found.append((rel, full))
    return pack, sorted(found)


def install(args):
    pack, found = images(args.pack, args.maps)
    if not found:
        sys.exit("hud_pack: no HUD images in %s (a pack's folder of PNG images: Text, HUD ...)" % args.pack)
    # weapon icons: the pack's own (HUD/weapons) unless --icons original; the Original Icons recognise them either way
    own = {rel.split("/")[-1].lower(): full for rel, full in found if rel.lower().startswith("hud/weapons/")}
    dest = out_dir()
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    lines, done = [], {}
    for n, (rel, full) in enumerate(found):
        try:
            w, h = png_size(full)
        except (OSError, struct.error):
            continue
        if w > 1024 or h > 1024:
            continue
        shown = full
        if rel.lower().startswith(ICONS_ORIGINAL + "/") and args.icons == "pack":
            shown = own.get(rel.split("/")[-1].lower(), full)
        try:
            w, h, rgba = read_png(full)
            if shown not in done:
                sw, sh, srgba = (w, h, rgba) if shown == full else read_png(shown)
                if (sw, sh) != (w, h):
                    shown = full  # (a different size: keep the one recognised)
                    sw, sh, srgba = w, h, rgba
                name = "%03d.rgba" % len(done)
                with open(os.path.join(dest, name), "wb") as f:
                    f.write(srgba)
                done[shown] = (name, sw, sh)
        except (ValueError, zlib.error, OSError) as e:
            print("hud_pack: skipped %s (%s)" % (rel, e))
            continue
        name, sw, sh = done[shown]
        for s in SCALES:
            if w % s or h % s or w // s < 8 or h // s < 8:
                continue
            pname = "%03d_p%d.rgba" % (n, s)
            with open(os.path.join(dest, pname), "wb") as f:
                f.write(probe(w, h, rgba, s))
            lines.append("%d %d %d %d %s %s %s" % (w // s, h // s, sw, sh, pname, name, rel))
        print("  %s" % rel, flush=True)
    with open(os.path.join(dest, "index.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("hud_pack: %d images, %d probes in %s" % (len(done), len(lines), dest))
    print("In the game: F2 > Graphics > HD HUD textures.")


def status(_):
    index = os.path.join(out_dir(), "index.txt")
    if not os.path.isfile(index):
        print("hud_pack: none installed (%s)" % out_dir())
        return
    with open(index, encoding="utf-8") as f:
        lines = [l for l in f if l.strip()]
    print("hud_pack: %d probes of %d images in %s" % (len(lines), len({l.split()[5] for l in lines}), out_dir()))


def remove(_):
    if os.path.isdir(out_dir()):
        shutil.rmtree(out_dir())
    print("hud_pack: removed (%s)" % out_dir())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("install", help="convert a pack's HUD images into the data folder")
    p.add_argument("pack")
    p.add_argument("--icons", choices=("pack", "original"), default="pack")
    p.add_argument("--maps", action="store_true", help="the radar's map tiles too")
    p.set_defaults(fn=install)
    sub.add_parser("status").set_defaults(fn=status)
    sub.add_parser("remove").set_defaults(fn=remove)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
