#!/usr/bin/env python3
"""XMEM.DRV's LOADALL transport, without a 286 - SPEC.md 41.13.

    python3 tests/xmla.py [--mode ok|trap|badbios|badcache]

No emulator in this tree runs a 286's LOADALL: MartyPC is an 8088, QEMU and
unicorn are 386-class, and 86Box - whose 286 does model LOADALL (0F 05) and
which tools/os88box.py now drives headless (docs/86BOX.md) - has no row
written against it yet. So this runs the code
AS IT SHIPS - xm_bios, xm_lcopy, xm_lend, xm_lainit and xm_ltrip cut out of
drivers/xmem/xmem.asm, with apps/os88la.inc as XMEM.DRV includes it - under
unicorn, and stands in for the two things the CPU would do:

  - at the LOADALL itself it CHECKS THE TABLE at physical 0x800, field by
    field - IF clear in the flags it runs under and in the table's, TF and
    DF clear, IP the instruction after, SP the stack as it stands, CS's and
    SS's selectors the running ones and their caches selector x 16, the
    MSW, IDTR and GDTR la_capture took, DS's and ES's caches the bases
    asked for, and the 102 bytes that were there SAVED in la_save - records
    DS's and ES's caches, and carries on at the next instruction;
  - at the `rep movsw` inside the borrow (xm_lcopy.mov) it moves the words
    through THOSE CACHES, since unicorn keeps no base above 1 MB.

`int 15h AH=87h` is modelled from its descriptors. Every copy in the matrix -
both directions, 2 bytes to 32 KB, odd conventional addresses, conventional
ends outside the driver's segment - must land exactly, touch nothing either
side, leave the API cells and every register as they were, and move at most
XM_LPIECE (16 KB) a borrow with interrupts off.

Broken on purpose (each its own --mode, run by the default): a CPU on which
LOADALL traps (INT 6), a BIOS whose block move corrupts a word, and a cache
that does not take - each must leave [xm_lall] 0, the cells and INT 6's
vector as they were, and the block table zeroed.

Needs nasm and the `unicorn` Python module.
"""
import os
import random
import re
import struct
import subprocess
import sys
import tempfile

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE,
                     UC_HOOK_INTR, UC_HOOK_INSN)
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_SS, UC_X86_REG_EFLAGS,
                               UC_X86_INS_IN, UC_X86_INS_OUT)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "drivers", "xmem", "xmem.asm")
SEG = 0x1000                    # the harness's CS = DS = ES = SS
BASE = SEG << 4
POOL_HI = 0x0011                # the pool above the HMA: 0x110000
MSW, IDT, GDT = 0xFFF0, (0, 0x3FF), (0x0F4500, 0x0037)
FAILS = []


def check(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def cut(src, start, stop):
    a = src.index(start)
    b = src.index(stop, a)
    return src[a:b]


def assemble(tmp):
    src = open(SRC).read()
    code = cut(src, "\nxm_bios:", "\n; Data that must outlive attach")
    code += '\n%include "os88la.inc"\n' if "os88la.inc" not in code else ""
    equs = cut(src, "\nxm_src          equ", "\n; ---------")
    wrap = os.path.join(tmp, "xlhx.asm")
    mapf = os.path.join(tmp, "xlhx.map")
    with open(wrap, "w") as f:
        f.write("[map symbols %s]\ncpu 8086\nbits 16\norg 0x100\n"
                "XM_MAX_BLKS equ 8\nXM_BLKSZ equ 8\n"
                "e_init:\n    call xm_lainit\n    hlt\n"
                "e_copy:\n    call xm_lcopy\n    hlt\n"
                "xm_a20_reopen:\n    clc\n    ret\n" % mapf)
        f.write(code)
        f.write("\nxm_once:\n" + equs + "\n    times 1024 db 0\n")
    out = os.path.join(tmp, "xlhx.com")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                    os.path.join(ROOT, "apps") + "/", "-I",
                    os.path.join(ROOT, "drivers") + "/", "-o", out, wrap],
                   check=True)
    syms = {}
    for line in open(mapf):
        m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(2), 16)
    return open(out, "rb").read(), syms


