#!/usr/bin/env python3
"""os88doc.py - generate a REAL Word for Windows .DOC (SPEC.md 65.4) from a
minimal line-based markup (.wtx).

Usage: os88doc.py SOURCE.wtx -o OUT.DOC

The output is DETERMINISTIC: the same source produces the same bytes, no
timestamps - the shipped WELCOME.DOC can be rebuilt byte-for-byte.

The file this writes is the Word for Windows 1.x/2.x binary format, laid out
from the Opus headers exactly as apps/word/wddoc.inc lays it out: a 314-byte
FIB in a 384-byte header page (wIdent 0xA59B, nFib 33), the text, CHPX and
PAPX FKP pages with their bin tables, a one-style STSH, a one-section
plcfsed whose SEPX is fcNil, and a DOP. tools/wordfmt.py reads it back -
deliberately a SECOND implementation, so the generator and the verifier are
separate code paths over one specification (SPEC.md 65.4.2).

Markup (.wtx), one PARAGRAPH per line:
  ; comment            a line starting ';' is dropped (';;' escapes a
                       literal leading ';')
  directives           leading '.'-tokens, before the text, in any order:
                         .c / .r / .j      center / right / justified
                         .sp15 / .sp2      1.5 / double line spacing
                         .open             space before (an open paragraph)
                         .li N / .fi N / .ri N
                                           left / first-line (signed) /
                                           right indent, in cells (1 cell =
                                           1/10 inch)
  {b}...{/b}           inline character spans: b bold, i italic, u underline,
                       w word-underline, d double-underline, k small caps
  a literal TAB        byte 9 (the default-stop tab)

The last line is the TAIL paragraph (no closing mark); every other line ends
in a 13 whose CHP byte is that paragraph's PAP index.
"""

import argparse
import struct
import sys

MAGIC = 0xA59B
NFIB = 33                               # nFibCurrent   (wordtech/file.h)
NFIBBACK = 25                           # nFibBackCurrent
PAPMAX = 256
MAXTEXT = 30 * 1024                     # WD_MAXKB (SPEC.md 65.3)
HDR = 384                               # cbFileHeader
SECT = 512                              # cbSector
TWIPCELL = 144                          # one cell = 1/10 inch

ATTR = {'b': 0x01, 'i': 0x02, 'u': 0x04, 'w': 0x08, 'd': 0x10, 'k': 0x20}
ALIGN = {'.c': 1, '.r': 2, '.j': 3}
SPACE = {'.sp15': 1, '.sp2': 2}


def parse_line(line):
    """One source line -> (pap 4-tuple, [(char, attr), ...])."""
    align = spacing = before = left = first = right = 0
    # leading directives
    while True:
        stripped = line.lstrip(' ')
        if not stripped.startswith('.'):
            break
        tok, _, rest = stripped.partition(' ')
        if tok in ALIGN:
            align = ALIGN[tok]
        elif tok in SPACE:
            spacing = SPACE[tok]
        elif tok == '.open':
            before = 1
        elif tok in ('.li', '.fi', '.ri'):
            val, _, rest = rest.lstrip(' ').partition(' ')
            n = int(val)
            if tok == '.li':
                left = n
            elif tok == '.fi':
                first = n
            else:
                right = n
        else:
            raise SystemExit('os88doc: unknown directive %r' % tok)
        line = rest
    if line.startswith(' '):            # one space separates directives from
        line = line[1:]                 # text; further spaces are content

    pap = (align | (spacing << 2) | (before << 4),
           left & 0xFF, first & 0xFF, right & 0xFF)

    chars = []
    attr = 0
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == '{':
            end = line.find('}', i)
            if end < 0:
                raise SystemExit('os88doc: unclosed { in %r' % line)
            tag = line[i + 1:end]
            off = tag.startswith('/')
            key = tag[1:] if off else tag
            if key not in ATTR:
                raise SystemExit('os88doc: unknown span {%s}' % tag)
            if off:
                attr &= ~ATTR[key]
            else:
                attr |= ATTR[key]
            i = end + 1
            continue
        if ch == '\t':
            chars.append((9, attr))
        elif 32 <= ord(ch) <= 126:
            chars.append((ord(ch), attr))
        else:
            raise SystemExit('os88doc: character %r is outside 32..126' % ch)
        i += 1
    return pap, chars


# --- the real file format (SPEC.md 65.4) ------------------------------------

