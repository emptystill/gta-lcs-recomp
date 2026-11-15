#!/usr/bin/env python3
"""Carry instruction patches from one executable to another of the same engine: each patched instruction's
surroundings (masked as tools/xmatch.py masks: link-time addresses out) are searched for in the other executable;
where found once, the patch's address there is printed with the original word at both places (they must agree for
the patch to mean the same thing there).

    python tools/xpatch.py FROM.elf FROM_OVERRIDES.toml TO.elf [--window 16,12,8,6]

Analysis only: addresses and instruction words, never code ranges.
"""
import argparse
import os
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xglobal  # noqa: E402
import mipsdis  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("from_elf")
    ap.add_argument("overrides")
    ap.add_argument("to_elf")
    ap.add_argument("--window", default="16,12,10,8,6")
    a = ap.parse_args()
    src, dst = xglobal.Code(a.from_elf), xglobal.Code(a.to_elf)
    index = xglobal.Index(dst)
    windows = [int(x) for x in a.window.split(",")]
    patches = tomllib.load(open(a.overrides, "rb")).get("patches", [])
    for p in patches:
        addr, value = int(p["address"], 16), int(p["value"], 16)
        found = None
        for si, (vaddr, words, masked) in enumerate(src.words):
            if vaddr <= addr < vaddr + len(words) * 4:
                i = (addr - vaddr) // 4
                orig = words[i]
                for w in windows:
                    lo, hi = max(0, i - w), min(len(masked), i + w + 1)
                    hits = index.find(masked[lo:hi])
                    if len(hits) == 1:
                        dsi, dlo = hits[0]
                        dvaddr, dwords, _ = dst.words[dsi]
                        j = dlo + (i - lo)
                        found = (dvaddr + j * 4, dwords[j], w)
                        break
                break
        if found:
            to, word, w = found
            same = "same" if word == orig else "DIFFERENT"
            print("0x%08X -> 0x%08X  %s (window %d): %s | patch %s" % (
                addr, to, same, w, mipsdis.dis(to, word), mipsdis.dis(to, value)))
        else:
            print("0x%08X -> not found once" % addr)


if __name__ == "__main__":
    main()
