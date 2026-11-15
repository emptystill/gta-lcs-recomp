#!/usr/bin/env python3
"""A minimal MIPS (R5900) listing of an address range of a PS2 executable: enough to place instruction patches and
read call sites (lui / ori / addiu / loads / stores / jal / j / jr / branches / COP1 compares; the rest as raw words).
Standard library only (tools/eedis.py needs capstone).

    python tools/mipsdis.py ELF 0x2B4CF8 0x2B4F00

Analysis only: prints addresses and instructions to the terminal, never into the repository.
"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import xmatch  # noqa: E402

R = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
     "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
IMM = {0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu", 0x0C: "andi", 0x0D: "ori", 0x0E: "xori",
       0x19: "daddiu"}
MEM = {0x20: "lb", 0x21: "lh", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x27: "lwu", 0x28: "sb", 0x29: "sh", 0x2B: "sw",
       0x37: "ld", 0x3F: "sd", 0x31: "lwc1", 0x39: "swc1", 0x1E: "lq", 0x1F: "sq"}
BR = {0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz", 0x14: "beql", 0x15: "bnel"}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def dis(pc, w):
    op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    imm = w & 0xFFFF
    if w == 0:
        return "nop"
    if op == 0x0F:
        return "lui %s, 0x%04x" % (R[rt], imm)
    if op in (2, 3):
        return "%s 0x%06x" % ("jal" if op == 3 else "j", (w & 0x3FFFFFF) << 2)
    if op in IMM:
        return "%s %s, %s, %d" % (IMM[op], R[rt], R[rs], s16(imm) if op in (8, 9, 0x19) else imm)
    if op in MEM:
        reg = ("f%d" % rt) if op in (0x31, 0x39) else R[rt]
        return "%s %s, %d(%s)" % (MEM[op], reg, s16(imm), R[rs])
    if op in BR:
        return "%s %s, %s, 0x%06x" % (BR[op], R[rs], R[rt], pc + 4 + s16(imm) * 4)
    if op == 0x01:
        return "regimm rt=%d %s, 0x%06x" % (rt, R[rs], pc + 4 + s16(imm) * 4)
    if op == 0:
        fn = w & 63
        names = {0x08: "jr", 0x09: "jalr", 0x21: "addu", 0x2D: "daddu", 0x25: "or", 0x24: "and", 0x23: "subu",
                 0x2A: "slt", 0x2B: "sltu", 0x00: "sll", 0x02: "srl", 0x03: "sra", 0x0C: "syscall"}
        if fn in (0x08, 0x09):
            return "%s %s" % (names[fn], R[rs])
        if fn in (0, 2, 3):
            return "%s %s, %s, %d" % (names[fn], R[rd], R[rt], (w >> 6) & 31)
        return "%s %s, %s, %s" % (names.get(fn, "special.%02x" % fn), R[rd], R[rs], R[rt])
    if op == 0x11:
        fmt = rs
        if fmt == 0x04:
            return "mtc1 %s, f%d" % (R[rt], rd)
        if fmt == 0x00:
            return "mfc1 %s, f%d" % (R[rt], rd)
        if fmt == 0x08:
            return "bc1%s 0x%06x" % ("t" if rt & 1 else "f", pc + 4 + s16(imm) * 4)
        if fmt == 0x10:
            fn, ft, fs, fd = w & 63, rt, rd, (w >> 6) & 31
            names = {0: "add.s", 1: "sub.s", 2: "mul.s", 3: "div.s", 6: "mov.s", 0x24: "cvt.w.s", 0x28: "max.s",
                     0x29: "min.s", 0x30: "c.f.s", 0x32: "c.eq.s", 0x34: "c.lt.s", 0x36: "c.le.s"}
            return "%s f%d, f%d, f%d" % (names.get(fn, "cop1.s.%02x" % fn), fd, fs, ft)
        if fmt == 0x14:
            return "cvt.s.w f%d, f%d" % ((w >> 6) & 31, rd)
    return ".word 0x%08x" % w


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    elf, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    words = xmatch.words_at(xmatch.load_code(elf), lo, hi)
    if words is None:
        sys.exit("range outside the code")
    for i, w in enumerate(words):
        pc = lo + i * 4
        print("%08x: %08x  %s" % (pc, w, dis(pc, w)))


if __name__ == "__main__":
    main()
