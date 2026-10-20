#!/usr/bin/env python3
"""A GIF THAT PLAYS, ON THE HOST (SPEC.md 106.25): the GIF part's frames under
Unicorn, against tools/pixelsim.py's gif_anim.

    python3 tests/pxanimemu.py [-k NAME] [--win 16384] [-v]

The GIF part (build/pxgif.bin) decodes frame 0 as the package would - K_EMIT
writing each row into a 1/1 master - and AV_INIT reads its animation facts
out of that decode; then the WORKER's frame job (JOB_ANIM) is run the way
the resident runs it: the stream from [px_anpos] (or the file's start, for a
global table frame 0 did not use), a fresh work claim, and the UI's part
played by this script - at every frame the job says is ready ([px_anrun] 2)
the master is held to pixelsim's frame byte for byte, and the dirty rect
the frame leaves owed ([px_anrect]) to pixelsim's; then go. At the pass's
end AV_WAKE is called for real, and the second pass's frame 0 must be
pixelsim's gif_restart. A job that asks for more backup (PXD_MEM) gets it,
and every animation claim - av_start's 1 KB first - has a fence after it.

The fast loop; tests/pxdecode.py's animation leg holds the same part on
MartyPC.
"""
import os
import sys
import argparse
import struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import pxpartemu as E                                    # noqa: E402
import pixelsim as P                                     # noqa: E402
import pixcorpus as C                                    # noqa: E402
if not E.available():                                    # pragma: no cover
    print("%s: SKIP - no Unicorn here (pip install unicorn); tests/pxdecode.py "
          "holds the same part on MartyPC" % os.path.basename(__file__))
    sys.exit(0)
from unicorn import UC_HOOK_CODE                         # noqa: E402
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_FLAGS,   # noqa: E402
                               UC_X86_REG_SI, UC_X86_REG_CX,
                               UC_X86_REG_ES, UC_X86_REG_DX)

K = dict(SW=4, SH=6, RF=8, BITS=9, PACK=10, NPAL=12, DPARA=14, SCL=16, MW=18,
         MH=20, RSEG=22, DSEG=24, SPAL=26, SVC=28, HSEG=0, HLEN=2)
SVC = 0x0F00
PXS_NEXT, PXS_EMIT, PXS_SCAT, PXS_TICK, PXS_PAL = 0, 1, 2, 3, 4
GATE = 0x0F40                           # the UI services' gate: a retf
JOB_DECODE, JOB_ANIM = 1, 5
PXD_ABORT, PXD_MEM = 0xFF, 7
LZW_KB = 18
R = dict(MW=32, MH=34, SCL=36, PMODE=37, MSEG=38, NPAL=44, FMT=24)


