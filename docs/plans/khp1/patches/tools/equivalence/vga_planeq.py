#!/usr/bin/env python3
"""vga_planeq - the VGA rung's planar bodies, base vs tip, on each kernel's own
bytes under unicorn with a GC/Sequencer model (Set/Reset, enable, ALU
function, Bit Mask, latches, Read Map, Map Mask; write mode 0).

    python3 vga_planeq.py BASE_DIR TIP_DIR [--n N] [--seed S] [--only a,b]

Each DIR holds kernel.map and kernel-full.bin (kern_big: a build/ after
`make`, or a scratch `nasm ... --before "[map all DIR/kernel.map]"`). Written
when kern_big took KHP_VGASAVE/VGAFILL/VGAEDGE/VGABLITP and their size pass
(docs/plans/khp1/LOG.md L.6); base = the knobs off, tip = on. Bodies:
  fill   vga_solid_rect_v, replace and XOR, random rects incl. clipped
  gray   gfx_fill_gray_raw_v
  save   vga_save_vram_v (four planes and the one-plane arm)
  rest   vga_restore_vram_v (four planes, one plane, sub-rect armed)
  blitp  gfx_blitp_v (aligned blocks, narrow and wide rows, odd widths)
  cur    cur_geom + cur_saveu + cur_restoreu (the planar pointer's walks)
Compared: all four planes, every register on return, the GC state, the
memory a save writes, and the per-byte write counts (a body that writes a
byte twice where the base wrote it once differs even when the bytes agree).
"""
import argparse, random, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_INSN
from unicorn.x86_const import *

ap = argparse.ArgumentParser()
ap.add_argument("base")
ap.add_argument("tip")
ap.add_argument("--n", type=int, default=400)
ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--only", default="fill,gray,save,rest,blitp,cur")
a = ap.parse_args()
KSEG = 0x60
SSEG = 0x3000
BSEG = 0x5000        # caller buffers
VB = 0xA0000
REGS = [UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI,
        UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_DS, UC_X86_REG_ES]


def loadmap(bdir):
    syms, secs = {}, {}
    for line in open(bdir + "/kernel.map"):
        p = line.split()
        if len(p) == 3:
            try:
                syms.setdefault(p[2], int(p[1], 16))
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
        self.img = {}
        for sec in (".text", ".vga"):
            vs, st, ln = secs[sec]
            self.img[sec] = (KSEG * 16 + vs, full[st:st + ln])
        u.hook_add(UC_HOOK_MEM_READ, self._r, begin=VB, end=VB + 0xFFFF)
        u.hook_add(UC_HOOK_MEM_WRITE, self._w, begin=VB, end=VB + 0xFFFF)
        u.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)

    def reset(self, planes):
        u = self.u
        u.mem_write(0, bytes(0x100000))
        for base, data in self.img.values():
            u.mem_write(base, data)
        self.planes = [bytearray(p) for p in planes]
        self.gc = [0] * 9
        self.gc[8] = 0xFF
        self.gcidx = self.seqidx = 0
        self.mapmask = 0xF
        self.latch = [0, 0, 0, 0]
        self.writes = {}

    def _r(self, uc, acc, addr, size, val, ud):
        for i in range(size):
            o = (addr + i - VB) & 0xFFFF
            self.latch = [self.planes[p][o] for p in range(4)]
            uc.mem_write(addr + i, bytes([self.planes[self.gc[4] & 3][o]]))

    def _w(self, uc, acc, addr, size, val, ud):
        gc = self.gc
        for i in range(size):
            o = (addr + i - VB) & 0xFFFF
            b = (val >> (8 * i)) & 0xFF
            self.writes[o] = self.writes.get(o, 0) + 1
            assert (gc[5] & 3) == 0, "write mode %d" % (gc[5] & 3)
            assert (gc[3] & 7) == 0, "rotate"
            fn = (gc[3] >> 3) & 3
            for p in range(4):
                if not (self.mapmask & (1 << p)):
                    continue
                d = (0xFF if gc[0] & (1 << p) else 0) if gc[1] & (1 << p) else b
                l = self.latch[p]
                d = [d, d & l, d | l, d ^ l][fn]
                self.planes[p][o] = (d & gc[8]) | (l & ~gc[8] & 0xFF)

    def _out(self, uc, port, size, value, ud):
        vals = [(port, value & 0xFF)] if size == 1 else [(port, value & 0xFF), (port + 1, value >> 8)]
        for pt, v in vals:
            if pt == 0x3CE:
                self.gcidx = v
            elif pt == 0x3CF:
                if self.gcidx < 9:
                    self.gc[self.gcidx] = v
            elif pt == 0x3C4:
                self.seqidx = v
            elif pt == 0x3C5:
                if self.seqidx == 2:
                    self.mapmask = v & 0xF

    def w8(self, n, v, seg=KSEG): self.u.mem_write(seg * 16 + self.S[n], bytes([v & 0xFF]))
    def w16(self, n, v, seg=KSEG): self.u.mem_write(seg * 16 + self.S[n], (v & 0xFFFF).to_bytes(2, "little"))
    def r16(self, n): return int.from_bytes(self.u.mem_read(KSEG * 16 + self.S[n], 2), "little")

    def call(self, name, regs):
        u = self.u
        STOP = 0xFFF0
        u.mem_write(KSEG * 16 + STOP, b"\xeb\xfe")
        u.reg_write(UC_X86_REG_CS, KSEG)
        u.reg_write(UC_X86_REG_DS, KSEG)
        u.reg_write(UC_X86_REG_ES, regs.get("es", 0x4321))
        u.reg_write(UC_X86_REG_SS, SSEG)
        u.reg_write(UC_X86_REG_SP, 0xFFFE)
        u.mem_write(SSEG * 16 + 0xFFFE, STOP.to_bytes(2, "little"))
        for r, k in zip(REGS[:7], ("ax", "bx", "cx", "dx", "si", "di", "bp")):
            u.reg_write(r, regs.get(k, 0x1111 * (REGS.index(r) + 1)) & 0xFFFF)
        u.reg_write(UC_X86_REG_EFLAGS, 2)
        u.emu_start(KSEG * 16 + self.S[name], KSEG * 16 + STOP, count=5000000)
        assert u.reg_read(UC_X86_REG_IP) == STOP, name
        assert u.reg_read(UC_X86_REG_SP) == 0x0000, "stack unbalanced"
        return tuple(u.reg_read(r) for r in REGS) + (u.reg_read(UC_X86_REG_EFLAGS) & 1,)

    def state(self):
        return ([bytes(p) for p in self.planes], tuple(self.gc), self.mapmask, sorted(self.writes.items()))


