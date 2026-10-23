#!/usr/bin/env python3
"""THE BLOCK DECODER PART on the host - SPEC.md 98.3.23.

    make && python3 tests/vblkemu.py [--seed N] [--rounds N]

apps/video/vblk.asm (build/vblk.bin, part 0 of VIDEO.O88) run under
Unicorn's x86 core, against a model of what it writes to: a linear
framebuffer for LIN320, and four planes behind the Sequencer's Map Mask and
the Graphics Controller's write mode and Set/Reset for MODEX and VGA4. The
blocks are RANDOM - every kind, colours, masks, levels, segments both
skip-coded and absolute, an origin of 0 and one that is not - and the
picture each round leaves must be tools/os88vid.py's decode of the same
blocks (vga4_block, v8_block, blk_rows), to the byte, in every plane.

EACH ROUND RUNS EVERY PATH THE PLAYER CAN TAKE: straight onto the screen
(write mode 3 on a VGA; a plane at a time, write mode 0, on an EGA) and
round the text box, where the part hands each byte to the player's callback
- here a stub that does what vp_bcb does to a screen with no box on it, and
then SCRIBBLES AX, BX, CX and DX, which is the callback's contract, so a
part that kept something in them fails here and not on a machine.

It also holds the part to the rest of its contract: VBV_SET answers
VB_PROBE, a vector returns SI past its lists with CF = 0 and BP, DS and ES
kept, and a lead it does not know is CF = 1.

`--pm` runs the PROTECTED-MODE assembly (build/vblkpm.bin, part 1, SPEC.md
98.3.23.2) the way kern_mega calls it: CS is a SECOND segment over the same
bytes - a code alias - and VBV_SET's ES the part's own data segment. Every
byte written through either is mirrored to the other, so the part reads
back what it stored exactly as one memory under two selectors does; and a
write that lands through the ALIAS is a write through CS, which on the
machine is a #GP the kernel completes, so it FAILS the row. The same random
blocks then must make the same pictures.

A seconds-long row, and an exact one: what it cannot see is the player
around the part - the fetch, the bracket, the dispatch - which is
tests/vidv2.py's. Needs unicorn (`python3 -m pip install unicorn`); a host
without it SKIPS.
"""
import argparse
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                               # noqa: E402

try:
    from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE,
                         UC_HOOK_INSN, UC_HOOK_MEM_WRITE)
    from unicorn import x86_const as X
    HAVE = True
except ImportError:                                  # pragma: no cover
    HAVE = False

PART, DSEG, SSEG, STUB, CBSEG = 0x2000, 0x3000, 0x4000, 0x0050, 0x0060
ALIAS = 0x2800                          # --pm: CS, the code alias
VSEG = 0xA000
VB_PROBE = 0x5642 ^ 0x2E19
VBV_SET, VBV_LIN, VBV_PLN, VBV_ONE = 0, 1, 2, 3
VBM_NOWM3, VBM_VGA4 = 1, 2


class Vga:
    """The four planes and the registers a block writes through"""

    def __init__(self, linear):
        self.linear = linear
        self.pl = [bytearray(65536) for _ in range(4)]
        self.mm, self.mode, self.sr = 0x0F, 0, 0
        self.seq = self.gc = 0

    def out(self, port, size, value):
        writes = [(port, value & 0xFF)]
        if size == 2:
            writes.append((port + 1, value >> 8))
        for p, v in writes:
            if p == 0x3C4:
                self.seq = v
            elif p == 0x3C5 and self.seq == 2:
                self.mm = v & 0x0F
            elif p == 0x3CE:
                self.gc = v
            elif p == 0x3CF:
                if self.gc == 0:
                    self.sr = v & 0x0F
                elif self.gc == 5:
                    self.mode = v & 3

    def write(self, off, v):
        if self.linear:
            self.pl[0][off] = v
            return
        for p in range(4):
            if not self.mm >> p & 1:
                continue
            if self.mode == 3:          # write mode 3: the byte selects
                s = 0xFF if self.sr >> p & 1 else 0     # Set/Reset's bits
                self.pl[p][off] = (s & v) | (self.pl[p][off] & ~v & 0xFF)
            else:
                self.pl[p][off] = v