class Box:
    def __init__(self, image, syms, mode, rnd):
        self.s, self.mode, self.rnd = syms, mode, rnd
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x200000)
        mu.mem_write(BASE + 0x100, image)
        self.cells = bytes(rnd.randrange(256) for _ in range(0x66))
        mu.mem_write(0x800, self.cells)
        self.ivt6 = struct.pack("<HH", 0xFF53, 0xF000)
        mu.mem_write(6 * 4, self.ivt6)
        self.loadall = syms["la_borrow.resume"] - 2
        self.movsw = syms["xm_lcopy.mov"]
        self.caches = None
        self.borrows = []           # bytes moved under each borrow
        self.errs = []
        mu.hook_add(UC_HOOK_CODE, self.code)
        mu.hook_add(UC_HOOK_INTR, self.intr)
        mu.hook_add(UC_HOOK_INSN, lambda uc, p, sz, ud: 0, None, 1, 0,
                    UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, lambda uc, p, sz, v, ud: None, None, 1, 0,
                    UC_X86_INS_OUT)

    def r(self, reg):
        return self.mu.reg_read(reg)

    def w16(self, lin):
        return struct.unpack("<H", self.mu.mem_read(lin, 2))[0]

    def err(self, what):
        self.errs.append(what)

    def code(self, mu, addr, size, ud):
        ip = addr - BASE
        if ip == self.loadall:
            self.at_loadall()
        elif ip == self.movsw and self.caches is not None:
            self.at_movsw()

    def at_loadall(self):
        mu, s = self.mu, self.s
        fl = self.r(UC_X86_REG_EFLAGS)
        if fl & 0x200:
            self.err("LOADALL reached with interrupts ON")
        if self.mode == "trap":         # an INT 6, as a CPU without it does
            vec = struct.unpack("<HH", mu.mem_read(6 * 4, 4))
            if vec != (s["la_ud"], SEG):
                self.err("INT 6 is not la_ud's at the LOADALL")
            sp = self.r(UC_X86_REG_SP) - 6
            mu.mem_write((SEG << 4) + sp, struct.pack(
                "<HHH", self.loadall, SEG, fl))
            mu.reg_write(UC_X86_REG_SP, sp)
            mu.reg_write(UC_X86_REG_IP, vec[0])
            return
        t = bytes(mu.mem_read(0x800, 0x66))
        save = bytes(mu.mem_read(BASE + s["la_save"], 0x66))
        if save != self.cells:
            self.err("la_save does not hold the API cells")

        def w(o):
            return struct.unpack_from("<H", t, o)[0]

        def cache(o):
            base = t[o] | t[o + 1] << 8 | t[o + 2] << 16
            return base, t[o + 3], w(o + 4)
        want = {
            "MSW": (w(0x06), MSW), "IP": (w(0x1A), s["la_borrow.resume"]),
            "SP": (w(0x2C), self.r(UC_X86_REG_SP)),
            "CS": (w(0x22), SEG), "SS": (w(0x20), SEG),
            "CS cache": (cache(0x3C), (BASE, 0x93, 0xFFFF)),
            "SS cache": (cache(0x42), (BASE, 0x93, 0xFFFF)),
            "IDTR": ((cache(0x5A)[0], w(0x5A + 4)), IDT),
            "GDTR": ((cache(0x4E)[0], w(0x4E + 4)), GDT),
            "FLAGS IF/TF/DF": (w(0x18) & 0x700, 0),
        }
        for k, (got, exp) in want.items():
            if got != exp:
                self.err("table %s %r, want %r" % (k, got, exp))
        for o in (0x2E, 0x30, 0x32, 0x34, 0x26, 0x28, 0x2A, 0x16, 0x1C):
            if w(o):
                self.err("table word %02Xh not 0" % o)
        dsc, esc = cache(0x48), cache(0x36)
        for nm, c in (("DS", dsc), ("ES", esc)):
            if c[1:] != (0x93, 0xFFFF):
                self.err("%s cache access/limit %r" % (nm, c[1:]))
        self.caches = (dsc[0], esc[0], w(0x1E), w(0x24))
        if self.mode == "badcache":
            self.caches = (dsc[0] + 2, esc[0], w(0x1E), w(0x24))
        self.mu.reg_write(UC_X86_REG_IP, self.loadall + 2)
        self.borrows.append(0)

    def at_movsw(self):
        mu = self.mu
        if self.r(UC_X86_REG_EFLAGS) & 0x600:
            self.err("the copy runs with interrupts ON or DF set")
        dsc, esc, dsv, esv = self.caches
        n = self.r(UC_X86_REG_CX)
        si, di = self.r(UC_X86_REG_SI), self.r(UC_X86_REG_DI)
        if si + 2 * n > 0x10000 or di + 2 * n > 0x10000:
            self.err("a piece runs past its 64 KB window")
        # the conventional end must show its own paragraph
        for c, v in ((dsc, dsv), (esc, esv)):
            if c < 0x100000 and c != v << 4:
                self.err("a conventional cache %06X shows selector %04X"
                         % (c, v))
        data = bytes(mu.mem_read(dsc + si, 2 * n))
        mu.mem_write(esc + di, data)
        mu.reg_write(UC_X86_REG_SI, si + 2 * n)
        mu.reg_write(UC_X86_REG_DI, di + 2 * n)
        mu.reg_write(UC_X86_REG_CX, 0)
        mu.reg_write(UC_X86_REG_IP, self.movsw + 2)
        self.borrows[-1] += 2 * n
        self.caches = None

    def intr(self, mu, n, ud):
        if n != 0x15 or self.r(UC_X86_REG_AX) >> 8 != 0x87:
            self.err("interrupt %02Xh" % n)
            mu.emu_stop()
            return
        blk = (self.r(UC_X86_REG_ES) << 4) + self.r(UC_X86_REG_SI)
        d = bytes(mu.mem_read(blk, 0x30))

        def base(o):
            return d[o + 2] | d[o + 3] << 8 | d[o + 4] << 16
        words = self.r(UC_X86_REG_CX)
        data = bytearray(mu.mem_read(base(0x10), 2 * words))
        if self.mode == "badbios" and data:
            data[0] ^= 0x40
        mu.mem_write(base(0x18), bytes(data))
        mu.reg_write(UC_X86_REG_AX, self.r(UC_X86_REG_AX) & 0xFF)
        mu.reg_write(UC_X86_REG_EFLAGS, self.r(UC_X86_REG_EFLAGS) & ~1)

    def go(self, entry, regs=None):
        mu = self.mu
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        for r, v in (regs or {}).items():
            mu.reg_write(r, v)
        mu.emu_start(BASE + self.s[entry], BASE + 0xFFFF, count=20_000_000)

    def b(self, name):
        return self.mu.mem_read(BASE + self.s[name], 1)[0]


