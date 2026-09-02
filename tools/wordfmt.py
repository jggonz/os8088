#!/usr/bin/env python3
"""wordfmt.py - read a Word for Windows 1.x/2.x .DOC and dump what is in it.

Usage: wordfmt.py FILE.DOC [--wtx]

There is no copy of Word here to open apps/word's output with, and 'it
round-trips through the app that wrote it' proves only that the app is
self-consistent. So this is a SECOND, independent implementation of the
format described in SPEC.md 68.4, written from the same Opus headers -

  wordtech/file.h   struct FIB, cbSector, struct BTE
  wordtech/fkp.h    struct FKP
  wordtech/doc.h    struct SED, struct PCD, clxtPrc / clxtPlcpcd
  wordtech/prm.h    the sprm codes, wordtech/prcsubs.c's dnsprm the widths
  wordtech/props.h  jc*, kul*, stcNormal

- and nothing is shared with tools/os88doc.py, which writes it. Two code
  paths over one specification is what makes a disagreement a signal.

--wtx re-emits the document in the .wtx markup os88doc.py takes, so a
generated file can be diffed against the source it was generated from. That
is what `make wordcheck` does.

What this does NOT establish is that a running Word 1.1a accepts the file.
Nobody here has run one (SPEC.md 68.4.2).
"""

import argparse
import struct
import sys

MAGIC = 0xA59B
SECT = 512
TWIPCELL = 144

# FIB field offsets: 94 + 6n for the nth (FC, uns) pair, fcStshf being n = 0.
F_IDENT, F_NFIB, F_FLAGS, F_NFIBBACK = 0, 2, 10, 12
F_FCMIN, F_FCMAC, F_CBMAC, F_CCPTEXT = 24, 28, 32, 52
F_STSH, F_SED = 94, 124
F_BTECHP, F_BTEPAP = 160, 166
F_DOP, F_CLX = 262, 274
F_PNCHP, F_PNPAP, F_CPNCHP, F_CPNPAP = 306, 308, 310, 312

FCOMPLEX = 0x0004

# attribute bits, apps/word/word.asm's WDAT_*
BOLD, ITAL, UL, WUL, DUL, SCAP, HID = 1, 2, 4, 8, 16, 32, 64

# sprm operand widths (dnsprm cch: 1 = none, 2 = a byte, 3 = a word)
PAP_WORD = set(range(16, 23)) | set(range(26, 37))
PAP_VAR = {3, 15, 23, 52}
CHP_WORD = {68, 74}
CHP_THREE = {70}
CHP_NONE = {58}
CHP_VAR = {56, 57}