class Part:
    def __init__(self, path, pm=False):
        self.code = open(path, "rb").read()   # (written back after each
                                              # call: VBV_SET patches it)
        assert self.code[:2] == b"VB", "not a block part"
        assert len(self.code) <= (ALIAS - PART) * 16, "the alias overlaps"
        self.vec = struct.unpack_from("<4H", self.code, 4)
        self.pm = pm
        self.cswrites = set()                 # --pm: IPs that stored via CS

    def run(self, vga, vector, ax=0, bx=0, cx=0, dx=0, si=0, bp=0,
            data=b"", wm3_reads=False, cb=None):
        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        uc.mem_write(PART * 16, self.code)
        if self.pm:
            uc.mem_write(ALIAS * 16, self.code)
        uc.mem_write(DSEG * 16, data)
        uc.mem_write(STUB * 16, b"\xF4")            # hlt: the return lands
        uc.mem_write(CBSEG * 16, b"\xCB")           # retf: the callback
        if wm3_reads:                               # read mode 1, CDC 0
            uc.mem_write(VSEG * 16, b"\xFF" * 65536)
        else:
            uc.mem_write(VSEG * 16, bytes(vga.pl[0]))
        sp = 0xFFF0
        uc.mem_write(SSEG * 16 + sp - 4, struct.pack("<HH", 0, STUB))
        sp -= 4
        for r, v in ((X.UC_X86_REG_AX, ax), (X.UC_X86_REG_BX, bx),
                     (X.UC_X86_REG_CX, cx), (X.UC_X86_REG_DX, dx),
                     (X.UC_X86_REG_SI, si), (X.UC_X86_REG_DI, 0x1234),
                     (X.UC_X86_REG_BP, bp), (X.UC_X86_REG_DS, DSEG),
                     (X.UC_X86_REG_ES, PART if vector == VBV_SET else VSEG),
                     (X.UC_X86_REG_SS, SSEG), (X.UC_X86_REG_SP, sp),
                     (X.UC_X86_REG_CS, ALIAS if self.pm else PART),
                     (X.UC_X86_REG_IP, self.vec[vector])):
            uc.reg_write(r, v)
        pend = []

        n = len(self.code)

        def on_write(u, access, addr, size, value, user):
            if self.pm:                             # one memory, two names
                for a, b in ((PART, ALIAS), (ALIAS, PART)):
                    if a * 16 <= addr < a * 16 + n:
                        if a == ALIAS:              # ...and this is CS's
                            self.cswrites.add(u.reg_read(X.UC_X86_REG_IP))
                        u.mem_write(addr - a * 16 + b * 16,
                                    (value & ((1 << 8 * size) - 1)).to_bytes(
                                        size, "little"))
                        return
            if not VSEG * 16 <= addr < VSEG * 16 + 65536:
                return
            for i in range(size):
                off = addr - VSEG * 16 + i
                vga.write(off & 0xFFFF, value >> (8 * i) & 0xFF)
                if wm3_reads:
                    pend.append(addr + i)

        def on_code(u, addr, size, user):
            while pend:                             # the reads stay FFh
                u.mem_write(pend.pop(), b"\xFF")
            if addr == CBSEG * 16 and cb is not None:
                a = u.reg_read(X.UC_X86_REG_AX)
                di = u.reg_read(X.UC_X86_REG_DI)
                cb(a & 0xFF, a >> 8, di)
                for r in (X.UC_X86_REG_AX, X.UC_X86_REG_BX,
                          X.UC_X86_REG_CX, X.UC_X86_REG_DX):
                    u.reg_write(r, 0xDEAD)          # the contract: clobbered

        uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
        uc.hook_add(UC_HOOK_CODE, on_code)
        uc.hook_add(UC_HOOK_INSN, lambda u, port, size, value, user:
                    vga.out(port, size, value), None, 1, 0,
                    X.UC_X86_INS_OUT)
        uc.emu_start((ALIAS if self.pm else PART) * 16 + self.vec[vector],
                     STUB * 16, count=20_000_000)
        self.code = bytes(uc.mem_read(PART * 16, len(self.code)))
        regs = {n: uc.reg_read(getattr(X, "UC_X86_REG_" + n))
                for n in ("AX", "SI", "DI", "BP", "DS", "ES", "FLAGS", "SP",
                          "CS", "IP")}
        return regs


