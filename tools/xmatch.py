#!/usr/bin/env python3
"""Find functions of one PS2 executable in another by their code (VCS -> LCS: the same engine and the same Sony SDK).

Each function of the first executable (named in its function map) is turned into a signature: its instruction words
with whatever depends on where things were linked masked out - jump / call targets (j, jal), `lui` high halves, the
immediate of loads, stores and addiu / ori that do not go through the stack pointer (the low halves of absolute
addresses, $gp-relative offsets). Branches are relative and stay. The signature is then searched for in the second
executable's code. A match that is unique is the same function; several matches are listed (short or generic code).

    python tools/xmatch.py FROM.elf FROM_MAP.csv TO.elf 0x46CD10 [0x... ...]   # given functions
    python tools/xmatch.py FROM.elf FROM_MAP.csv TO.elf --range 0x443000 0x480000 [--csv OUT]   # every function there

FROM_MAP is a function map as game/<game>/functions.csv (Name,Start,End,Size). Only analysis output: names and
addresses, never code (OUT goes under build/analysis/).
"""
import argparse
import csv
import struct
import sys

STACK = 29
MASKED_IMM = {0x08, 0x09, 0x0D, 0x18, 0x19, 0x1A, 0x1B, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
              0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x31, 0x35, 0x36, 0x37, 0x39, 0x3D, 0x3E, 0x3F}


def load_code(path):
    """The executable's loadable segments: [(vaddr, bytes)]."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        sys.exit("%s: not an ELF file" % path)
    phoff = struct.unpack_from("<I", d, 0x1C)[0]
    phentsize, phnum = struct.unpack_from("<HH", d, 0x2A)
    segs = []
    for i in range(phnum):
        ptype, off, vaddr, _, filesz, _, flags, _ = struct.unpack_from("<8I", d, phoff + i * phentsize)
        if ptype == 1 and filesz:
            segs.append((vaddr, d[off:off + filesz], bool(flags & 1)))
    return segs


def words_at(segs, start, end):
    for vaddr, data, _ in segs:
        if vaddr <= start and end <= vaddr + len(data):
            o = start - vaddr
            return list(struct.unpack_from("<%dI" % ((end - start) // 4), data, o))
    return None


def mask(w):
    op = w >> 26
    if op in (2, 3):  # j, jal
        return w & 0xFC000000
    if op == 0x0F:  # lui
        return w & 0xFFFF0000
    if op in MASKED_IMM and (w >> 21) & 31 != STACK:
        return w & 0xFFFF0000
    return w


def read_map(path):
    funcs = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            funcs.append((row["Name"], int(row["Start"], 16), int(row["End"], 16)))
    return funcs


class Target:
    def __init__(self, segs, key_words=4):
        self.k = key_words
        self.index = {}
        self.code = []  # (vaddr, masked words)
        for vaddr, data, executable in segs:
            if not executable:
                continue
            m = [mask(w) for w in struct.unpack_from("<%dI" % (len(data) // 4), data)]
            ci = len(self.code)
            self.code.append((vaddr, m))
            for i in range(len(m) - self.k + 1):
                self.index.setdefault(tuple(m[i:i + self.k]), []).append((ci, i))

    def find(self, sig):
        if len(sig) < self.k:
            return []
        hits = []
        for ci, i in self.index.get(tuple(sig[:self.k]), []):
            vaddr, m = self.code[ci]
            if m[i:i + len(sig)] == sig:
                hits.append(vaddr + i * 4)
        return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("from_elf")
    ap.add_argument("from_map")
    ap.add_argument("to_elf")
    ap.add_argument("addresses", nargs="*", help="function starts in FROM (hex)")
    ap.add_argument("--range", nargs=2, metavar=("LO", "HI"), help="every mapped function starting in [LO, HI)")
    ap.add_argument("--min-words", type=int, default=6, help="shorter functions are reported as too short (default 6)")
    ap.add_argument("--csv", help="write from_name,from_addr,size,to_addr,matches to this file")
    a = ap.parse_args()

    src = load_code(a.from_elf)
    funcs = read_map(a.from_map)
    by_start = {s: (n, s, e) for n, s, e in funcs}
    if a.range:
        lo, hi = int(a.range[0], 16), int(a.range[1], 16)
        chosen = [f for f in funcs if lo <= f[1] < hi]
    else:
        chosen = []
        for x in a.addresses:
            v = int(x, 16)
            if v not in by_start:
                sys.exit("0x%08X is not a function start in %s" % (v, a.from_map))
            chosen.append(by_start[v])
    target = Target(load_code(a.to_elf))
    rows, unique = [], 0
    for name, s, e in chosen:
        w = words_at(src, s, e)
        if w is None:
            rows.append((name, s, e - s, None, "outside the code"))
            continue
        if len(w) < a.min_words:
            rows.append((name, s, e - s, None, "too short (%d words)" % len(w)))
            continue
        hits = target.find([mask(x) for x in w])
        if len(hits) == 1:
            unique += 1
        rows.append((name, s, e - s, hits[0] if len(hits) == 1 else None,
                     "unique" if len(hits) == 1 else ("not found" if not hits else
                                                       "%d matches: %s" % (len(hits), " ".join("0x%08X" % h for h in hits[:6])))))
    for name, s, size, to, why in rows:
        print("%-16s 0x%08X %6d -> %s  %s" % (name, s, size, "0x%08X" % to if to else "-" * 10, why))
    print("[xmatch] %d of %d functions found once" % (unique, len(rows)))
    if a.csv:
        with open(a.csv, "w", newline="\n") as f:
            f.write("from_name,from_addr,size,to_addr,result\n")
            for name, s, size, to, why in rows:
                f.write("%s,0x%08X,%d,%s,%s\n" % (name, s, size, "0x%08X" % to if to else "", why.replace(",", ";")))


if __name__ == "__main__":
    main()