class Anim:
    def __init__(self, win):
        self.p = E.PxPart(os.path.join(E.ROOT, "build", "pxgif.bin"))
        self.win = win
        mu = self.p.mu
        for i in range(8):
            mu.mem_write(E.PKG * 16 + SVC + 4 * i, b"\xCB")
        mu.mem_write(E.PKG * 16 + GATE, b"\xF8\xCB")      # clc ; retf
        mu.hook_add(UC_HOOK_CODE, self._svc, begin=E.PKG * 16 + SVC,
                    end=E.PKG * 16 + SVC + 32)
        self.p.w16("px_svgp", GATE)
        self.p.on_alive = self._alive
        self.k = self.p.syms["px_k"]
        self.claims = {}
        self.cells = self.p.cells
        mu.hook_add(UC_HOOK_CODE, self._mem, begin=E.KSEG * 16,
                    end=E.KSEG * 16 + 0xFFFF)

    def _mem(self, mu, addr, size, _):
        off = addr - E.KSEG * 16
        if off == self.cells["OSAPI_MEM_CLAIM"]:
            kb = mu.reg_read(UC_X86_REG_AX)
            seg = self.p.claim(kb * 1024)
            mu.reg_write(UC_X86_REG_DX, seg)
            f = mu.reg_read(UC_X86_REG_FLAGS)
            mu.reg_write(UC_X86_REG_FLAGS, f & ~1)

    def _flag(self, cf):
        f = self.p.mu.reg_read(UC_X86_REG_FLAGS)
        self.p.mu.reg_write(UC_X86_REG_FLAGS, (f | 1) if cf else (f & ~1))

    def _svc(self, mu, addr, size, _):
        i = (addr - E.PKG * 16 - SVC) // 4
        p = self.p
        if i == PXS_NEXT:
            if self.pos >= len(self.data):
                self._flag(True)
                return
            # px_rnext's windows: the slot from the stream's place, its
            # rbase the window's start (a cluster's, the base dropped)
            start = self.pos
            chunk = self.data[start:start + self.win]
            mu.mem_write(self.ring * 16, chunk)
            p.w16("px_rbase", start & 0xFFFF)
            p.w16("px_rbase", start >> 16, 2)
            p.w16("px_rpos", len(chunk))
            p.w16("px_rend", len(chunk))
            mu.reg_write(UC_X86_REG_ES, self.ring)
            mu.reg_write(UC_X86_REG_SI, 0)
            mu.reg_write(UC_X86_REG_CX, len(chunk))
            self.pos += len(chunk)
            self._flag(False)
        elif i == PXS_EMIT:
            y = mu.reg_read(UC_X86_REG_AX)
            row = bytes(mu.mem_read(self.rseg * 16, self.sw))
            self.master[y * self.sw:(y + 1) * self.sw] = row
            self._flag(False)
        elif i in (PXS_PAL, PXS_TICK, PXS_SCAT):
            self._flag(p.r8("px_abort") != 0 and i == PXS_TICK)
            if i == PXS_TICK and p.r8("px_abort"):
                mu.reg_write(UC_X86_REG_AX, PXD_ABORT)
        else:
            raise RuntimeError("service %d" % i)

    def _alive(self, p):
        if p.r8("px_anrun") == 2 and self.on_ready:
            self.on_ready()

    def stream(self, base):
        self.data_pos = base
        self.pos = base

    def decode0(self, data):
        p = self.p
        self.data = data
        ph = P.gif_header(data[:2048], len(data))
        self.sw, self.sh = ph.w, ph.h
        self.rseg = p.claim(8192 + 16)
        self.dseg = p.claim(LZW_KB * 1024 + 16)
        self.ring = p.claim(self.win + 16)
        self.mseg = p.claim(self.sw * self.sh + 2048 + 16)
        self.master = bytearray(self.sw * self.sh)
        p.wbytes("px_spal", b"\0" * 768)
        for f, v in (("SW", self.sw), ("SH", self.sh), ("SCL", 0),
                     ("MW", self.sw), ("MH", self.sh), ("RSEG", self.rseg),
                     ("DSEG", self.dseg), ("SPAL", p.syms["px_spal"])):
            if f == "SCL":
                p.w8("px_k", v, K[f])
            else:
                p.w16("px_k", v, K[f])
        for i in range(7):
            p.w16("px_k", SVC + 4 * i, K["SVC"] + 2 * i)
        p.w8("px_job", JOB_DECODE)
        p.w8("px_hmode", 0)
        self.stream(0)
        ax, cf = p.call(1, di=self.k)
        if cf:
            return ax
        # the record a successful open leaves
        p.wbytes("px_pal", p.rbytes("px_spal", 768))
        p.w16("px_cur", self.sw, R["MW"])
        p.w16("px_cur", self.sh, R["MH"])
        p.w8("px_cur", 0, R["SCL"])
        p.w16("px_cur", p.r16("px_k", K["NPAL"]), R["NPAL"])
        p.w8("px_cur", 3, R["FMT"])
        p.seg_write(self.mseg, self.master)
        p.w16("px_cur", self.mseg, R["MSEG"])
        p.w8("px_rflat", 0)
        p.w8("px_job", 0)
        return 0

    def master_now(self):
        return self.p.seg_read(self.mseg, self.sw * self.sh)

    def rect(self):
        v = struct.unpack("<4H", self.p.rbytes("px_anrect", 8))
        return None if v[0] == 0xFFFF else v

    def job(self):
        """One JOB_ANIM run, as av_start makes it. AX, CF back."""
        p = self.p
        wb = p.claim((LZW_KB + 8) * 1024)
        p.w16("px_wbase", wb)
        gread = p.r8("px_anflg") & 4
        base = 0 if gread else p.r16("px_anpos") | (p.r16("px_anpos", 2) << 16)
        self.stream(base)
        p.w8("px_abort", 0)
        p.w8("px_ango", 0)
        p.w8("px_anrun", 1)
        p.w8("px_anjob", 1)
        p.w8("px_job", JOB_ANIM)
        ax, cf = p.call(1, di=0)
        p.w8("px_job", 0)
        return ax, cf

    def wake_end(self, res):
        p = self.p
        p.w8("px_wres", res)
        p.w8("px_job", 0)
        p.call(2, cx=2)                  # PXV_INFO, AV_WAKE