def cbset(part, vga, step, mode, banks=1):
    r = part.run(vga, VBV_SET, ax=step, bx=mode | banks << 8, cx=CBSEG,
                 dx=0)
    assert r["AX"] == VB_PROBE, "VBV_SET answered %04x" % r["AX"]
    assert r["DI"] == 0x1234, "VBV_SET spent DI, where the player's door " \
        "holds the rate hook's address"


def blocks_v8(rng, g, n):
    """n random VGA8 blocks on g's canvas, no two overlapping"""
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4)
            for x in range(0, g.wb - 3, 4 if g.layout == vid.LAY_LIN320 else 1)]
    out = []
    for a in rng.sample(tops, min(n, len(tops))):
        k = rng.choice(("fill8", "two8", "eight8"))
        if k == "fill8":
            pay = bytes([rng.randrange(256)])
        elif k == "two8":
            pay = struct.pack("<H", rng.randrange(65536)) + bytes(
                rng.randrange(256) for _ in range(2))
        else:
            pay = struct.pack("<H", rng.randrange(65536)) + bytes(
                rng.randrange(256) for _ in range(8))
        out.append((a, pay, k))
    return out


def blocks_v4(rng, g, n):
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4) for x in range(g.wb)]
    out = []
    for a in rng.sample(tops, min(n, len(tops))):
        if rng.random() < 0.5:
            pay = bytes([rng.randrange(256), rng.randrange(17)])
            out.append((a, pay, vid.BLK_PFILL))
        else:
            pay = bytes(rng.randrange(256) for _ in range(5))
            out.append((a, pay, vid.BLK_TWO))
    return out


def expect(g, bl, org, planar):
    """os88vid's picture of the blocks: plane bytes at each address"""
    pl = [bytearray(65536) for _ in range(4)]
    mx = {"fill8": "mfill", "two8": "mtwo", "eight8": "meight"}
    for a, pay, k in bl:
        rows = vid.blk_rows(g, a)
        if planar == 2:
            for r, pb in zip(rows, vid.vga4_block(k, pay)):
                for p in range(4):
                    pl[p][(r + org) & 0xFFFF] = pb[p]
            continue
        px = vid.v8_block(k if planar == 0 else mx[k], pay)
        for r, ra in enumerate(rows):
            for x in range(4):
                c = px[4 * r + x]
                if planar == 0:
                    pl[0][(ra + org + x) & 0xFFFF] = c
                else:
                    pl[x][(ra + org) & 0xFFFF] = c
    return pl


