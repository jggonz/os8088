#!/usr/bin/env python3
"""vgaeq - the planar cursor save/restore, base vs tip, on each kernel's own
bytes under unicorn with a 4-plane VGA model (Read Map Select, Map Mask,
write mode 0 / no set-reset / bit mask FF - the state SPEC.md 1 rule 7
guarantees on entry).

    python3 vgaeq.py <base build dir> <tip build dir> [--n N]
"""
import argparse, random, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_INSN
from unicorn.x86_const import *

ap = argparse.ArgumentParser()
ap.add_argument("base")
ap.add_argument("tip")
ap.add_argument("--n", type=int, default=3000)
ap.add_argument("--seed", type=int, default=5)
a = ap.parse_args()
KSEG = 0x60
VB = 0xA0000


def loadmap(bdir):
    syms, secs = {}, {}
    for line in open(bdir + "/kernel.map"):
        p = line.split()
        if len(p) == 3:
            try:
                syms[p[2]] = int(p[1], 16)
            except ValueError:
                pass
        if len(p) >= 6 and p[-1].startswith(".") and p[-2] in ("progbits", "nobits"):
            try:
                secs[p[-1]] = (int(p[0], 16), int(p[1], 16), int(p[3], 16))
            except ValueError:
                pass
    return syms, secs


class K:
    def __init__(self, bdir):
        self.S, secs = loadmap(bdir)
        full = open(bdir + "/kernel-full.bin", "rb").read()
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_16)
        u.mem_map(0, 0x100000)
        for sec in (".text", ".vga"):
            vs, st, ln = secs[sec]
            u.mem_write(KSEG * 16 + vs, full[st:st + ln])
        self.planes = [bytearray(0x10000) for _ in range(4)]
        self.gcidx = self.seqidx = 0
        self.readmap, self.mapmask = 0, 0xF
        self.writes = {}
        u.hook_add(UC_HOOK_MEM_READ, self._r, begin=VB, end=VB + 0xFFFF)
        u.hook_add(UC_HOOK_MEM_WRITE, self._w, begin=VB, end=VB + 0xFFFF)
        u.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)

    def _r(self, uc, acc, addr, size, val, ud):
        for i in range(size):
            o = (addr + i - VB) & 0xFFFF
            uc.mem_write(addr + i, bytes([self.planes[self.readmap][o]]))

    def _w(self, uc, acc, addr, size, val, ud):
        for i in range(size):
            o = (addr + i - VB) & 0xFFFF
            b = (val >> (8 * i)) & 0xFF
            self.writes[o] = self.writes.get(o, 0) + 1
            for p in range(4):
                if self.mapmask & (1 << p):
                    self.planes[p][o] = b

    def _out(self, uc, port, size, value, ud):
        vals = [(port, value & 0xFF)] if size == 1 else [(port, value & 0xFF), (port + 1, value >> 8)]
        for pt, v in vals:
            if pt == 0x3CE:
                self.gcidx = v
            elif pt == 0x3CF:
                if self.gcidx == 4:
                    self.readmap = v & 3
            elif pt == 0x3C4:
                self.seqidx = v
            elif pt == 0x3C5:
                if self.seqidx == 2:
                    self.mapmask = v & 0xF

    def w8(self, n, v): self.u.mem_write(KSEG * 16 + self.S[n], bytes([v & 0xFF]))
    def w16(self, n, v): self.u.mem_write(KSEG * 16 + self.S[n], (v & 0xFFFF).to_bytes(2, "little"))
    def r8(self, n): return self.u.mem_read(KSEG * 16 + self.S[n], 1)[0]
    def r16(self, n): return int.from_bytes(self.u.mem_read(KSEG * 16 + self.S[n], 2), "little")

    def call(self, name):
        u = self.u
        STOP = 0xFFF0
        u.mem_write(KSEG * 16 + STOP, b"\xeb\xfe")
        for r, v in ((UC_X86_REG_CS, KSEG), (UC_X86_REG_DS, KSEG), (UC_X86_REG_ES, 0x4321),
                     (UC_X86_REG_SS, 0xCC0), (UC_X86_REG_SP, 0x2FFE)):
            u.reg_write(r, v)
        u.mem_write(0xCC00 + 0x2FFE, STOP.to_bytes(2, "little"))
        u.reg_write(UC_X86_REG_EFLAGS, 2)
        u.emu_start(KSEG * 16 + self.S[name], KSEG * 16 + STOP, count=200000)
        assert u.reg_read(UC_X86_REG_IP) == STOP, name
        assert u.reg_read(UC_X86_REG_SP) == 0x3000
        assert u.reg_read(UC_X86_REG_ES) == 0x4321, "ES not preserved"


