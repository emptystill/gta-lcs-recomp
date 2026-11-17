#!/usr/bin/env python3
"""A high-resolution interface texture pack for the game (docs/LCS.md "HUD texture pack").

    python tools/hud_pack.py install PACK_DIR [PACK_DIR ...] [--icons original|pack] [--no-maps]
    python tools/hud_pack.py status
    python tools/hud_pack.py remove

PACK_DIR is a pack's folder of PNG images - made for PPSSPP (the PSP version: "GTA LCS HD+ Interface", its ULES00151
folder or the one above it). Its images are named by PPSSPP's table of the PSP's texture hashes, which means nothing to
this game, so they are not looked up by name: `install` converts them into <data>/hud_pack - raw RGBA, and for each a
probe: the image box-averaged down to the sizes the game's own texture may have (1/2, 1/4, 1/8) - and the game matches
each texture of its HUD and 2D drawing (menus, the maps, loading screens) to the probes of its size by content when
Graphics > HD HUD textures is on.

Several packs install together, the first one's image winning where two have the same file (by its path in the
pack). A pack made for PCSX2 (the PS2 version: a folder of `<hash>-<hash>-<bits>.png` replacements, such as "Remastered
Frontend PCSX2 [LCS]" for SLUS-21423) names the game texture's size in each file name (the bits: PCSX2's TEX0 PSM,
TW, TH), so its probe is the image box-averaged to exactly that size. A PPSSPP pack's 3D world folders (Buildings,
Vehicles, Signs, World, Props, Pedestrians, Particles) are left out: this draws the interface only.

Everything of the pack is taken but its Extras and Alternative folders (spare variants). --icons original (default): the
weapon icons of its "Original Icons" folder, black and white as the game's; --icons pack: its HUD/weapons, with a coloured
background. --no-maps leaves the map tiles out.

Alpha: the PS2's full opacity is 128 (the GS doubles it), a PPSSPP pack's 255; its images are converted to the PS2's
scale, or blending would take them as twice opaque. A PCSX2 pack's images are in the PS2's scale already.

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
SKIP = ("extras", "alternative - read readme",  # first path part, lower case
        "buildings", "vehicles", "signs", "world", "props", "pedestrians", "particles")  # a PPSSPP pack's 3D textures
ICONS_ORIGINAL = "original icons - replace"
SCALES = (2, 4, 8)
MAX_SIZE = 2048     # the pack's largest images (loading screens)
MAX_PROBE = 1024    # the game's largest texture it reads back to match
GS_ALPHA = bytes((a * 128 + 127) // 255 for a in range(256))


def out_dir():
    return os.path.join(backup.data_dir(), OUT)


# ---- PNG (8 / 16-bit, grey / RGB / palette / grey+alpha / RGBA, non-interlaced) -> RGBA bytes -------------------------
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
        o, e = y * w * 4, (y + 1) * w * 4
        if ct == 6:
            out[o:e] = px
        elif ct == 2:
            out[o:e:4], out[o + 1:e:4], out[o + 2:e:4] = px[0::3], px[1::3], px[2::3]
            out[o + 3:e:4] = b"\xff" * w
        elif ct == 0:
            out[o:e:4] = out[o + 1:e:4] = out[o + 2:e:4] = px
            out[o + 3:e:4] = b"\xff" * w
        elif ct == 4:
            g = px[0::2]
            out[o:e:4] = out[o + 1:e:4] = out[o + 2:e:4] = g
            out[o + 3:e:4] = px[1::2]
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


def to_gs_alpha(rgba):
    rgba[3::4] = bytes(rgba[3::4]).translate(GS_ALPHA)
    return rgba


# Box average by 2, colour weighted by alpha, alpha the mean. Sums carried as (r*a, g*a, b*a, a) per texel so that the
# 4x and 8x levels made from the 2x one are exact. gs_hw.cpp compares the result (RGBA8) against the game's texture.
def half(w, h, sums):
    hw, hh = w // 2, h // 2
    out = [None] * (hw * hh)
    for y in range(hh):
        r0, r1 = (2 * y) * w, (2 * y + 1) * w
        for x in range(hw):
            a, b, c, d = sums[r0 + 2 * x], sums[r0 + 2 * x + 1], sums[r1 + 2 * x], sums[r1 + 2 * x + 1]
            out[y * hw + x] = (a[0] + b[0] + c[0] + d[0], a[1] + b[1] + c[1] + d[1], a[2] + b[2] + c[2] + d[2],
                               a[3] + b[3] + c[3] + d[3])
    return hw, hh, out


def to_bytes(sums, n):
    out = bytearray(len(sums) * 4)
    for i, (r, g, b, a) in enumerate(sums):
        if a:
            out[4 * i:4 * i + 4] = bytes((r // a, g // a, b // a, a // n))
    return out


def probes(w, h, rgba):
    """{scale: RGBA bytes} for the scales whose size fits (both sides divisible, 8..MAX_PROBE)."""
    out = {}
    sums = [(rgba[i] * rgba[i + 3], rgba[i + 1] * rgba[i + 3], rgba[i + 2] * rgba[i + 3], rgba[i + 3])
            for i in range(0, len(rgba), 4)]
    s, cw, chh = 1, w, h
    for scale in SCALES:
        while s < scale:
            if cw % 2 or chh % 2:
                return out
            cw, chh, sums = half(cw, chh, sums)
            s *= 2
        if 8 <= cw <= MAX_PROBE and 8 <= chh <= MAX_PROBE:
            out[scale] = to_bytes(sums, scale * scale)
    return out


def scaled(w, h, rgba, tw, th):
    """The image box-averaged to tw x th (w, h multiples of them), colour weighted by alpha: a PCSX2 pack's probe."""
    fx, fy = w // tw, h // th
    n = fx * fy
    out = bytearray(tw * th * 4)
    for ty in range(th):
        for tx in range(tw):
            r = g = b = a = 0
            for y in range(ty * fy, ty * fy + fy):
                o = (y * w + tx * fx) * 4
                for i in range(o, o + fx * 4, 4):
                    al = rgba[i + 3]
                    r += rgba[i] * al
                    g += rgba[i + 1] * al
                    b += rgba[i + 2] * al
                    a += al
            if a:
                p = (ty * tw + tx) * 4
                out[p:p + 4] = bytes((r // a, g // a, b // a, a // n))
    return out


def stretch(w, h, rgba, nw, nh):
    """Bilinear resize to nw x nh: a PCSX2 image of another shape than its texture (1024x512 for a 512x512 loading
    screen) made the texture's, k times larger on both sides, as the game draws it."""
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        fy = max(0.0, (y + 0.5) * h / nh - 0.5)
        y0 = min(int(fy), h - 1)
        y1, ty = min(y0 + 1, h - 1), fy - int(fy)
        for x in range(nw):
            fx = max(0.0, (x + 0.5) * w / nw - 0.5)
            x0 = min(int(fx), w - 1)
            x1, tx = min(x0 + 1, w - 1), fx - int(fx)
            a, b = (y0 * w + x0) * 4, (y0 * w + x1) * 4
            c, d = (y1 * w + x0) * 4, (y1 * w + x1) * 4
            o = (y * nw + x) * 4
            for k in range(4):
                top = rgba[a + k] + (rgba[b + k] - rgba[a + k]) * tx
                bot = rgba[c + k] + (rgba[d + k] - rgba[c + k]) * tx
                out[o + k] = int(top + (bot - top) * ty + 0.5)
    return out


def pcsx2_size(name):
    """The game texture's size from a PCSX2 replacement's name (<hash>[-<hash>]-<bits>.png), or None."""
    parts = os.path.splitext(name)[0].split("-")
    if len(parts) not in (2, 3) or any(not 1 <= len(p) <= 16 for p in parts):
        return None
    try:
        bits = int(parts[-1], 16)
        int(parts[0], 16)
    except ValueError:
        return None
    return 1 << ((bits >> 6) & 15), 1 << ((bits >> 10) & 15)


def images(pack, icons):
    """(relative path, full path) of the pack's images, the folder that has ULES00151 accepted too."""
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
            low = rel.lower()
            if low.split("/")[0] in SKIP:
                continue
            if " alt" in os.path.basename(low):  # a pack's alternative versions ("HUD_Numbers Alt Star")
                continue
            # the black and white icons: the coloured ones left out, so none of them can win a match
            if icons == "original" and low.startswith("hud/weapons/"):
                continue
            found.append((rel, full))
    return pack, sorted(found)


def install(args):
    found, seen = [], set()
    for one in args.pack:
        for rel, full in images(one, args.icons)[1]:
            if rel.lower() not in seen:  # the first pack's image of a path wins
                seen.add(rel.lower())
                found.append((rel, full))
    if not args.maps:
        found = [(r, f) for r, f in found if not r.lower().startswith("map/")]
    if not found:
        sys.exit("hud_pack: no images in %s (a pack's folder of PNG images: Text, HUD ...)" % ", ".join(args.pack))
    # --icons pack: the pack's own weapon icons (HUD/weapons), recognised through its Original Icons
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
        if w > MAX_SIZE or h > MAX_SIZE:
            continue
        shown = full
        if rel.lower().startswith(ICONS_ORIGINAL + "/") and args.icons == "pack":
            shown = own.get(rel.split("/")[-1].lower(), full)
        # PCSX2's replacements keep the GS's alpha (128 opaque, as its dumps write it): not converted again
        size = pcsx2_size(os.path.basename(full))
        alpha = (lambda b: b) if size else to_gs_alpha
        try:
            w, h, rgba = read_png(full)
            if shown not in done:
                sw, sh, srgba = (w, h, rgba) if shown == full else read_png(shown)
                if (sw, sh) != (w, h):
                    shown, sw, sh, srgba = full, w, h, rgba  # (a different size: the one recognised)
                if size and sw % size[0] == 0 and sh % size[1] == 0 and sw // size[0] != sh // size[1]:
                    k = max(sw // size[0], sh // size[1])
                    srgba = stretch(sw, sh, srgba, size[0] * k, size[1] * k)
                    sw, sh = size[0] * k, size[1] * k
                name = "%03d.rgba" % len(done)
                with open(os.path.join(dest, name), "wb") as f:
                    f.write(alpha(bytearray(srgba)))
                done[shown] = (name, sw, sh)
            if size and w >= size[0] and h >= size[1] and w % size[0] == 0 and h % size[1] == 0 and                     max(size) <= MAX_PROBE:
                # PCSX2: the probe at the texture's own size; the index's scale field is then the width's ratio
                pr = {(w // size[0], h // size[1]): scaled(w, h, bytearray(rgba), *size)}
            else:
                pr = probes(w, h, to_gs_alpha(bytearray(rgba)))
        except (ValueError, zlib.error, OSError) as e:
            print("hud_pack: skipped %s (%s)" % (rel, e))
            continue
        name, sw, sh = done[shown]
        for s, data in sorted(pr.items()):
            sx, sy = s if isinstance(s, tuple) else (s, s)
            pname = "%03d_p%d.rgba" % (n, sx)
            with open(os.path.join(dest, pname), "wb") as f:
                f.write(data)
            lines.append("%d %d %d %d %s %s %s" % (w // sx, h // sy, sw, sh, pname, name, rel))
        print("  [%d/%d] %s" % (n + 1, len(found), rel), flush=True)
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
    p = sub.add_parser("install", help="convert a pack's images into the data folder")
    p.add_argument("pack", nargs="+", help="pack folders, the first one's images first")
    p.add_argument("--icons", choices=("original", "pack"), default="original",
                   help="the weapon icons: the pack's black and white originals (default) or its coloured ones")
    p.add_argument("--no-maps", dest="maps", action="store_false", help="leave the map tiles out")
    p.set_defaults(fn=install)
    sub.add_parser("status").set_defaults(fn=status)
    sub.add_parser("remove").set_defaults(fn=remove)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
