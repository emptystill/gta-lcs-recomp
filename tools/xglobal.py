#!/usr/bin/env python3
"""Find a VCS global variable's address in LCS (or any executable in another of the same engine) by the code that
uses it.

Every place in FROM's code that forms an address in [ADDR, ADDR + span) - a `lui` followed by an addiu / load / store
on the same register, or a $gp-relative load / store - is taken with a few instructions around it, masked as
tools/xmatch.py masks (link-time addresses out, registers / branches / stack offsets kept). That window is searched for
in TO's code; where it is found once, the address the same instructions form there is read off, less the site's
offset from ADDR. Each site votes; the answer is the address most sites agree on.

    python tools/xglobal.py FROM.elf TO.elf 0x4E4910 [0x... ...] [--span 4] [--window 6] [-v]
    python tools/xglobal.py --vcs-lcs 0x4E4910 ...        # the two executables of this setup (VCS's from its folder)

Analysis only: prints addresses, never code.
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmatch  # noqa: E402

GP = 28
LOAD_STORE_ADDIU = {0x08, 0x09, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2B, 0x31, 0x35, 0x37, 0x39, 0x3D,
                    0x3F, 0x1E, 0x1F, 0x1A, 0x1B, 0x2C, 0x2D, 0x22, 0x26, 0x2A, 0x2E}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class Code:
    def __init__(self, path):
        self.segs = [(v, d) for v, d, x in xmatch.load_code(path) if x]
        self.words = []  # (vaddr, words, masked)
        for vaddr, data in self.segs:
            w = list(struct.unpack_from("<%dI" % (len(data) // 4), data))
            self.words.append((vaddr, w, [xmatch.mask(x) for x in w]))
        self.gp = self.find_gp()

    def find_gp(self):
        """$gp from the startup code: lui gp, hi / addiu (or ori) gp, gp, lo - the first such pair."""
        for vaddr, w, _ in self.words:
            for i in range(len(w) - 4):
                if w[i] >> 26 == 0x0F and (w[i] >> 16) & 31 == GP:
                    for j in range(i + 1, i + 4):
                        op, rs, rt = w[j] >> 26, (w[j] >> 21) & 31, (w[j] >> 16) & 31
                        if rs == GP and rt == GP and op in (0x09, 0x0D):
                            lo = s16(w[j] & 0xFFFF) if op == 0x09 else w[j] & 0xFFFF
                            return ((w[i] & 0xFFFF) << 16) + lo & 0xFFFFFFFF
        return None

    def sites(self, lo, hi):
        """(segment index, index of the first instruction, index of the last, address formed) for [lo, hi)."""
        out = []
        for si, (vaddr, w, _) in enumerate(self.words):
            n = len(w)
            for i in range(n):
                x = w[i]
                op = x >> 26
                if op == 0x0F:  # lui rt, hi: follow rt for a few instructions
                    rt = (x >> 16) & 31
                    hi16 = (x & 0xFFFF) << 16
                    for j in range(i + 1, min(i + 10, n)):
                        y = w[j]
                        yop, yrs, yrt = y >> 26, (y >> 21) & 31, (y >> 16) & 31
                        if yop in LOAD_STORE_ADDIU and yrs == rt:
                            a = (hi16 + (s16(y & 0xFFFF))) & 0xFFFFFFFF
                            if yop == 0x0D:
                                a = hi16 | (y & 0xFFFF)
                            if lo <= a < hi:
                                out.append((si, i, j, a))
                        # rt overwritten (not as the base of a store): stop following it
                        if yop not in (0x28, 0x29, 0x2B, 0x39, 0x3D, 0x3F, 0x1F, 0x2C, 0x2D, 0x2A, 0x2E) and yop != 0 and yrt == rt:
                            break
                        if yop == 0 and ((y >> 11) & 31) == rt:
                            break
                elif self.gp is not None and op in LOAD_STORE_ADDIU and (x >> 21) & 31 == GP:
                    a = (self.gp + s16(x & 0xFFFF)) & 0xFFFFFFFF
                    if lo <= a < hi:
                        out.append((si, i, i, a))
        return out

    def address_at(self, si, i, j):
        """The address the instruction pair at (i, j) forms (j alone for $gp)."""
        _, w, _ = self.words[si]
        y = w[j]
        if i == j:
            return (self.gp + s16(y & 0xFFFF)) & 0xFFFFFFFF if self.gp is not None else None
        if w[i] >> 26 != 0x0F:
            return None
        hi16 = (w[i] & 0xFFFF) << 16
        return hi16 | (y & 0xFFFF) if y >> 26 == 0x0D else (hi16 + s16(y & 0xFFFF)) & 0xFFFFFFFF


class Index:
    def __init__(self, code, k=4):
        self.code, self.k = code, k
        self.idx = collections.defaultdict(list)
        for si, (_, _, m) in enumerate(code.words):
            for i in range(len(m) - k + 1):
                self.idx[tuple(m[i:i + k])].append((si, i))

    def find(self, sig):
        hits = []
        for si, i in self.idx.get(tuple(sig[:self.k]), []):
            m = self.code.words[si][2]
            if m[i:i + len(sig)] == sig:
                hits.append((si, i))
        return hits


def port(src, dst, index, addr, span, windows, verbose):
    votes = collections.Counter()
    sites = src.sites(addr, addr + span)
    for si, i, j, a in sites:
        vaddr, _, m = src.words[si]
        found = None
        for w in windows:
            lo, hi = max(0, i - w), min(len(m), j + w + 1)
            hits = index.find(m[lo:hi])
            if len(hits) == 1:
                found = (hits[0], lo)
                break
            if not hits:
                continue  # a wider window will not be found either; try a narrower one
        if not found:
            if verbose:
                print("    site 0x%08X: no unique match" % (vaddr + i * 4))
            continue
        (dsi, dlo), lo = found
        b = dst.address_at(dsi, dlo + (i - lo), dlo + (j - lo))
        if b is None:
            continue
        guess = (b - (a - addr)) & 0xFFFFFFFF
        votes[guess] += 1
        if verbose:
            print("    site 0x%08X -> 0x%08X: 0x%08X" % (vaddr + i * 4, dst.words[dsi][0] + (dlo + i - lo) * 4, guess))
    return sites, votes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("args", nargs="+", help="FROM.elf TO.elf ADDR... (or ADDR... with --vcs-lcs)")
    ap.add_argument("--vcs-lcs", action="store_true", help="FROM = the VCS port's ELF, TO = this tree's")
    ap.add_argument("--span", type=lambda v: int(v, 0), default=4, help="bytes from ADDR a site may address (default 4)")
    ap.add_argument("--window", default="12,8,6,4", help="instructions around a site, tried in turn")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    if a.vcs_lcs:
        import toolchain as tc
        here = os.path.join(tc.ROOT, "build", "elf", tc.ELF_NAME)
        vcs = os.environ.get("VCS_ELF") or os.path.join(os.path.dirname(tc.ROOT), "GTA Vice City Stories", "build",
                                                       "elf", "SLUS_215.90")
        frm, to, addrs = vcs, here, a.args
    else:
        frm, to, addrs = a.args[0], a.args[1], a.args[2:]
    src, dst = Code(frm), Code(to)
    print("[xglobal] gp: from 0x%08X, to 0x%08X" % (src.gp or 0, dst.gp or 0))
    index = Index(dst)
    windows = [int(x) for x in a.window.split(",")]
    for x in addrs:
        addr = int(x, 16)
        sites, votes = port(src, dst, index, addr, a.span, windows, a.v)
        best = votes.most_common(3)
        tally = ", ".join("0x%08X x%d" % (g, n) for g, n in best) or "-"
        print("0x%08X: %d sites, %d matched -> %s" % (addr, len(sites), sum(votes.values()), tally))


if __name__ == "__main__":
    main()