def one(part, rng, kind, path, org):
    """kind: lin320 / modex / vga4; path: screen / ega / box"""
    if kind == "lin320":
        g = vid.Geom(vid.LAY_LIN320, 320, 200)
        bl = blocks_v8(rng, g, rng.randrange(1, 200))
        data = vid.v2_lists(bl, g) + b"\xAA\x55"
        planar, mode = 0, 0
    elif kind == "modex":
        g = vid.Geom(vid.LAY_MODEX, 80, 240)
        bl = blocks_v8(rng, g, rng.randrange(1, 300))
        lead = rng.choice((0x20, 0x21, 0x22))
        k = vid.MODEX_LEADS[lead]
        back = {"mfill": "fill8", "mtwo": "two8", "meight": "eight8"}
        bl = [(a, p, kk) for a, p, kk in bl if kk == back[k]]
        if not bl:
            return None
        data = vid.to_blocks([(a, p) for a, p, kk in bl], 1) + b"\xAA\x55"
        planar, mode = 1, 0
    else:
        g = vid.Geom(vid.LAY_LIN80, 80, 480, bitplanes=True)
        bl = blocks_v4(rng, g, rng.randrange(1, 300))
        lead = rng.choice((vid.BLK_PFILL, vid.BLK_TWO))
        bl = [(a, p, k) for a, p, k in bl if k == lead]
        if not bl:
            return None
        data = vid.to_blocks([(a, p) for a, p, k in bl], 1) + b"\xAA\x55"
        planar, mode = 2, VBM_VGA4 | (VBM_NOWM3 if path == "ega" else 0)
    want = expect(g, bl, org, planar)
    vga = Vga(planar == 0)
    step = g.base[1] - g.base[0]
    cbset(part, vga, step, mode)
    box = path == "box"
    if planar == 2 and not box and path != "ega":
        vga.mode = 3                    # vp_m3set's write mode 3

    def cb(al, ah, di):                 # vp_bcb, onto a screen with no box
        if planar == 0:
            vga.write(di, al)
        elif planar == 1:
            for p in range(4):
                if ah >> p & 1:
                    vga.pl[p][di] = al
        else:                           # vc_m3b: AL a mask, AH its colour
            for p in range(4):
                s = 0xFF if ah >> p & 1 else 0
                vga.pl[p][di] = (s & al) | (vga.pl[p][di] & ~al & 0xFF)

    if planar == 0:
        r = part.run(vga, VBV_LIN, ax=0x100 if box else 0, si=0, bp=org,
                     data=data, cb=cb)
    else:
        r = part.run(vga, VBV_PLN, ax=(0x100 if box else 0) | lead, si=0,
                     bp=org, data=data, cb=cb,
                     wm3_reads=planar == 2 and not box and path != "ega")
    if r["FLAGS"] & 1:
        return "%s %s: CF = 1 on a known lead" % (kind, path)
    if r["SI"] != len(data) - 2:
        return "%s %s: SI %d after the lists, not %d" % (kind, path, r["SI"],
                                                         len(data) - 2)
    if (r["BP"], r["DS"], r["ES"]) != (org, DSEG, VSEG):
        return "%s %s: BP/DS/ES came back %04x/%04x/%04x" % (
            kind, path, r["BP"], r["DS"], r["ES"])
    if part.cswrites:
        return "%s %s: wrote through CS at %s" % (
            kind, path, ", ".join("%04x" % ip for ip in sorted(
                part.cswrites)))
    for p in range(1 if planar == 0 else 4):
        if vga.pl[p] != want[p]:
            bad = next(i for i in range(65536) if vga.pl[p][i] != want[p][i])
            return ("%s %s origin %d: plane %d byte %04x is %02x, not %02x "
                    "(%d blocks)" % (kind, path, org, p, bad, vga.pl[p][bad],
                                     want[p][bad], len(bl)))
    return ""


