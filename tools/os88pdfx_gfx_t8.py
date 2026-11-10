#!/usr/bin/env python3
"""Runs build/PDXG.MOD (the 8086 build) in Unicorn's real-mode x86 and prints the
same lines as apps/pagedesk/hosttest/pdxg_test.c, so the host and the target can
be diffed (PAGEDESK-SPEC 20.22.9, L7).  PDENG is a Python stand-in at 5000:0.

    python3 tools/os88pdfx_gfx_t8.py build/PDXG.MOD tests/pdfany/gfx/colour.cases
"""
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UcError
from unicorn.x86_const import *

MODSEG, XBSEG, TSEG, RSEG, STKSEG, ENGSEG, RETSEG = 0x2000, 0x3000, 0x3100, 0x3200, 0x4000, 0x5000, 0x6000
lin = lambda seg, off: seg * 16 + off


class Eng:
    def __init__(self):
        self.syn, self.stm, self.cur, self.pos = {}, {}, None, 0


def run(mod_path, cases_path, trace=False, shade=False, stats=None):
    img = open(mod_path, "rb").read()
    magic, abi, isz, bss = struct.unpack_from("<HHHH", img)
    eng = Eng()
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0x20000, 0x70000)
    mu.mem_write(lin(MODSEG, 0), img)
    mu.mem_write(lin(RETSEG, 0), b"\xf4")
    mu.mem_write(lin(ENGSEG, 0), b"\xf4")

    def rd(seg, off, n): return bytes(mu.mem_read(lin(seg, off), n))
    def wr(seg, off, b): mu.mem_write(lin(seg, off), bytes(b))

    def engine(uc, addr, size, user):
        if addr != lin(ENGSEG, 0): return
        verb = uc.reg_read(UC_X86_REG_EAX) & 0xFF
        bx, cx, dx = (uc.reg_read(r) & 0xFFFF for r in (UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX))
        ax = 0
        if verb == 0x0B:                         # RAW_COPY
            obj, gen = struct.unpack("<HH", rd(XBSEG, 0, 4))
            dseg, doff, dmax = struct.unpack("<HHH", rd(XBSEG, 8, 6))
            done, = struct.unpack("<H", rd(XBSEG, 4, 2))
            data = eng.syn.get(obj, b"")[done:done + dmax]
            wr(dseg, doff, data)
            wr(XBSEG, 14, struct.pack("<H", len(data)))
        elif verb == 0x06:                       # STRM_OPEN
            obj, = struct.unpack("<H", rd(XBSEG, 0, 2))
            if obj in eng.stm: eng.cur, eng.pos = obj, 0
            else: ax = 53
        elif verb == 0x07:                       # STRM_READ
            data = eng.stm.get(eng.cur, b"")[eng.pos:eng.pos + dx]
            wr(bx, cx, data); eng.pos += len(data); ax = len(data)
        sp = uc.reg_read(UC_X86_REG_SP); ss = uc.reg_read(UC_X86_REG_SS)
        ip, cs = struct.unpack("<HH", rd(ss, sp, 4))
        uc.reg_write(UC_X86_REG_SP, (sp + 4) & 0xFFFF)
        uc.reg_write(UC_X86_REG_EIP, ip); uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_EAX, ax)
        uc.reg_write(UC_X86_REG_EFLAGS, uc.reg_read(UC_X86_REG_EFLAGS) & ~1)

    mu.hook_add(UC_HOOK_CODE, engine, begin=lin(ENGSEG, 0), end=lin(ENGSEG, 0) + 1)

    depth = [0]

    def call(verb, bx):
        wr(STKSEG, 0xD000, b"\xcc" * 0x2000)
        mu.reg_write(UC_X86_REG_SS, STKSEG); mu.reg_write(UC_X86_REG_SP, 0xF000)
        sp = 0xF000 - 4
        wr(STKSEG, sp, struct.pack("<HH", 0, RETSEG))
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_CS, MODSEG); mu.reg_write(UC_X86_REG_DS, 0x1111); mu.reg_write(UC_X86_REG_ES, 0x2222)
        mu.reg_write(UC_X86_REG_EAX, verb); mu.reg_write(UC_X86_REG_EBX, bx)
        mu.reg_write(UC_X86_REG_ECX, 0); mu.reg_write(UC_X86_REG_EDX, 0)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        try:
            mu.emu_start(lin(MODSEG, 8), lin(RETSEG, 0), count=40_000_000)
        except UcError as e:
            raise SystemExit("unicorn: %s at %04x:%04x" % (e, mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_IP)))
        used = 0xF000 - next((o for o in range(0xD000, 0xF000) if rd(STKSEG, o, 1) != b"\xcc"), 0xF000)
        depth[0] = max(depth[0], used)
        return mu.reg_read(UC_X86_REG_EAX) & 0xFFFF, mu.reg_read(UC_X86_REG_EFLAGS) & 1

    # PDV_X_INIT: pdxb_init at the XB: xb_seg, eng_off, eng_seg
    wr(XBSEG, 0, b"\0" * 1024)
    wr(XBSEG, 0, struct.pack("<HHH", XBSEG, 0, ENGSEG) + b"\0" * 42 + struct.pack("<HH", 0x8000, 14))
    call(0xC0, XBSEG)

    def fx(v):
        n = int(round(v * 65536)) & 0xFFFFFFFF
        return struct.pack("<HH", n & 0xFFFF, n >> 16)

    out = []
    for line in open(cases_path):
        t = line.split()
        if not t: continue
        if t[0] == "O": eng.syn[int(t[1])] = bytes.fromhex(t[2])
        elif t[0] == "S": eng.stm[int(t[1])] = bytes.fromhex(t[2])
        elif t[0] == "H" and shade:
            obj = int(t[2]); st = 0; tot = 0
            while True:
                g = struct.pack("<18H", obj, 0, 0, 0, 0, 0x7000, 0, 760, 0, 0, 0, st, 0, 0, 0, 0, 0, 0)
                g += b"\0" * 36 + fx(4) + b"\0" * 8 + fx(-4) + b"\0" * 4 + fx(192) + struct.pack("<4hH", 0, 0, 63, 47, 0)
                wr(XBSEG, 0, g)
                ax, cf = call(0xCB, XBSEG)
                x = rd(XBSEG, 0, 106)
                rlen, = struct.unpack_from("<H", x, 16)
                ntri, = struct.unpack_from("<H", x, 32)
                st, = struct.unpack_from("<H", x, 22)
                q = rd(0x7000, 0, rlen)
                o = 0
                for _ in range(ntri):
                    npts = q[o + 4]
                    pts = struct.unpack_from("<%dh" % (2 * npts), q, o + 6)
                    out.append("P %d %d %d %d %d %d %s" % (q[o], q[o + 1], q[o + 2], q[o + 3], q[o + 5], npts, " ".join(map(str, pts))))
                    o += 6 + 4 * npts; tot += 1
                if ax != 65: break
            out.append("END %s %d" % (t[1], tot))
        elif t[0] == "C":
            name, text, nc = t[1], bytes.fromhex(t[2]), int(t[3])
            comps = [float(x) for x in t[4:4 + nc]]
            wr(TSEG, 0, text)
            g = struct.pack("<HHHHHHHHHHHHHHHHHH", 0, 0, TSEG, 0, len(text), RSEG, 0, 4096, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
            wr(XBSEG, 0, g + b"\0" * (72 - len(g)))
            ax, cf = call(0xC8, XBSEG)
            if ax != 0:
                out.append("%s FAILED %d" % (name, ax)); continue
            cs = rd(RSEG, 0, 1264)
            kind, n, flags = cs[0], cs[1], cs[2]
            rgb, none = [0, 0, 0], False
            if flags & 0x10: none = True
            elif flags & 0x01:                   # PDREND's own lookup of a table space: Indexed rows, else 65 samples
                hival, = struct.unpack_from("<H", cs, 4)
                nr = (256 if hival > 255 else hival + 1) if kind == 7 else 65
                ix = int(round(comps[0])) if kind == 7 else int(round(comps[0] * 64))
                ix = max(0, min(nr - 1, ix))
                rgb = list(rd(RSEG, 16 + 3 * ix, 3))
            elif flags & 0x02: rgb = [int(round(c * 255)) for c in comps[:3]]
            elif flags & 0x04:
                a, b, c, k = [int(round(v * 255)) for v in comps[:4]]
                rgb = [(255 - a) * (255 - k) // 255, (255 - b) * (255 - k) // 255, (255 - c) * (255 - k) // 255]
            else:
                q = struct.pack("<18H", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, RSEG, 0, len(comps), 0, 0, 0)
                q += b"".join(fx(c) for c in comps) + b"\0" * 4 * (8 - len(comps)) + b"\0" * 4
                wr(XBSEG, 0, q)
                ax, cf = call(0xC9, XBSEG)
                if ax != 0: out.append("%s FAILED COLOR" % name); continue
                r = rd(XBSEG, 68, 4)
                none = bool(r[3]); rgb = list(r[:3])
            out.append("%s NONE" % name if none else "%s %d %d %d" % (name, *rgb))
    if stats is not None:
        stats["stack"] = depth[0]
    return out


if __name__ == "__main__":
    st = {}
    print("\n".join(run(sys.argv[1], sys.argv[2], shade=len(sys.argv) > 3, stats=st)))
    print("deepest stack use: %d bytes" % st["stack"], file=sys.stderr)