def setup(k, w, h):
    for n, v in (("vid_seg", 0xA000), ("vid_stride", 80), ("vid_bmask", 0), ("vid_rowadd", 80),
                 ("vid_wrapbit", 0), ("vid_wrapfix", 0), ("vid_ch", h), ("vid_rowmax", 0),
                 ("vid_cw", w), ("vid_cwm1", w - 1), ("vid_chm1", h - 1),
                 ("vid_ox", 0), ("vid_oy", 0)):
        k.w16(n, v)
    k.w8("vid_bshift", 0); k.w8("vid_planes", 4); k.w8("vid_ndisp", 1)
    k.w16("vga_pn", 0x0104)


def rect(rnd, w, h):
    def coord(lim):
        return rnd.choice([rnd.randrange(lim), rnd.randrange(-20, 20), lim - 1 - rnd.randrange(-20, 20),
                           rnd.randrange(lim)])
    x1, x2 = sorted((coord(w), coord(w)))
    y1, y2 = sorted((coord(h), coord(h)))
    if rnd.random() < 0.15:
        x2 = x1 + rnd.randrange(8)
    if rnd.random() < 0.1:
        y2 = y1
    if rnd.random() < 0.03:
        x1, x2 = x2, x1          # empty
    return x1, y1, x2, y2