class Fkp:
    """One 512-byte formatting page (wordtech/fkp.h): rgfc[crun+1] file
    offsets, then crun one-byte offsets IN WORDS to the properties, and the
    properties themselves packed down from the end. Byte 511 is crun, which
    is why the property area stops at 510."""

    def __init__(self, fc0):
        self.fcs = [fc0]
        self.offs = []
        self.page = bytearray(SECT)
        self.hi = SECT - 1

    def add(self, fclim, prop):
        n = len(self.offs)
        head = 4 * (n + 2) + (n + 1)
        hi = self.hi
        if prop:
            hi = (self.hi - len(prop)) & ~1
        if hi <= head:
            return False
        self.fcs.append(fclim)
        if prop:
            self.hi = hi
            self.page[hi:hi + len(prop)] = prop
            self.offs.append(hi >> 1)
        else:
            self.offs.append(0)
        return True

    def seal(self):
        off = 0
        for fc in self.fcs:
            self.page[off:off + 4] = struct.pack('<I', fc)
            off += 4
        for b in self.offs:
            self.page[off] = b
            off += 1
        self.page[SECT - 1] = len(self.offs)
        return bytes(self.page)


def chpx(attr):
    """The CHPX for an attribute byte, or b'' for the nil (default) run."""
    g = b''
    if attr & 0x01:
        g += bytes([60, 1])             # sprmCFBold
    if attr & 0x02:
        g += bytes([61, 1])             # sprmCFItalic
    if attr & 0x20:
        g += bytes([65, 1])             # sprmCFSmallCaps
    if attr & 0x40:
        g += bytes([67, 1])             # sprmCFVanish
    kul = 1 if attr & 0x04 else 2 if attr & 0x08 else 3 if attr & 0x10 else 0
    if kul:
        g += bytes([69, kul])           # sprmCKul
    return bytes([len(g)]) + g if g else b''


def papx(pap):
    """The PAPX for a PAP dictionary entry: cw (a count of WORDS - nFib >= 25),
    stc, a PHE with fUnk set, then the grpprl."""
    packed, left, first, right = pap
    first = first - 256 if first > 127 else first
    g = b''
    jc = packed & 3
    if jc:
        g += bytes([5, jc])                                  # sprmPJc
    if left:
        g += bytes([17]) + struct.pack('<h', left * TWIPCELL)
    if first:
        g += bytes([19]) + struct.pack('<h', first * TWIPCELL)
    if right:
        g += bytes([16]) + struct.pack('<h', right * TWIPCELL)
    sp = (packed >> 2) & 3
    if sp:
        g += bytes([20]) + struct.pack('<h', 240 + 120 * sp)
    if packed & 0x10:
        g += bytes([21]) + struct.pack('<h', 120)
    body = bytes([0]) + struct.pack('<HHH', 2, 0, 0) + g     # stc, PHE
    cb = (len(g) + 9) & ~1
    out = bytes([cb >> 1]) + body
    return out + b'\x00' * (cb - len(out))


def pack_runs(runs, fc0):
    """runs = [(fclim, prop)] -> ([page bytes], [each page's first fc])."""
    pages, firsts = [], []
    fkp = Fkp(fc0)
    firsts.append(fc0)
    for fclim, prop in runs:
        if not fkp.add(fclim, prop):
            pages.append(fkp.seal())
            fkp = Fkp(fkp.fcs[-1])
            firsts.append(fkp.fcs[0])
            if not fkp.add(fclim, prop):
                raise SystemExit('os88doc: a run too big for one FKP page')
    pages.append(fkp.seal())
    return pages, firsts