def run(name, data, win, v):
    msgs = []
    try:
        loop, frames = P.gif_anim(data)
    except P.Refused:
        return None
    a = Anim(win)
    r = a.decode0(data)
    if r:
        return ["frame 0 refused %d" % r]
    if bytes(a.master) != frames[0]["master"]:
        return ["frame 0's master is not pixelsim's"]
    a.p.wbytes("px_anim", b"\0")
    a.p.call(2, cx=0)                   # AV_INIT
    anim = a.p.r8("px_anim")
    want = P.gif_animated(data)
    if bool(anim) != bool(want and len(frames) > 1):
        return ["animated %d, pixelsim %r (%d frames)" % (anim, want,
                                                          len(frames))]
    if not anim:
        return msgs
    if a.p.r16("px_andly") != frames[0]["delay"]:
        msgs.append("frame 0's delay %d ticks, pixelsim %d" % (
            a.p.r16("px_andly"), frames[0]["delay"]))
    lw = 0 if loop is None else (0xFFFF if loop == 0 else loop)
    if a.p.r16("px_anloop") != lw:
        msgs.append("passes after the first %d, pixelsim %r" % (
            a.p.r16("px_anloop"), loop))
    seen = []
    fences = []

    def fenced(kb):
        """A claim of KB as av_start / AV_WAKE make it, with 4 KB of 0xA5
        laid straight after it: a backup that runs past its claim (review-
        w8 A1: a 320 x 200 frame's sum carried and read as 0 KB) breaks it"""
        seg = a.p.claim(kb * 1024)
        fs = a.p.claim(4096)
        a.p.seg_write(fs, b"\xA5" * 4096)
        fences.append(fs)
        return seg
    a.p.w16("px_anseg", fenced(1))     # av_start's first: 1 KB
    a.p.w16("px_ankb", 1)

    def ready():
        k = a.p.r16("px_anfr") - 1
        got = a.master_now()
        f = frames[k] if k < len(frames) else None
        if f is None:
            msgs.append("a frame %d pixelsim does not have" % k)
        else:
            if got != f["master"]:
                d = next(i for i in range(len(got)) if got[i] != f["master"][i])
                msgs.append("frame %d differs at (%d,%d): %d vs %d" % (
                    k, d % a.sw, d // a.sw, got[d], f["master"][d]))
            rc = a.rect()
            if rc != (tuple(f["rect"]) if f["rect"] else None):
                msgs.append("frame %d's rect %r, pixelsim %r" % (k, rc,
                                                                 f["rect"]))
            if a.p.r16("px_andly") != f["delay"]:
                msgs.append("frame %d's delay %d ticks, pixelsim %d" % (
                    k, a.p.r16("px_andly"), f["delay"]))
        seen.append(k)
        a.p.wbytes("px_anrect", b"\xFF\xFF" + b"\0" * 6)
        a.p.w8("px_anrun", 3)
        a.p.w8("px_ango", 1)
    a.on_ready = ready
    for _ in range(40):                 # the first pass (asking for room)
        ax, cf = a.job()
        if cf and ax == PXD_MEM:
            need = a.p.r16("px_anneed")
            if need == 0xFFFF:
                msgs.append("a backup over a segment")
                break
            old = a.p.r16("px_anseg")
            seg = fenced(need)
            a.p.seg_write(seg, a.p.seg_read(old, 768))
            a.p.w16("px_anseg", seg)
            a.p.w16("px_ankb", need)
            continue
        if cf:
            msgs.append("the job answered %d" % ax)
        break
    if seen != list(range(1, len(frames))):
        msgs.append("frames shown %r, pixelsim 1..%d" % (seen, len(frames) - 1))
    for fs in fences:
        if a.p.seg_read(fs, 4096) != b"\xA5" * 4096:
            msgs.append("a write past the animation's claim")
            break
    # the pass is over: AV_WAKE's restart, then the second pass's frame 0
    a.wake_end(0)
    if loop is None:
        if a.p.r8("px_anon"):
            msgs.append("plays again without a loop count")
        return msgs
    seen.clear()
    restart = P.gif_restart(data)

    def ready2():
        k = a.p.r16("px_anfr") - 1
        if k == 0 and a.master_now() != restart:
            msgs.append("the second pass's frame 0 is not gif_restart's")
        seen.append(k)
        a.p.w8("px_abort", 1)            # (one frame of it is enough)
    a.on_ready = ready2
    a.job()
    if seen[:1] != [0]:
        msgs.append("the second pass did not start at frame 0: %r" % seen)
    return msgs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k")
    ap.add_argument("--win", type=int, default=16384)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    bad = n = 0
    for name, data, want in C.corpus():
        if not name.endswith(".GIF") or want:
            continue
        if a.k and a.k.upper() not in name:
            continue
        n += 1
        try:
            msgs = run(name, data, a.win, a.v)
        except Exception as e:                       # noqa: BLE001
            msgs = ["crashed: %s" % e]
        if msgs is None:
            n -= 1
            continue
        if msgs or a.v:
            print("%s %-12s %s" % ("FAIL" if msgs else "ok  ", name,
                                   "; ".join(msgs)))
        bad += bool(msgs)
    print("pxanimemu: %d GIFs, %s" % (n, "all pixelsim's" if not bad
                                      else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