def case(kind, rnd):
    w, h = rnd.choice([(640, 480), (640, 350)])
    planes = [bytes(rnd.randrange(256) for _ in range(0x10000)) for _ in range(4)]
    plan = {"w": w, "h": h, "planes": planes}
    if kind in ("fill", "gray"):
        plan["r"] = rect(rnd, w, h)
        plan["color"] = rnd.randrange(16)
        plan["xorm"] = rnd.choice([0, 0, 0x0F, rnd.randrange(1, 16)])
    elif kind in ("save", "rest"):
        x1, y1, x2, y2 = rect(rnd, w, h)
        if rnd.random() < 0.5:
            y2 = min(y2, y1 + rnd.randrange(40))
        # a buffer is one segment: keep it under 64KB (a word straddling
        # offset FFFFh is out of contract, and unicorn does not wrap it)
        nb = (max(0, min(x2, w - 1)) // 8 - max(0, x1) // 8 + 1) * 4
        y2 = min(y2, y1 + 0xE000 // max(nb, 1))
        plan["r"] = (x1, y1, x2, y2)
        plan["pn"] = rnd.choice([0x0104, 0x0104, 0x0F01])
        plan["sub"] = (0, 0, 0) if rnd.random() < 0.6 else (rnd.randrange(8), rnd.randrange(6), rnd.randrange(30))
        plan["buf"] = bytes(rnd.randrange(256) for _ in range(0x10000))
    elif kind == "blitp":
        x = rnd.randrange(0, w, 8)
        wid = rnd.choice([8, 16, 24, 56, 64, 72, 120, 256, rnd.randrange(1, 200)])
        if rnd.random() < 0.85:
            wid = (wid + 7) & ~7          # the fast path wants whole bytes
        y = rnd.choice([rnd.randrange(h), rnd.randrange(-10, 0), h - rnd.randrange(1, 20)])
        hh = rnd.randrange(1, 40)
        plan.update(x=x, y=y, wid=wid, hh=hh, stride=((wid + 7) // 8) + rnd.choice([0, 0, 3]),
                    buf=bytes(rnd.randrange(256) for _ in range(0x10000)))
        plan["pstep"] = plan["stride"] * hh + rnd.choice([0, 5])
    elif kind == "cur":
        plan["shape"] = rnd.choice([("cur_and", 0, 0), ("cur_cross_and", 3, 5), ("cur_busy_and", 4, 6)])
        plan["x"] = rnd.choice([rnd.randrange(w), w - 1 - rnd.randrange(16), rnd.randrange(16)])
        plan["y"] = rnd.choice([rnd.randrange(h), h - 1 - rnd.randrange(14), rnd.randrange(8)])
        plan["junk"] = bytes(rnd.randrange(256) for _ in range(96))
    return plan


def run(k, kind, p):
    k.reset(p["planes"])
    setup(k, p["w"], p["h"])
    if kind in ("fill", "gray"):
        x1, y1, x2, y2 = p["r"]
        k.w8("gfx_color", p["color"]); k.w8("vga_xorm", p["xorm"])
        regs = k.call("vga_solid_rect_v" if kind == "fill" else "gfx_fill_gray_raw_v",
                      dict(ax=x1, bx=y1, cx=x2, dx=y2))
        return regs, k.state()
    if kind in ("save", "rest"):
        x1, y1, x2, y2 = p["r"]
        k.u.mem_write(BSEG * 16, p["buf"])
        k.w16("vga_pn", p["pn"])
        st, rs, ps = p["sub"]
        k.w16("gfx_sub_st", st); k.w16("gfx_sub_rs", rs); k.w16("gfx_sub_ps", ps)
        if kind == "save":
            regs = k.call("vga_save_vram_v", dict(ax=x1, bx=y1, cx=x2, dx=y2, di=0x100, es=BSEG))
        else:
            regs = k.call("vga_restore_vram_v", dict(ax=x1, bx=y1, cx=x2, dx=y2, si=0x100, es=BSEG))
        return regs, k.state(), bytes(k.u.mem_read(BSEG * 16, 0x10000)), k.r16("gfx_sub_st"), k.r16("gfx_sub_rs")
    if kind == "blitp":
        k.u.mem_write(BSEG * 16, p["buf"])
        regs = k.call("gfx_blitp_v", dict(ax=p["x"], bx=p["y"], cx=p["wid"], dx=p["hh"], si=0x40,
                                          di=p["pstep"], bp=p["stride"], es=BSEG))
        return regs, k.state()
    if kind == "cur":
        sh = p["shape"]
        k.w16("cur_hx", sh[1]); k.w16("cur_hy", sh[2]); k.w16("cur_tabp", k.S[sh[0]])
        k.u.mem_write(KSEG * 16 + k.S["cur_save"], p["junk"])
        k.w16("cur_drawn_x", p["x"]); k.w16("cur_drawn_y", p["y"])
        k.call("cur_geom", {})
        k.writes = {}
        k.call("cur_saveu", {})
        buf = bytes(k.u.mem_read(KSEG * 16 + k.S["cur_save"], 96))
        st1 = (k.gc[4], k.mapmask, len(k.writes))
        for pl in range(4):
            for o in range(0, 0x10000, 7):
                k.planes[pl][o] ^= 0x5A
        k.writes = {}
        k.call("cur_restoreu", {})
        return buf, st1, k.state(), k.r16("vga_pn")


kb, kt = K(a.base), K(a.tip)
rnd = random.Random(a.seed)
total = bad = 0
for kind in a.only.split(","):
    nb = 0
    for it in range(a.n):
        p = case(kind, rnd)
        rb, rt = run(kb, kind, p), run(kt, kind, p)
        total += 1
        if rb != rt:
            nb += 1
            if nb < 5:
                diff = [i for i in range(len(rb)) if rb[i] != rt[i]]
                print("MISMATCH", kind, {k_: v for k_, v in p.items() if k_ not in ("planes", "buf", "junk")},
                      "fields", diff, rb[0] if 0 in diff else "", rt[0] if 0 in diff else "")
    print("%-6s %d cases, %d mismatches" % (kind, a.n, nb))
    bad += nb
print("total %d, mismatches %d" % (total, bad))
sys.exit(1 if bad else 0)
