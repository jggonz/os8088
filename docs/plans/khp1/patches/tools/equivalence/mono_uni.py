#!/usr/bin/env python3
"""Run kernel renderer bodies under unicorn, base against tip, and compare.

    python3 uni.py BASE_ROOT TIP_ROOT [N] [--only fill,xfer,...]

Each tree's build/kernel-full.bin + build/kernel.map is loaded into its own
1MB machine: .text at KERNEL_SEG, .cold at COLD_SEG (+vstart), .bss zeroed,
.lowbss at LOW_SEG with the stack. The display geometry is written as
vid_apply would for CGA and for Hercules, and every case starts from the SAME
random framebuffer on both kernels. A case passes when the framebuffer, the
caller's buffer and every register the contract preserves agree.
"""
import random
import re
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UcError
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_IP, UC_X86_REG_FLAGS)

REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX,
        "dx": UC_X86_REG_DX, "si": UC_X86_REG_SI, "di": UC_X86_REG_DI,
        "bp": UC_X86_REG_BP, "sp": UC_X86_REG_SP, "ds": UC_X86_REG_DS,
        "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS}
SENT = 0xFFF0
BUFSEG = 0x7000            # the caller's buffer (save/restore, spans list)


def load_map(root):
    secs, syms, equ = {}, {}, {}
    cur = None
    insym = False
    for L in open(root + "/build/kernel.map"):
        if L.startswith("-- Sections (summary)"):
            mode = "sum"
            cur = "sum"
            continue
        if L.startswith("-- Symbols"):
            insym = True
            continue
        if not insym:
            f = L.split()
            if cur == "sum" and len(f) == 6 and f[4] in ("progbits", "nobits"):
                secs[f[5]] = (int(f[0], 16), int(f[1], 16), int(f[3], 16),
                              f[4])
            if L.startswith("-- Sections (detailed)"):
                cur = None
            continue
        m = re.match(r"---- (?:Section (\S+)|No Section)", L)
        if m:
            cur = m.group(1)
            continue
        f = L.split()
        if cur is None and len(f) == 2 and re.fullmatch(r"[0-9A-F]{8}", f[0]):
            equ[f[1]] = int(f[0], 16)
        elif cur and len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[1]):
            syms[f[2]] = (cur, int(f[1], 16))
    return secs, syms, equ


class K:
    def __init__(self, root, adapter):
        self.root = root
        self.secs, self.syms, self.equ = load_map(root)
        img = open(root + "/build/kernel-full.bin", "rb").read()
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x110000)
        self.KS = self.equ["KERNEL_SEG"]
        self.LS = self.equ["LOW_SEG"]
        self.CS_ = self.equ.get("COLD_SEG")
        vs, st, ln, cl = self.secs[".text"]
        mu.mem_write(self.KS * 16 + vs, img[st:st + ln])
        if ".cold" in self.secs and self.CS_:
            vs, st, ln, cl = self.secs[".cold"]
            mu.mem_write(self.CS_ * 16 + vs, img[st:st + ln])
        self.adapter = adapter
        self.geom()

    def seg(self, name):
        sec, off = self.syms[name]
        return {".text": self.KS, ".bss": self.KS, ".lowbss": self.LS,
                ".cold": self.CS_}[sec], off

    def lin(self, name):
        s, o = self.seg(name)
        return s * 16 + o

    def w16(self, name, v):
        self.mu.mem_write(self.lin(name), struct.pack("<H", v & 0xFFFF))

    def w8(self, name, v):
        self.mu.mem_write(self.lin(name), bytes([v & 0xFF]))

    def r16(self, name):
        return struct.unpack("<H", self.mu.mem_read(self.lin(name), 2))[0]

    def geom(self):
        if self.adapter == "cga":
            row = (0xB800, 80, 1, 1, 0x2000, 0x4000, 0xC050, 640, 200)
            self.fb, self.fbsize = 0xB800, 0x4000
        else:
            row = (0xB000, 90, 3, 2, 0x2000, 0x8000, 0x805A, 720, 348)
            self.fb, self.fbsize = 0xB000, 0x8000
        self.row = row
        base = self.lin("vid_seg")
        self.mu.mem_write(base, struct.pack("<9H", *row))
        self.w16("vid_bankmask", row[2] << 13)
        self.w16("vid_rseg", row[0])
        self.w16("vid_cwm1", row[7] - 1)
        self.w16("vid_chm1", row[8] - 1)
        self.w16("vid_rowmax", 0)
        self.w8("vid_mono", 1)
        self.w8("vid_planes", 1)

    def call(self, entry, regs, limit=20000000):
        mu = self.mu
        sp = 0xFF00
        mu.reg_write(UC_X86_REG_SS, self.LS)
        sp -= 2
        mu.mem_write(self.LS * 16 + sp, struct.pack("<H", SENT))
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_CS, self.KS)
        mu.reg_write(UC_X86_REG_DS, self.KS)
        mu.reg_write(UC_X86_REG_ES, regs.get("es", self.KS))
        for k, v in regs.items():
            if k != "es":
                mu.reg_write(REGS[k], v & 0xFFFF)
        mu.reg_write(UC_X86_REG_FLAGS, 0x0002)
        s, o = self.seg(entry)
        mu.emu_start(s * 16 + o, self.KS * 16 + SENT, count=limit)
        ip = mu.reg_read(UC_X86_REG_IP)
        cs = mu.reg_read(UC_X86_REG_CS)
        if cs * 16 + ip != self.KS * 16 + SENT:
            raise RuntimeError("%s did not return: %04X:%04X" % (entry, cs, ip))
        out = {k: mu.reg_read(r) for k, r in REGS.items()}
        out["flags"] = mu.reg_read(UC_X86_REG_FLAGS)
        return out

    def fbuf(self):
        return bytes(self.mu.mem_read(self.fb * 16, self.fbsize))

    def setfb(self, data):
        self.mu.mem_write(self.fb * 16, data)

    def buf(self, n):
        return bytes(self.mu.mem_read(BUFSEG * 16, n))

    def setbuf(self, data):
        self.mu.mem_write(BUFSEG * 16, data)