def build_doc(text, chp, paps, tail):
    # A Word file's text ends with a paragraph mark and its N marks are N
    # paragraphs; this document model holds N-1 marks and a TAIL paragraph
    # (SPEC.md 65.3/65.4), so the two differ by exactly this one character.
    text = text + b'\r'
    chp = chp + bytes([tail])

    def attr_at(i):
        return 0 if text[i] == 13 else chp[i]

    chp_runs = []
    i = 0
    while i < len(text):
        a = attr_at(i)
        j = i + 1
        while j < len(text) and attr_at(j) == a:
            j += 1
        chp_runs.append((HDR + j, chpx(a)))
        i = j

    pap_runs = []
    i = 0
    while i < len(text):
        j = i
        while j < len(text) and text[j] != 13:
            j += 1
        idx = chp[j] if j < len(text) else tail
        j = min(j + 1, len(text))
        pap_runs.append((HDR + j, papx(paps[idx])))
        i = j

    chp_pages, chp_first = pack_runs(chp_runs, HDR)
    pap_pages, pap_first = pack_runs(pap_runs, HDR)

    out = bytearray(HDR)
    out += text
    while len(out) % SECT:
        out.append(0)

    pn_chp = len(out) // SECT
    for p in chp_pages:
        out += p
    pn_pap = len(out) // SECT
    for p in pap_pages:
        out += p

    fc_stsh = len(out)
    out += struct.pack('<h', 0)                 # cstcStd
    out += struct.pack('<H', 2 + 1 + 6) + bytes([6]) + b'Normal'
    out += struct.pack('<H', 2 + 1) + bytes([255])
    out += struct.pack('<H', 2 + 1) + bytes([255])
    out += struct.pack('<H', 1) + bytes([0, 0])  # the PLESTCP
    cb_stsh = len(out) - fc_stsh

    fc_sed = len(out)
    out += struct.pack('<II', 0, len(text))      # the section's CP bounds
    out += struct.pack('<HI', 0, 0xFFFFFFFF)     # SED: fcSepx = fcNil
    cb_sed = len(out) - fc_sed

    def bintable(firsts, pn0):
        b = b''.join(struct.pack('<I', fc) for fc in firsts)
        b += struct.pack('<I', HDR + len(text))
        b += b''.join(struct.pack('<H', pn0 + k) for k in range(len(firsts)))
        return b

    fc_btechp = len(out)
    out += bintable(chp_first, pn_chp)
    cb_btechp = len(out) - fc_btechp
    fc_btepap = len(out)
    out += bintable(pap_first, pn_pap)
    cb_btepap = len(out) - fc_btepap

    fc_dop = len(out)
    dop = bytearray(66)
    for off, val in ((10, 15840), (12, 12240), (14, 1440), (16, 1800),
                     (18, 1440), (20, 1800), (24, 720)):
        dop[off:off + 2] = struct.pack('<H', val)
    out += dop
    cb_dop = len(dop)

    fib = bytearray(HDR)
    def w(off, val):
        fib[off:off + 2] = struct.pack('<H', val & 0xFFFF)
    def d(off, val):
        fib[off:off + 4] = struct.pack('<I', val)
    w(0, MAGIC)
    w(2, NFIB)
    w(12, NFIBBACK)
    d(24, HDR)                                   # fcMin
    d(28, HDR + len(text))                       # fcMac
    d(32, len(out))                              # cbMac
    d(52, len(text))                             # ccpText
    d(88, fc_stsh); w(92, cb_stsh)               # fcStshfOrig
    d(94, fc_stsh); w(98, cb_stsh)               # fcStshf
    d(124, fc_sed); w(128, cb_sed)               # fcPlcfsed   (n = 5)
    d(160, fc_btechp); w(164, cb_btechp)         # fcPlcfbteChpx (n = 11)
    d(166, fc_btepap); w(170, cb_btepap)         # fcPlcfbtePapx (n = 12)
    d(262, fc_dop); w(266, cb_dop)               # fcDop       (n = 28)
    w(306, pn_chp); w(308, pn_pap)               # pnChpFirst / pnPapFirst
    w(310, len(chp_pages)); w(312, len(pap_pages))
    out[0:HDR] = fib
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('-o', '--output', required=True)
    args = ap.parse_args()

    with open(args.source, 'r', encoding='ascii') as f:
        raw = f.read().split('\n')
    if raw and raw[-1] == '':           # a trailing newline is not an extra
        raw.pop()                       # empty paragraph
    lines = []
    for line in raw:
        if line.startswith(';;'):
            lines.append(line[1:])
        elif line.startswith(';'):
            continue
        else:
            lines.append(line)
    if not lines:
        raise SystemExit('os88doc: empty source')

    paps = [(0, 0, 0, 0)]               # entry 0 IS Normal (SPEC.md 65.3)
    text = bytearray()
    chp = bytearray()
    tail = 0
    for n, line in enumerate(lines):
        pap, chars = parse_line(line)
        if pap in paps:
            idx = paps.index(pap)
        else:
            if len(paps) >= PAPMAX:
                raise SystemExit('os88doc: more than %d paragraph formats'
                                 % PAPMAX)
            paps.append(pap)
            idx = len(paps) - 1
        for code, attr in chars:
            text.append(code)
            chp.append(attr)
        if n < len(lines) - 1:          # the mark carries its paragraph's
            text.append(13)             ; chp.append(idx)
        else:
            tail = idx                  # the tail has no mark (SPEC.md 65.4)

    if len(text) > MAXTEXT:
        raise SystemExit('os88doc: %d bytes of text; the ceiling is %d'
                         % (len(text), MAXTEXT))

    image = build_doc(bytes(text), bytes(chp), paps, tail)
    with open(args.output, 'wb') as f:
        f.write(image)
    sys.stderr.write('os88doc: %d chars, %d formats -> %s (%d bytes)\n'
                     % (len(text), len(paps), args.output, len(image)))


if __name__ == '__main__':
    main()