def seed_capture(box):
    """la_capture's three 286 instructions run under a 386 as legal ones,
    with the 386's answers - so the box's MSW/IDTR/GDTR are what the table
    is checked against, read back from what la_capture stored"""


def run(mode, image, syms):
    print("xmla: %s" % {"ok": "a 286 whose LOADALL works",
                        "trap": "a CPU on which 0F 05 traps",
                        "badbios": "a BIOS whose block move corrupts a word",
                        "badcache": "a cache that does not take"}[mode])
    rnd = random.Random(41)
    box = Box(image, syms, mode, rnd)
    mu, s = box.mu, syms
    mu.mem_write(BASE + s["xm_base_hi"], struct.pack("<H", POOL_HI))
    box.go("e_init")
    # what la_capture took is the box's own CPU's (unicorn's), so the table
    # must carry exactly those: read them back as the reference
    global MSW, IDT, GDT
    MSW = box.w16(BASE + s["la_msw"])
    idt = bytes(mu.mem_read(BASE + s["la_idt"], 6))
    gdt = bytes(mu.mem_read(BASE + s["la_gdt"], 6))
    IDT = (idt[0] | idt[1] << 8 | idt[2] << 16,
           struct.unpack_from("<H", idt, 4)[0])
    GDT = (gdt[0] | gdt[1] << 8 | gdt[2] << 16,
           struct.unpack_from("<H", gdt, 4)[0])
    if mode == "ok":
        # the init's own table was checked against the zeros before the
        # reference was known: run it again now that it is
        box = Box(image, syms, mode, rnd)
        mu = box.mu
        mu.mem_write(BASE + s["xm_base_hi"], struct.pack("<H", POOL_HI))
        box.go("e_init")
    lall = box.b("xm_lall")
    check(not box.errs, "attach: every LOADALL table right%s"
          % ("" if not box.errs else " - " + "; ".join(sorted(set(box.errs)))))
    check(lall == (1 if mode == "ok" else 0),
          "attach: [xm_lall] %d - the transport %s" % (
              lall, "verified" if mode == "ok" else "REFUSED"))
    check(bytes(mu.mem_read(0x800, 0x66)) == box.cells,
          "attach: the API cells at 0x800 as they were")
    check(bytes(mu.mem_read(24, 4)) == box.ivt6,
          "attach: INT 6's vector as it was")
    tab = bytes(mu.mem_read(BASE + s["xm_tab"], s["xm_tab_end"] - s["xm_tab"]))
    check(not any(tab), "attach: the block table empty again")
    check(box.r(UC_X86_REG_EFLAGS) & 0x200, "attach: interrupts back on")
    if mode != "ok":
        return
    n = 0
    for up in (True, False):
        for size in (2, 24, 8192, 16382, 16384, 16386, 30000, 32768):
            for conv in (0x10000 + 0x3000, 0x34567, 0x9F000 - 32768 - 64):
                for ext in (0x110000, 0x112346):
                    n += 1
                    box.borrows, box.errs = [], []
                    src, dst = (conv, ext) if up else (ext, conv)
                    pre = bytes(rnd.randrange(256) for _ in range(size + 64))
                    mu.mem_write(dst - 32, pre)
                    data = bytes(rnd.randrange(256) for _ in range(size))
                    mu.mem_write(src, data)
                    mu.mem_write(BASE + s["xm_src"],
                                 struct.pack("<I", src))
                    mu.mem_write(BASE + s["xm_dst"],
                                 struct.pack("<I", dst))
                    regs = {UC_X86_REG_AX: 0x1111, UC_X86_REG_BX: 0x2222,
                            UC_X86_REG_CX: 0x3333, UC_X86_REG_DX: 0x4444,
                            UC_X86_REG_SI: 0x5555, UC_X86_REG_DI: 0x6666,
                            UC_X86_REG_BP: size}
                    box.go("e_copy", regs)
                    got = bytes(mu.mem_read(dst - 32, size + 64))
                    bad = []
                    if got[32:32 + size] != data:
                        bad.append("the bytes")
                    if got[:32] != pre[:32] or got[32 + size:] != pre[32 + size:]:
                        bad.append("the bytes either side")
                    if bytes(mu.mem_read(0x800, 0x66)) != box.cells:
                        bad.append("the API cells")
                    for r, v in regs.items():
                        if box.r(r) != v:
                            bad.append("a register")
                    if box.r(UC_X86_REG_DS) != SEG or \
                            box.r(UC_X86_REG_ES) != SEG:
                        bad.append("DS/ES")
                    if not box.r(UC_X86_REG_EFLAGS) & 0x200:
                        bad.append("interrupts left off")
                    if len(box.borrows) != -(-size // 16384) or \
                            max(box.borrows) > 16384:
                        bad.append("pieces %r" % box.borrows)
                    if box.errs:
                        bad += sorted(set(box.errs))
                    if bad:
                        check(False, "%s %d bytes, conventional %05X, "
                              "extended %06X: %s" % (
                                  "up" if up else "down", size, conv, ext,
                                  ", ".join(bad)))
    check(not [f for f in FAILS if " bytes, conventional " in f],
          "%d copies, both ways, 2 B to 32 KB: every byte where it belongs, "
          "nothing either side, the cells, registers and DS/ES as they were, "
          "16 KB a borrow at most" % n)


def main():
    modes = sys.argv[sys.argv.index("--mode") + 1:][:1] \
        if "--mode" in sys.argv else ["ok", "trap", "badbios", "badcache"]
    with tempfile.TemporaryDirectory() as tmp:
        image, syms = assemble(tmp)
        for m in modes:
            run(m, image, syms)
    print("xmla: %s" % ("FAIL (%d)" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