def rect(r, W, H):
    """Mostly on-screen rects with edge cases, some clipped."""
    k = r.random()
    if k < 0.15:                      # one row
        x1 = r.randrange(-20, W + 5)
        x2 = x1 + r.randrange(0, 400)
        y1 = y2 = r.randrange(-2, H + 1)
    elif k < 0.3:                     # one column
        x1 = x2 = r.randrange(-2, W + 1)
        y1 = r.randrange(-20, H)
        y2 = y1 + r.randrange(0, 200)
    elif k < 0.4:                     # one pixel
        x1 = x2 = r.randrange(0, W)
        y1 = y2 = r.randrange(0, H)
    elif k < 0.55:                    # byte aligned
        x1 = r.randrange(0, W // 8) * 8
        x2 = x1 + r.randrange(1, 40) * 8 - 1
        y1 = r.randrange(0, H)
        y2 = y1 + r.randrange(0, 80)
    elif k < 0.65:                    # small
        x1 = r.randrange(0, W)
        x2 = x1 + r.randrange(0, 20)
        y1 = r.randrange(0, H)
        y2 = y1 + r.randrange(0, 12)
    else:
        x1 = r.randrange(-30, W + 10)
        x2 = x1 + r.randrange(0, W)
        y1 = r.randrange(-30, H + 10)
        y2 = y1 + r.randrange(0, H)
    return x1, y1, x2, y2


def run(base, tip, n, only):
    fails = 0
    cases = 0
    for ad in ("cga", "herc"):
        A, B = K(base, ad), K(tip, ad)
        W, H = A.row[7], A.row[8]
        r = random.Random(1983 + (ad == "herc"))
        for i in range(n):
            kind = r.choice(only)
            fb = bytes(r.getrandbits(8) for _ in range(A.fbsize)) \
                if i % 50 == 0 or i < 3 else None
            if fb is not None:
                A.setfb(fb)
                B.setfb(fb)
            x1, y1, x2, y2 = rect(r, W, H)
            col = r.randrange(16)
            regs = {"ax": x1, "bx": y1, "cx": x2, "dx": y2,
                    "si": r.getrandbits(16), "di": r.getrandbits(16),
                    "bp": r.getrandbits(16)}
            for k in (A, B):
                k.w8("gfx_color", col)
            pre = {}
            if kind in ("fill", "gray", "xor"):
                entry = {"fill": "sw_fill", "gray": "sw_fill_gray",
                         "xor": "sw_xor_fill"}[kind]
                keep = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ds",
                        "es")
            elif kind == "pat":
                entry = "sw_fill_pat"
                pat = bytes(r.getrandbits(8) for _ in range(8))
                for k in (A, B):
                    k.mu.mem_write(k.lin("sw_tmp_pat") if "sw_tmp_pat" in
                                   k.syms else k.KS * 16 + 0xFF80, pat)
                    k.w16("gfx_pat", 0xFF80)
                keep = ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "ds",
                        "es")
            elif kind in ("save", "restore"):
                entry = "sw_save" if kind == "save" else "sw_restore"
                data = bytes(r.getrandbits(8) for _ in range(0x9000))
                for k in (A, B):
                    k.setbuf(data)
                    sub = r.random() < 0.3 and kind == "restore"
                regs["es"] = BUFSEG
                regs["di" if kind == "save" else "si"] = r.randrange(0, 0x100)
                st = rs = ps = 0
                if kind == "restore" and r.random() < 0.3:
                    st, rs, ps = (r.randrange(0, 20), r.randrange(0, 6),
                                  r.randrange(0, 40))
                for k in (A, B):
                    k.w16("gfx_sub_st", st)
                    k.w16("gfx_sub_rs", rs)
                    k.w16("gfx_sub_ps", ps)
                keep = ("ax", "bx", "cx", "dx", "bp", "sp", "ds", "es",
                        "si" if kind == "save" else "di")
                keepio = "di" if kind == "save" else "si"
            elif kind == "scroll":
                # vgas_bankcopy with a legal geometry
                x1 = r.randrange(0, W // 8) * 8
                nb = r.randrange(1, (W - x1) // 8 + 1)
                h = r.randrange(2, H)
                y1 = r.randrange(0, H - h + 1)
                dy = r.randrange(1, h)
                if r.random() < 0.5:
                    dy = -dy
                if r.random() < 0.5:
                    dy = dy - dy % (A.row[2] + 1) or (A.row[2] + 1) * (1 if dy > 0 else -1)
                    if abs(dy) >= h:
                        dy = A.row[2] + 1
                        if dy >= h:
                            continue
                rows = h - abs(dy)
                for k in (A, B):
                    k.w16("vgas_sdy", dy)
                    k.w16("vgas_col", x1 // 8)
                    k.w16("vgas_nb", nb)
                    k.w16("vgas_rows", rows)
                    k.w16("vgas_ydst", y1 if dy > 0 else y1 + h - 1)
                    k.w16("vgas_step", 1 if dy > 0 else -1)
                entry = "vgas_bankcopy"
                keep = ("bp", "sp", "ds", "es")
                regs["es"] = A.KS
            else:
                raise SystemExit(kind)
            outs = []
            for k in (A, B):
                try:
                    outs.append(k.call(entry, dict(regs)))
                except (UcError, RuntimeError) as e:
                    outs.append({"err": str(e)})
            cases += 1
            bad = []
            if "err" in outs[0] or "err" in outs[1]:
                bad.append("error %r / %r" % (outs[0].get("err"),
                                              outs[1].get("err")))
            else:
                for rg in keep:
                    if outs[0][rg] != outs[1][rg]:
                        bad.append("%s %04X/%04X" % (rg, outs[0][rg],
                                                     outs[1][rg]))
                if kind in ("save", "restore"):
                    if outs[0][keepio] != outs[1][keepio]:
                        bad.append("%s %04X/%04X" % (keepio, outs[0][keepio],
                                                     outs[1][keepio]))
                if A.fbuf() != B.fbuf():
                    fa, fb_ = A.fbuf(), B.fbuf()
                    d = [j for j in range(len(fa)) if fa[j] != fb_[j]]
                    bad.append("fb differs at %d bytes, first %04X" %
                               (len(d), d[0]))
                    B.setfb(fa)
                if kind == "save" and A.buf(0x9000) != B.buf(0x9000):
                    bad.append("buffer differs")
                    B.setbuf(A.buf(0x9000))
                if kind == "scroll" and (A.r16("vgas_rows") != B.r16("vgas_rows")):
                    pass
            if bad:
                fails += 1
                if fails < 15:
                    print("%s #%d %s rect %r col %d: %s" % (
                        ad, i, kind, (x1, y1, x2, y2), col, "; ".join(bad)))
    print("%d cases, %d failed" % (cases, fails))
    return fails


if __name__ == "__main__":
    base, tip = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("-") else 2000
    only = ["fill", "gray", "xor", "pat", "save", "restore", "scroll"]
    for a in sys.argv:
        if a.startswith("--only="):
            only = a[7:].split(",")
    sys.exit(1 if run(base, tip, n, only) else 0)
