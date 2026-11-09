#!/usr/bin/env python3
"""moveq - cur_move_mono / cur_put_mono / cur_get_mono equivalence, base vs tip,
on each kernel's OWN bytes under unicorn.

    python3 moveq.py <base build dir> <tip build dir> [--seqs N] [--seed S]

For each adapter geometry (CGA 640x200 2-bank, Hercules 720x348 4-bank,
linear 640x480 1bpp) and each shape (arrow, cross, busy - with their hot
spots), runs the SAME random walk on both kernels: random VRAM, a show
(cur_put_mono), N moves (cur_move_mono) with small and large deltas and the
right/bottom edges, then a hide (cur_get_mono). After every call it compares
the whole VRAM, the live save buffer's VALID bytes, and the banked geometry,
and it counts writes per VRAM byte during each move (each must be <= 1).
After the hide the VRAM must equal the original fill.
"""
import argparse, random, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.x86_const import *

ap = argparse.ArgumentParser()
ap.add_argument("base")
ap.add_argument("tip")
ap.add_argument("--seqs", type=int, default=40)
ap.add_argument("--moves", type=int, default=60)
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--small", action="store_true", help="kern_small maps (no vid_ox/oy)")
a = ap.parse_args()

KSEG = 0x60


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
        vstart, start, ln = secs[".text"]
        self.text = full[start:start + ln]
        self.u = Uc(UC_ARCH_X86, UC_MODE_16)
        self.u.mem_map(0, 0x100000)
        self.u.mem_write(KSEG * 16, self.text)
        self.low = 0xCC0
        self.writes = {}
        self.vlo, self.vhi = 0, 0
        self.u.hook_add(UC_HOOK_MEM_WRITE, self._w)
        self.STOP = 0xFFF0       # a `hlt`-free sentinel: return lands here

    def _w(self, uc, access, addr, size, value, user):
        if self.vlo <= addr < self.vhi:
            for i in range(size):
                self.writes[addr + i] = self.writes.get(addr + i, 0) + 1

    def w8(self, n, v):
        self.u.mem_write(KSEG * 16 + self.S[n], bytes([v & 0xFF]))

    def w16(self, n, v):
        self.u.mem_write(KSEG * 16 + self.S[n], (v & 0xFFFF).to_bytes(2, "little"))

    def r8(self, n):
        return self.u.mem_read(KSEG * 16 + self.S[n], 1)[0]

    def r16(self, n):
        return int.from_bytes(self.u.mem_read(KSEG * 16 + self.S[n], 2), "little")

    def call(self, name, ax=0, bx=0):
        u = self.u
        # sentinel: a 'ret' target in kernel space nothing uses: put a jmp $
        u.mem_write(KSEG * 16 + self.STOP, b"\xeb\xfe")
        for r, v in ((UC_X86_REG_CS, KSEG), (UC_X86_REG_DS, KSEG),
                     (UC_X86_REG_ES, 0x1234), (UC_X86_REG_SS, self.low),
                     (UC_X86_REG_AX, ax), (UC_X86_REG_BX, bx),
                     (UC_X86_REG_CX, 0x5A5A), (UC_X86_REG_DX, 0xA5A5),
                     (UC_X86_REG_SI, 0x1111), (UC_X86_REG_DI, 0x2222),
                     (UC_X86_REG_BP, 0x3333)):
            u.reg_write(r, v)
        sp = 0x3000
        sp -= 2
        u.mem_write(self.low * 16 + sp, self.STOP.to_bytes(2, "little"))
        u.reg_write(UC_X86_REG_SP, sp)
        u.reg_write(UC_X86_REG_EFLAGS, 0x0002)   # DF = 0, IF = 0
        u.emu_start(KSEG * 16 + self.S[name], KSEG * 16 + self.STOP,
                    count=200000)
        assert u.reg_read(UC_X86_REG_IP) == self.STOP, (name, hex(u.reg_read(UC_X86_REG_IP)))
        assert u.reg_read(UC_X86_REG_SP) == 0x3000, "stack imbalance"


GEOMS = {
    # name: seg, stride, banks, bshift, rowadd, wrapbit, wrapfix, w, h
    "cga": (0xB800, 80, 1, 1, 0x2000, 0x4000, (-0x4000 + 80) & 0xFFFF, 640, 200),
    "herc": (0xB000, 90, 3, 2, 0x2000, 0x8000, (-0x8000 + 90) & 0xFFFF, 720, 348),
    "lin": (0xA000, 80, 0, 0, 80, 0, 0, 640, 480),
}
SHAPES = [("cur_and", 0, 0), ("cur_cross_and", 3, 5), ("cur_busy_and", 4, 6)]


