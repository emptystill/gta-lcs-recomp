#!/usr/bin/env python3
"""Where the game compares or clamps its time step against a constant: every load of the step (CTimer::ms_fTimeStep,
through $gp or lui) with a `lui rX, 0x3F00` (0.5) or `0x3F80` (1.0) near it that reaches a c.lt.s / c.le.s / max.s /
min.s - the places that behave differently above 100 / 50 fps (docs/vcs-reference/game-internals.md 4: the VCS port's
patches in game/vcs/overrides.toml are such places).

    python tools/stepscan.py ELF STEP_ADDR GP [--map functions.csv] [--around 12]

Analysis only.
"""
import argparse
import bisect
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xglobal  # noqa: E402
import mipsdis  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("step", type=lambda v: int(v, 16))
    ap.add_argument("gp", type=lambda v: int(v, 16))
    ap.add_argument("--map")
    ap.add_argument("--around", type=int, default=12)
    a = ap.parse_args()
    code = xglobal.Code(a.elf)
    code.gp = a.gp
    funcs = []
    if a.map:
        funcs = sorted((int(r["Start"], 16), r["Name"]) for r in csv.DictReader(open(a.map)))
    starts = [f[0] for f in funcs]
    seen = set()
    for si, i, j, addr in code.sites(a.step, a.step + 4):
        vaddr, words, _ = code.words[si]
        lo, hi = max(0, j - a.around), min(len(words), j + a.around)
        consts = [k for k in range(lo, hi) if words[k] >> 26 == 0x0F and (words[k] & 0xFFFF) in (0x3F00, 0x3F80)]
        uses = [k for k in range(lo, hi) if words[k] >> 26 == 0x11 and (words[k] >> 21) & 31 == 0x10 and
                (words[k] & 63) in (0x28, 0x29, 0x34, 0x36)]
        for k in consts:
            if not uses:
                continue
            pc = vaddr + k * 4
            if pc in seen:
                continue
            seen.add(pc)
            fn = ""
            if funcs:
                n = bisect.bisect_right(starts, pc) - 1
                fn = "%s+0x%x" % (funcs[n][1], pc - funcs[n][0]) if n >= 0 else ""
            print("0x%08X %s  [%s]  step load at 0x%08X; uses: %s" % (
                pc, mipsdis.dis(pc, words[k]), fn, vaddr + j * 4,
                ", ".join("0x%08X %s" % (vaddr + u * 4, mipsdis.dis(vaddr + u * 4, words[u])) for u in uses)))


if __name__ == "__main__":
    main()