class Bad(Exception):
    pass


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def i16(b, o):
    return struct.unpack_from('<h', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


class Doc:
    def __init__(self, raw):
        self.raw = raw
        if len(raw) < 384:
            raise Bad('shorter than a header page')
        if u16(raw, F_IDENT) != MAGIC:
            raise Bad('wIdent is %#06x, not %#06x' % (u16(raw, F_IDENT), MAGIC))
        self.nfib = u16(raw, F_NFIB)
        if self.nfib < 18:
            raise Bad('nFib %d is below nFibMinDoc' % self.nfib)
        self.complex = bool(u16(raw, F_FLAGS) & FCOMPLEX)
        self.fcmin = u32(raw, F_FCMIN)
        self.fcmac = u32(raw, F_FCMAC)
        self.ccp = u32(raw, F_CCPTEXT)
        self.pieces = self._pieces()
        self.text = b''.join(raw[fc:fc + n] for fc, _, n in self.pieces)
        self.chp = self._paint_chp()
        self.paps = self._paint_pap()

    # --- the piece table (SPEC.md 68.4.1) ---------------------------------
    def _pieces(self):
        if not self.complex:
            return [(self.fcmin, 0, self.ccp)]
        fc, cb = u32(self.raw, F_CLX), u16(self.raw, F_CLX + 4)
        if not cb:
            raise Bad('fComplex is set but cbClx is 0')
        p, end = fc, fc + cb
        while p < end and self.raw[p] == 1:              # clxtPrc
            p += 3 + u16(self.raw, p + 1)
        if p >= end or self.raw[p] != 2:                 # clxtPlcpcd
            raise Bad('no plcpcd in the CLX')
        cbplc = u16(self.raw, p + 1)
        p += 3
        n = (cbplc - 4) // 12
        if n <= 0:
            raise Bad('an empty piece table')
        cps = [u32(self.raw, p + 4 * i) for i in range(n + 1)]
        pcds = p + 4 * (n + 1)
        out = []
        for i in range(n):
            pfc = u32(self.raw, pcds + 8 * i + 2)
            out.append((pfc, cps[i], cps[i + 1] - cps[i]))
        return out

    # --- the bin tables and the FKP pages ---------------------------------
    def _bte(self, which):
        base = F_BTECHP if which == 'chp' else F_BTEPAP
        fc, cb = u32(self.raw, base), u16(self.raw, base + 4)
        if not cb or cb < 10:
            return []
        n = (cb - 4) // 6
        fcs = [u32(self.raw, fc + 4 * i) for i in range(n + 1)]
        pns = [u16(self.raw, fc + 4 * (n + 1) + 2 * i) for i in range(n)]
        return list(zip(fcs, fcs[1:], pns))

    def _run(self, which, fc):
        """(value, fclim) for the run covering fc. value is the attribute
        byte for 'chp' and the raw PAPX bytes for 'pap'."""
        for lo, hi, pn in self._bte(which):
            if lo <= fc < hi:
                return self._in_page(which, pn, fc)
        return (0 if which == 'chp' else None), fc + 1

    def _in_page(self, which, pn, fc):
        base = pn * SECT
        if base + SECT > len(self.raw):
            raise Bad('FKP page %d is off the end of the file' % pn)
        crun = self.raw[base + SECT - 1]
        for i in range(crun):
            lo = u32(self.raw, base + 4 * i)
            hi = u32(self.raw, base + 4 * (i + 1))
            if lo <= fc < hi:
                b = self.raw[base + 4 * (crun + 1) + i]
                if b == 0:
                    return (0 if which == 'chp' else None), hi
                off = base + 2 * b                       # the offset is WORDS
                if which == 'chp':
                    cb = self.raw[off]
                    return chp_attr(self.raw[off + 1:off + 1 + cb]), hi
                cw = self.raw[off] * 2
                return self.raw[off:off + cw], hi
        return (0 if which == 'chp' else None), fc + 1

    def _paint_chp(self):
        out = bytearray(len(self.text))
        for pfc, cp, n in self.pieces:
            fc, i, left = pfc, cp, n
            while left > 0:
                a, lim = self._run('chp', fc)
                step = max(1, min(lim - fc, left))
                for k in range(step):
                    if i + k < len(out):
                        out[i + k] = a
                fc += step
                i += step
                left -= step
        return bytes(out)

    def _fc_of(self, cp):
        for pfc, c0, n in self.pieces:
            if c0 <= cp < c0 + n:
                return pfc + (cp - c0)
        return None

    def _paint_pap(self):
        """[(pap tuple, cp of the mark)], one per paragraph."""
        out = []
        start = 0
        for cp in range(len(self.text)):
            if self.text[cp] != 13:
                continue
            fc = self._fc_of(cp)
            papx, _ = self._run('pap', fc) if fc is not None else (None, 0)
            out.append((pap_of(papx), start, cp))
            start = cp + 1
        return out


def chp_attr(g):
    a, i = 0, 0
    while i < len(g):
        s = g[i]
        i += 1
        if s in (60, 61, 65, 67):
            on = i < len(g) and (g[i] & 1)
            i += 1
            if on:
                a |= {60: BOLD, 61: ITAL, 65: SCAP, 67: HID}[s]
        elif s == 69:
            v = g[i] if i < len(g) else 0
            i += 1
            a &= ~(UL | WUL | DUL)
            a |= {1: UL, 2: WUL, 3: DUL}.get(v, 0)
        elif s in CHP_NONE:
            pass
        elif s in CHP_VAR:
            i += (g[i] if i < len(g) else 0) + 1
        elif s in CHP_WORD:
            i += 2
        elif s in CHP_THREE:
            i += 3
        else:
            i += 1
    return a


def pap_of(papx):
    """A PAPX -> the (packed, left, first, right) tuple os88doc.py builds."""
    align = spacing = before = left = first = right = 0
    if papx:
        cw = papx[0] * 2
        g = papx[8:cw]                       # past cw, stc and the 6-byte PHE
        i = 0
        while i < len(g):
            s = g[i]
            i += 1
            if s in PAP_VAR:
                i += (g[i] if i < len(g) else 0) + 1
                continue
            w = 2 if s in PAP_WORD else 1
            if i + w > len(g):
                break
            if s == 5:
                align = g[i] & 3
            elif s == 17:
                left = i16(g, i) // TWIPCELL
            elif s == 19:
                first = i16(g, i) // TWIPCELL
            elif s == 16:
                right = i16(g, i) // TWIPCELL
            elif s == 20:
                v = abs(i16(g, i))
                spacing = 0 if v < 300 else 1 if v < 420 else 2
            elif s == 21:
                before = 1 if i16(g, i) else 0
            i += w
    return (align | (spacing << 2) | (before << 4),
            left & 0xFF, first & 0xFF, right & 0xFF)


SPANS = [(BOLD, 'b'), (ITAL, 'i'), (UL, 'u'), (WUL, 'w'), (DUL, 'd'),
         (SCAP, 'k')]


def to_wtx(doc):
    """Re-emit the document in os88doc.py's markup."""
    lines = []
    # The file's LAST paragraph mark is this port's tail paragraph
    # (SPEC.md 68.4), so it contributes no line of its own.
    paras = doc.paps
    for n, (pap, start, mark) in enumerate(paras):
        packed, left, first, right = pap
        d = ''
        a = packed & 3
        if a:
            d += {1: '.c ', 2: '.r ', 3: '.j '}[a]
        sp = (packed >> 2) & 3
        if sp:
            d += {1: '.sp15 ', 2: '.sp2 '}[sp]
        if packed & 0x10:
            d += '.open '
        for tok, v in (('.li', left), ('.fi', first), ('.ri', right)):
            if v:
                d += '%s %d ' % (tok, v - 256 if v > 127 else v)
        body, attr = '', 0
        for cp in range(start, mark):
            want = doc.chp[cp] & (BOLD | ITAL | UL | WUL | DUL | SCAP)
            if want != attr:
                for bit, tag in SPANS:
                    if (attr & bit) and not (want & bit):
                        body += '{/%s}' % tag
                for bit, tag in SPANS:
                    if (want & bit) and not (attr & bit):
                        body += '{%s}' % tag
                attr = want
            body += chr(doc.text[cp])
        for bit, tag in SPANS:
            if attr & bit:
                body += '{/%s}' % tag
        lines.append(d + body)
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--wtx', action='store_true',
                    help='re-emit as os88doc.py markup')
    args = ap.parse_args()
    raw = open(args.file, 'rb').read()
    try:
        doc = Doc(raw)
    except Bad as e:
        sys.stderr.write('wordfmt: %s: %s\n' % (args.file, e))
        return 1
    if args.wtx:
        sys.stdout.write(to_wtx(doc) + '\n')
        return 0
    print('%s: nFib %d, %s, %d bytes, %d characters, %d paragraphs'
          % (args.file, doc.nfib,
             'complex (%d pieces)' % len(doc.pieces) if doc.complex
             else 'simple', len(raw), len(doc.text), len(doc.paps)))
    for pap, start, mark in doc.paps:
        attrs = sorted({tag for cp in range(start, mark)
                        for bit, tag in SPANS if doc.chp[cp] & bit})
        print('  pap%s%s  %r' % (pap, ' [' + ''.join(attrs) + ']' if attrs
                                 else '', doc.text[start:mark][:60]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