def setup(k, w, h, shape):
    for n, v in (("vid_seg", 0xA000), ("vid_stride", 80), ("vid_bmask", 0), ("vid_rowadd", 80),
                 ("vid_wrapbit", 0), ("vid_wrapfix", 0), ("vid_ch", h), ("vid_rowmax", 0),
                 ("vid_cw", w), ("vid_cwm1", w - 1), ("vid_chm1", h - 1),
                 ("vid_ox", 0), ("vid_oy", 0), ("cur_hx", shape[1]), ("cur_hy", shape[2]),
                 ("cur_tabp", k.S[shape[0]])):
        k.w16(n, v)
    k.w8("vid_bshift", 0); k.w8("vid_planes", 4); k.w8("vid_ndisp", 1)
    for n in ("gfx_sub_st", "gfx_sub_rs", "gfx_sub_ps"):
        k.w16(n, 0)
    k.w16("vga_pn", 0x0104)


SHAPES = [("cur_and", 0, 0), ("cur_cross_and", 3, 5), ("cur_busy_and", 4, 6)]
rnd = random.Random(a.seed)
kb, kt = K(a.base), K(a.tip)
bad = 0
for it in range(a.n):
    w, h = rnd.choice([(640, 480), (640, 350)])
    sh = rnd.choice(SHAPES)
    for k in (kb, kt):
        setup(k, w, h, sh)
    x = rnd.choice([rnd.randrange(w), w - 1 - rnd.randrange(16), rnd.randrange(16)])
    y = rnd.choice([rnd.randrange(h), h - 1 - rnd.randrange(14), rnd.randrange(8)])
    fill = [bytes(rnd.randrange(256) for _ in range(0x10000)) for _ in range(4)]
    junk = bytes(rnd.randrange(256) for _ in range(96))
    out = []
    for k in (kb, kt):
        k.planes = [bytearray(f) for f in fill]
        k.u.mem_write(KSEG * 16 + k.S["cur_save"], junk)
        k.w16("cur_drawn_x", x); k.w16("cur_drawn_y", y)
        k.call("cur_geom")
        k.writes = {}
        k.call("cur_saveu")
        buf = bytes(k.u.mem_read(KSEG * 16 + k.S["cur_save"], 96))
        st1 = (k.readmap, k.mapmask, len(k.writes))
        # scribble the planes, then put the save back
        for p in range(4):
            for o in range(0, 0x10000, 7):
                k.planes[p][o] ^= 0x5A
        k.writes = {}
        k.call("cur_restoreu")
        mx = max(k.writes.values()) if k.writes else 0
        out.append((buf, st1, [bytes(p) for p in k.planes], (k.readmap, k.mapmask), sorted(k.writes), mx,
                    k.r16("vga_pn")))
    if out[0][:6] != out[1][:6]:
        bad += 1
        if bad < 8:
            print("MISMATCH", (x, y), (w, h), sh[0], "buf", out[0][0] == out[1][0], "st", out[0][1], out[1][1],
                  "planes", out[0][2] == out[1][2], "regs", out[0][3], out[1][3], "wset",
                  out[0][4] == out[1][4], "maxw", out[1][5])
print("cases %d, mismatches %d" % (a.n, bad))
sys.exit(1 if bad else 0)
