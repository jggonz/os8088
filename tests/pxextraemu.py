#!/usr/bin/env python3
"""THE EXTRAS PART ON THE HOST (SPEC.md 106.25): HEAD and DECODE under Unicorn.

    python3 tests/pxextraemu.py [-k NAME] [--cl 512] [--win 16384]

Every TIFF, ICO, IFF and MacPaint fixture tools/pixcorpus.py makes is run
through build/pxextra.bin exactly as the resident runs it: HEAD on the
first head, again on each PXD_MORE with the head px_hmore would read (the
cluster holding the offset asked for, the 2,048 bytes from it), until it
answers; then DECODE over a ring of windows K_RING hands out, every row
K_EMIT is given captured. The verdict must be pixelsim's, and every good
fixture's rows - their order, their y and their bytes - pixelsim's too, so
the guest's master (the emitter is the resident's, the same for every
format) can only be pixelsim's. A PNG inside an ICO must answer PXD_REDIR
with the head moved down to the PNG and [px_sbase] set at it.

This is the fast loop; tests/pxdecode.py holds the same part inside the
package on MartyPC.
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
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_FLAGS,   # noqa: E402
                               UC_X86_REG_BX, UC_X86_REG_DX)

PXF = {"TIFF": 6, "ICO": 10, "LBM": 11, "MAC": 12}
K = dict(HSEG=0, HLEN=2, SW=4, SH=6, RF=8, BITS=9, PACK=10, RBLK=11, NPAL=12,
         DPARA=14, SCL=16, MW=18, MH=20, RSEG=22, DSEG=24, SPAL=26, SVC=28,
         FLAGS=42, HCNT=43, HPOS=44, HBASE=46, HNEXT=50, FSZ=54)
PXD_MORE, PXD_REDIR = 0x80, 0x83
SVC = 0x0F00                            # the services' stubs, in the package
PXS_EMIT, PXS_RING = 1, 6


class Run:
    def __init__(self, part, cl=512, win=16384):
        self.p = E.PxPart(part)
        self.cl, self.win = cl, win
        self.k = self.p.syms["px_k"]
        mu = self.p.mu
        # the two services: a retf each, the work done in Python first
        for i in range(7):
            mu.mem_write(E.PKG * 16 + SVC + 4 * i, b"\xCB")
        from unicorn import UC_HOOK_CODE
        mu.hook_add(UC_HOOK_CODE, self._svc,
                    begin=E.PKG * 16 + SVC, end=E.PKG * 16 + SVC + 28)
        mu.hook_add(UC_HOOK_CODE, self._readat, begin=E.KSEG * 16,
                    end=E.KSEG * 16 + 0xFFFF)

    def _readat(self, mu, addr, size, _):
        # OSAPI_FILE_READ_AT, for the part's own next head (pxhmore.inc):
        # whole clusters at a cluster's offset, as the kernel reads them
        if addr - E.KSEG * 16 != self.p.cells["OSAPI_FILE_READ_AT"]:
            return
        from unicorn.x86_const import UC_X86_REG_CX, UC_X86_REG_ES
        off = mu.reg_read(UC_X86_REG_AX) | (mu.reg_read(UC_X86_REG_DX) << 16)
        n = mu.reg_read(UC_X86_REG_CX)
        es = mu.reg_read(UC_X86_REG_ES)
        bx = mu.reg_read(UC_X86_REG_BX)
        assert off % self.cl == 0, "READ_AT off a cluster: %d" % off
        got = self.data[off:off + n]
        mu.mem_write(es * 16 + bx, got)
        mu.reg_write(UC_X86_REG_AX, len(got) & 0xFFFF)
        mu.reg_write(UC_X86_REG_DX, len(got) >> 16)
        self._flag(False)
        self.heads += 1

    def kw(self, f, v):
        self.p.w16("px_k", v, K[f])

    def kr(self, f):
        return self.p.r16("px_k", K[f])

    def _flag(self, cf):
        f = self.p.mu.reg_read(UC_X86_REG_FLAGS)
        self.p.mu.reg_write(UC_X86_REG_FLAGS, (f | 1) if cf else (f & ~1))

    def _svc(self, mu, addr, size, _):
        i = (addr - E.PKG * 16 - SVC) // 4
        if i == PXS_EMIT:
            y = mu.reg_read(UC_X86_REG_AX)
            n = self.w * (3 if self.rf == P.RF_RGB else 1)
            self.rows.append((y, bytes(mu.mem_read(self.rseg * 16, n))))
            self._flag(False)
        elif i == PXS_RING:
            # px_rnext: the window given back, the next one taken
            self.pos = self.nxt
            if self.pos >= len(self.data):
                self.p.w16("px_rpos", 0)
                self.p.w16("px_rend", 0)
                self._flag(True)
                return
            chunk = self.data[self.pos:self.pos + self.win]
            mu.mem_write(self.ring * 16, chunk)
            self.p.w16("px_rbase", self.pos & 0xFFFF)
            self.p.w16("px_rbase", self.pos >> 16, 2)
            self.p.w16("px_rpos", 0)
            self.p.w16("px_rend", len(chunk))
            self.p.w16("px_rsegc", self.ring)
            self.nxt = self.pos + len(chunk)
            self._flag(False)
        else:
            raise RuntimeError("service %d called" % i)

    def head(self, data, fmt):
        p, mu = self.p, self.p.mu
        self.data = data
        fsz = len(data)
        p.wbytes("px_spal", b"\0" * 768)
        p.w8("px_cur", PXF[fmt], 24)                 # PXR_FMT
        p.w8("px_rflat", 0)
        hcap = max(2048, self.cl) + self.cl
        hseg = p.claim(hcap + 16)
        p.w16("px_hseg", hseg)
        p.w16("px_hcap", hcap)
        p.w16("px_clsz", self.cl)
        first = data[:max(2048, self.cl)]
        p.seg_write(hseg, first + b"\0" * (hcap - len(first)))
        for f, v in (("HSEG", hseg), ("HLEN", len(first)), ("HPOS", 0),
                     ("HBASE", 0), ("FLAGS", 0)):
            self.kw(f, v)
        p.w16("px_k", 0, K["HBASE"] + 2)
        p.w8("px_k", 0, K["HCNT"])
        p.w16("px_k", fsz & 0xFFFF, K["FSZ"])
        p.w16("px_k", fsz >> 16, K["FSZ"] + 2)
        self.heads = 1
        while True:
            ax, cf = p.call(3, di=self.k)
            if not cf:
                return 0, None
            if ax == PXD_MORE:
                raise RuntimeError("PXD_MORE reached the resident: the part "
                                   "reads its own next head (pxhmore.inc)")
            if ax == PXD_REDIR:
                sb = p.r16("px_sbase") | (p.r16("px_sbase", 2) << 16)
                hl = self.kr("HLEN")
                moved = p.seg_read(hseg, min(hl, 64))
                return "REDIR", (sb, moved)
            return ax, None

    def decode(self, w, rf):
        p = self.p
        self.w, self.rf = w, rf
        self.rseg = p.claim(24576 + 16)
        dpara = self.kr("DPARA")
        self.dseg = p.claim(dpara * 16 + 16)
        self.ring = p.claim(self.win + 16)
        self.kw("RSEG", self.rseg)
        self.kw("DSEG", self.dseg)
        for i in range(7):
            p.w16("px_k", SVC + 4 * i, K["SVC"] + 2 * i)
        self.rows = []
        self.pos = self.nxt = 0
        p.w16("px_rpos", 0)
        p.w16("px_rend", 0)
        p.w16("px_rbase", 0)
        p.w16("px_rbase", 0, 2)
        ax, cf = p.call(1, di=self.k)
        return (ax if cf else 0), self.rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k")
    ap.add_argument("--cl", type=int, default=512)
    ap.add_argument("--win", type=int, default=16384)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    bad = 0
    n = 0
    for name, data, want in C.corpus():
        ext = name.rsplit(".", 1)[1]
        fmt = P.sniff(data, ext)
        if fmt not in PXF:
            continue
        if a.k and a.k.upper() not in name:
            continue
        n += 1
        try:
            ref = P.decode(data, ext)
            rv = 0
        except P.Refused as e:
            ref, rv = None, e.code
        r = Run(os.path.join(E.ROOT, "build", "pxextra.bin"), a.cl, a.win)
        try:
            v, extra = r.head(data, fmt)
        except Exception as e:                      # noqa: BLE001
            print("FAIL %-14s HEAD crashed: %s" % (name, e))
            bad += 1
            continue
        if v == "REDIR":
            sb, moved = extra
            ok = ref is not None and getattr(ref, "sbase", None) == sb \
                and moved[:4] == b"\x89PNG"
            if rv and rv != 0:
                # the PNG part's own verdict: the guest hands over, and the
                # PNG part refuses - not this part's to check
                ok = moved[:4] == b"\x89PNG"
            print("%s %-14s REDIR at %d%s" % ("ok  " if ok else "FAIL", name, sb,
                                            "" if ok else " (pixelsim %r)" % rv))
            bad += not ok
            continue
        if v != 0:
            ok = v == rv
            if a.v or not ok:
                print("%s %-14s refused %d (%s), pixelsim %d" % (
                    "ok  " if ok else "FAIL", name, v,
                    P.PXD_WORDS[v] if v < len(P.PXD_WORDS) else "?", rv))
            bad += not ok
            continue
        if rv:
            # HEAD took it; DECODE must refuse it as pixelsim does
            w = r.kr("SW")
            dv, rows = r.decode(w, r.p.r8("px_k", K["RF"]))
            ok = dv == rv
            print("%s %-14s decode refused %d, pixelsim %d" % (
                "ok  " if ok else "FAIL", name, dv, rv)) if (a.v or not ok) else None
            bad += not ok
            continue
        # the HEAD's answers
        got = (r.kr("SW"), r.kr("SH"), r.p.r8("px_k", K["RF"]),
               r.p.r8("px_k", K["BITS"]), r.p.r8("px_k", K["PACK"]),
               r.kr("NPAL"), bool(r.p.r8("px_k", K["FLAGS"]) & 1))
        npal = ref.npal
        want_h = (ref.w, ref.h, ref.rf, ref.bits, ref.pack, npal,
                  fmt == "ICO")
        msgs = []
        if got != want_h:
            msgs.append("head %r, pixelsim %r" % (got, want_h))
        if ref.rf == P.RF_IDX:
            gp = r.p.rbytes("px_spal", 768)
            sp = b"".join(bytes(c) for c in ref.pal)
            sp += b"\0" * (768 - len(sp))
            if gp != sp:
                i = next(i for i in range(768) if gp[i] != sp[i])
                msgs.append("palette differs at byte %d: %d vs %d" % (
                    i, gp[i], sp[i]))
        if r.heads != getattr(ref, "heads", r.heads):
            msgs.append("heads %d, pixelsim %d" % (r.heads, ref.heads))
        dv, rows = r.decode(ref.w, ref.rf)
        if dv:
            msgs.append("decode refused %d" % dv)
        elif rows != ref.rows:
            if len(rows) != len(ref.rows):
                msgs.append("%d rows, pixelsim %d" % (len(rows), len(ref.rows)))
            else:
                for (gy, gr), (sy, sr) in zip(rows, ref.rows):
                    if gy != sy or gr != sr:
                        d = next((i for i in range(min(len(gr), len(sr)))
                                  if gr[i] != sr[i]), None)
                        msgs.append("row y %d/%d differs at byte %s: %s vs %s" % (
                            gy, sy, d, gr[d:d + 6].hex() if d is not None else "",
                            sr[d:d + 6].hex() if d is not None else ""))
                        break
        ok = not msgs
        if a.v or not ok:
            print("%s %-14s %s" % ("ok  " if ok else "FAIL", name,
                                   "; ".join(msgs) or "%dx%d" % (ref.w, ref.h)))
        bad += not ok
    print("pxextraemu: %d fixtures, %s" % (n, "all pixelsim's" if not bad
                                          else "%d FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