def one_bit(part, rng, kind, path, org):
    """ONE BIT (98.1.10): a whole codebook and tiles, then a part of one and
    tiles, the second call's tiles reading entries from both - CGA's two
    banks or Hercules' four, onto the screen or round the box"""
    g = vid.Geom(vid.LAY_CGA, 80, 200) if kind == "cga" else \
        vid.Geom(vid.LAY_HERC, 90, 348)
    banks = 2 if kind == "cga" else 4
    vga = Vga(True)
    cbset(part, vga, g.wb, 0, banks)
    box = path == "box"
    cb_ = bytearray(rng.randrange(256) for _ in range(2048))
    want = bytearray(65536)
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4)
            for x in range(0, g.wb - 1, 2)]

    def cb(al, ah, di):                 # vp_bcb's one bit: vc_put's byte
        vga.write(di, al)

    for call in range(2):
        if call == 0:
            first, ent = 0, bytes(cb_)
        else:
            first = rng.randrange(256)
            n = rng.randrange(1, 257 - first)
            ent = bytes(rng.randrange(256) for _ in range(8 * n))
            cb_[first * 8:first * 8 + len(ent)] = ent
        tiles = [(a, bytes([rng.randrange(256)]), "tile")
                 for a in rng.sample(tops, rng.randrange(1, 300))]
        for a, pay, _ in tiles:
            for r, ra in enumerate(vid.blk_rows(g, a)):
                for x in range(2):
                    want[(ra + org + x) & 0xFFFF] = cb_[pay[0] * 8 + 2 * r + x]
        data = vid.v2_lists([(first, ent, "cbook")] + tiles, g) + b"\xAA\x55"
        r = part.run(vga, VBV_ONE, ax=0x100 if box else 0, si=0, bp=org,
                     data=data, cb=cb)
        if r["FLAGS"] & 1:
            return "%s %s: CF = 1 on call %d" % (kind, path, call)
        if r["SI"] != len(data) - 2:
            return "%s %s: SI %d after the tiles, not %d" % (
                kind, path, r["SI"], len(data) - 2)
        if (r["BP"], r["DS"], r["ES"]) != (org, DSEG, VSEG):
            return "%s %s: BP/DS/ES came back %04x/%04x/%04x" % (
                kind, path, r["BP"], r["DS"], r["ES"])
        if part.cswrites:
            return "%s %s: wrote through CS at %s" % (
                kind, path, ", ".join("%04x" % ip for ip in sorted(
                    part.cswrites)))
        if vga.pl[0] != want:
            bad = next(i for i in range(65536) if vga.pl[0][i] != want[i])
            return ("%s %s origin %d call %d: byte %04x is %02x, not %02x "
                    "(%d tiles)" % (kind, path, org, call, bad,
                                    vga.pl[0][bad], want[bad], len(tiles)))
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", help="the part (default build/vblk.bin, "
                    "or build/vblkpm.bin with --pm)")
    ap.add_argument("--pm", action="store_true", help="the protected-mode "
                    "assembly, called through a code alias it may not write")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--rounds", type=int, default=12)
    a = ap.parse_args()
    if not HAVE:
        print("vblkemu: SKIP - no unicorn (python3 -m pip install unicorn)")
        return 0
    part = Part(a.part or os.path.join(ROOT, "build", "vblkpm.bin" if a.pm
                                       else "vblk.bin"), a.pm)
    rng = random.Random(a.seed)
    fails, n = [], 0
    for rnd in range(a.rounds):
        for kind, paths in (("lin320", ("screen", "box")),
                            ("modex", ("screen", "box")),
                            ("vga4", ("screen", "ega", "box"))):
            for path in paths:
                for org in (0, 0x150 if kind == "lin320" else 0x2A):
                    seed = rng.randrange(1 << 30)
                    e = one(part, random.Random(seed), kind, path, org)
                    if e is None:
                        continue
                    n += 1
                    if e:
                        fails.append("%s (seed %d)" % (e, seed))
    for rnd in range(a.rounds):         # ONE BIT
        for kind in ("cga", "herc"):
            for path in ("screen", "box"):
                for org in (0, 0x2A):
                    seed = rng.randrange(1 << 30)
                    e = one_bit(part, random.Random(seed), kind, path, org)
                    n += 1
                    if e:
                        fails.append("%s (seed %d)" % (e, seed))
    g = vid.Geom(vid.LAY_CGA, 80, 200)   # ...and what one bit refuses
    vga = Vga(True)
    cbset(part, vga, 80, 0, 2)
    r = part.run(vga, VBV_ONE, data=vid.v2_lists([], g))
    if r["FLAGS"] & 1:
        fails.append("one bit: an empty record with no codebook refused")
    r = part.run(vga, VBV_ONE, data=vid.v2_lists(
        [(0, b"\x00", "tile")], g))
    if not r["FLAGS"] & 1:
        fails.append("one bit: a tile before any codebook was not refused")
    r = part.run(vga, VBV_ONE, data=struct.pack("<HB", 2, 255) + bytes(16)
                 + b"\x00")
    if not r["FLAGS"] & 1:
        fails.append("one bit: a codebook past entry 255 was not refused")
    vga = Vga(False)                    # a lead the part does not know
    cbset(part, vga, 80, VBM_VGA4)
    r = part.run(vga, VBV_PLN, ax=0x22, data=b"\x00")
    if not r["FLAGS"] & 1:
        fails.append("VGA4's lead 22h was not refused")
    cbset(part, vga, 80, 0)
    r = part.run(vga, VBV_PLN, ax=0x23, data=b"\x00")
    if not r["FLAGS"] & 1:
        fails.append("MODEX's lead 23h was not refused")
    if part.cswrites:                   # (the refusals above write too)
        fails.append("wrote through CS at %s" % ", ".join(
            "%04x" % ip for ip in sorted(part.cswrites)))
    for f in fails:
        print("vblkemu: FAIL " + f)
    if fails:
        return 1
    print("vblkemu: ok - %d rounds of random blocks, every kind on every "
          "path, the picture os88vid's to the byte; two unknown leads "
          "and two hostile one-bit records refused%s" % (
              n, "; the protected-mode part, through a code alias it never "
              "wrote" if a.pm else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