def setup(k, g, shape):
    seg, stride, bmask, bshift, rowadd, wb, wf, w, h = GEOMS[g]
    k.w16("vid_seg", seg); k.w16("vid_stride", stride); k.w16("vid_bmask", bmask)
    k.w8("vid_bshift", bshift); k.w16("vid_rowadd", rowadd); k.w16("vid_wrapbit", wb)
    k.w16("vid_wrapfix", wf); k.w16("vid_ch", h); k.w16("vid_rowmax", 0)
    k.w8("vid_planes", 1)
    if "vid_mono" in k.S:
        k.w8("vid_mono", 1)
    if not a.small:
        k.w16("vid_ox", 0); k.w16("vid_oy", 0); k.w8("vid_ndisp", 1)
    k.w16("cur_tabp", k.S[shape[0]]); k.w16("cur_hx", shape[1]); k.w16("cur_hy", shape[2])
    k.w16("cur_sptr", k.S["cur_save"])
    k.vlo, k.vhi = seg * 16, seg * 16 + 0x10000
    return w, h


def state(k):
    sp = k.r16("cur_sptr")
    rows, b1 = k.r8("cur_rows"), k.r8("cur_b1ok")
    buf = k.u.mem_read(KSEG * 16 + sp, 24)
    valid = []
    for r in range(rows):
        valid.append(buf[2 * r])
        if b1:
            valid.append(buf[2 * r + 1])
    geo = tuple(k.r16(n) for n in ("cur_off", "cur_cellx", "cur_celly",
                                   "cur_drawn_x", "cur_drawn_y")) + (rows, b1)
    return bytes(valid), geo


def main():
    rnd = random.Random(a.seed)
    kb, kt = K(a.base), K(a.tip)
    fails = moves = 0
    multi = 0
    for g in GEOMS:
        for shape in SHAPES:
            for sq in range(a.seqs):
                w, h = setup(kb, g, shape)
                setup(kt, g, shape)
                fill = bytes(rnd.randrange(256) for _ in range(0x10000))
                seg = GEOMS[g][0]
                for k in (kb, kt):
                    k.u.mem_write(seg * 16, fill)
                x, y = rnd.randrange(w), rnd.randrange(h)
                if sq % 4 == 1:
                    x = w - 1 - rnd.randrange(10)
                if sq % 4 == 2:
                    y = h - 1 - rnd.randrange(14)
                for k in (kb, kt):
                    k.w16("cur_drawn_x", x); k.w16("cur_drawn_y", y)
                    k.call("cur_put_mono")
                for mv in range(a.moves):
                    r = rnd.random()
                    if r < 0.7:
                        dx, dy = rnd.randint(-12, 12), rnd.randint(-14, 14)
                    elif r < 0.85:
                        dx, dy = rnd.randint(-127, 127), rnd.randint(-127, 127)
                    else:
                        dx, dy = rnd.choice([-1, 0, 1, 8, -8, 16]), rnd.choice([0, 1, -1, 12, -12])
                    nx, ny = min(max(x + dx, 0), w - 1), min(max(y + dy, 0), h - 1)
                    if rnd.random() < 0.08:
                        nx = w - 1 - rnd.randrange(9)
                    if rnd.random() < 0.08:
                        ny = h - 1 - rnd.randrange(13)
                    res = []
                    hs = rnd.random() < 0.15
                    for k in (kb, kt):
                        k.writes = {}
                        if hs:          # a hide and a show at the NEW place
                            k.call("cur_get_mono")
                            k.w16("cur_drawn_x", nx); k.w16("cur_drawn_y", ny)
                            k.call("cur_put_mono")
                            continue
                        k.call("cur_move_mono", nx, ny)
                        pass
                    for k in (kb, kt):
                        mx = 0 if hs else (max(k.writes.values()) if k.writes else 0)
                        res.append((bytes(k.u.mem_read(k.vlo, 0x10000)), state(k), mx,
                                    len(k.writes)))
                    moves += 1
                    if res[0][0] != res[1][0] or res[0][1] != res[1][1]:
                        fails += 1
                        if fails < 10:
                            vd = sum(1 for i in range(0x10000) if res[0][0][i] != res[1][0][i])
                            print("MISMATCH", g, shape[0], "seq", sq, "move", (x, y), "->", (nx, ny),
                                  "vram diff", vd, "state", res[0][1] == res[1][1],
                                  res[0][1][1], res[1][1][1])
                    if res[1][2] > 1:
                        multi += 1
                        if multi < 5:
                            print("TIP WROTE A BYTE TWICE", g, (x, y), "->", (nx, ny))
                    if res[0][3] != res[1][3]:
                        fails += 1
                        if fails < 10:
                            print("WRITE-SET SIZE DIFFERS", g, (x, y), (nx, ny), res[0][3], res[1][3])
                    x, y = nx, ny
                for k in (kb, kt):
                    k.call("cur_get_mono")
                    if bytes(k.u.mem_read(seg * 16, 0x10000)) != fill:
                        fails += 1
                        print("BACKGROUND NOT RESTORED", g, shape[0], "seq", sq,
                              "base" if k is kb else "tip")
    print("moves %d, mismatches %d, double-writes %d" % (moves, fails, multi))
    sys.exit(1 if fails or multi else 0)


main()
