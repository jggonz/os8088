#!/usr/bin/env python3
"""Price a typeface the way kernel/font.inc draws it.

`os88font.py` REFUSES a face that would draw wrong and warns when it looks
expensive; this says *how* expensive, in microseconds, against the machine's own
ROM face.  It exists because the warning it replaces was a single percentage -
"only N of 760 glyph rows are blank (the ROM font is 25%)" - which says a face
is inkier without saying what that costs, and the answer turns out to depend
almost entirely on which renderer the text goes through (SPEC.md 6.1).

WHAT THE RENDERER IS SENSITIVE TO, and it is ROWS and not columns:

  font_char / font_str   `or ah, ah / jz` skips a BLANK GLYPH ROW whole, so a
  (transparent)          face's blank-row fraction is its speed.  Measured on
                         a 4.77MHz 8088: one drawn row is ~15 us on CGA.

  font_run fast path     branch-free - it stores every row whether or not the
  (opaque, mono,         glyph set any bits, because the cell OWNS its
   x & 7 == 0)           framebuffer byte.  A face's ink costs exactly NOTHING
                         here, and this is the path every hot incremental
                         redraw in the tree uses (SPEC.md 12.9, 22.11, 27.2).

  ...and at an UNALIGNED x there is no fast path, so font_run falls back to the
  pair and becomes content-dependent again - with a second term, because the
  low `x & 7` bits of a row spill into a second framebuffer byte (`or al, al /
  jz`).  That is the ONE place a face's COLUMNS cost anything, and it is the
  rightmost ones - which is what SPEC.md 6.1.4's blank column 7 is about.

    python3 tools/os88fontcost.py fonts/tallx.f8
    python3 tools/os88fontcost.py fonts/tallx.f8 --rom BIOS_5150_U33.BIN
    python3 tools/os88fontcost.py fonts/a.f8 --baseline fonts/b.f8

The default baseline is the IBM face's DRAWN-ROW COUNTS, tabulated below - a
derived statistic, not the glyphs, so no BIOS ships in this tree.  Give
`--rom` a real 8KB U33 image (or any dump holding the 8x8 set at F000:FA6E)
for the full per-glyph and spill analysis.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

FIRST, LAST, ROWS = 32, 126, 8          # kernel/font.inc's FONT_FIRST/FONT_LAST
ROM_OFF = 0xFA6E - 0xE000               # F000:FA6E inside an 8KB U33 image

# Measured, tests/gfxbench on a cycle-accurate 4.77MHz 8088 with the real
# IBM 5150 BIOS: FONT_STR over a 10-cell run whose two faces differ by 7 drawn
# rows moved 105.61 us, so one drawn row is 15.09 us.  PERFORMANCE.md's
# five-instruction step (81.0 clocks = 16.97 us) is the same number modelled
# from the other end; the difference is that a SKIPPED row still pays its own
# load, test and branch.
US_PER_ROW = {'cga': 15.09, 'herc': 14.83}      # herc scaled by 79.6/81.0
US_PER_CELL = 840.3                              # FONT_STR aligned, per cell

# Drawn rows (8 - blank) per glyph 32..126 for the IBM 5150 ROM face.
IBM_DRAWN = [
    0, 6, 3, 7, 7, 6, 7, 3, 7, 7, 5, 5, 3, 1, 2, 7,    # 32..47
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 5, 7, 2, 7, 6,    # 48..63
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,    # 64..79
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 1,    # 80..95
    3, 5, 7, 5, 7, 5, 7, 6, 7, 6, 7, 7, 7, 5, 5, 5,    # 96..111
    6, 6, 5, 5, 7, 5, 5, 5, 5, 6, 5, 7, 6, 7, 2,       # 112..126
]
assert len(IBM_DRAWN) == LAST - FIRST + 1 and sum(IBM_DRAWN) == 568


class FontError(Exception):
    pass


def parse_f8(path):
    """-> {code: [8 row bytes]} - os88font.py's reader, kept in step with it."""
    glyphs, code, rows = {}, None, []
    head = re.compile(r'^\s*char\s+(\d+)')
    with open(path) as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip('\n')
            art = line.strip()
            # A PIXEL ROW IS TESTED FOR FIRST - a row of solid ink IS
            # '########', so a comment rule applied first eats it (os88font.py
            # carries the same note and the same bug once).
            isrow = len(art) == ROWS and not (set(art) - set('.#'))
            if not isrow and (not art or art.startswith('#')):
                continue
            m = head.match(line)
            if m:
                if code is not None:
                    glyphs[code] = rows
                code, rows = int(m.group(1)), []
                continue
            if isrow:
                rows.append(sum(0x80 >> i for i, ch in enumerate(art) if ch == '#'))
    if code is not None:
        glyphs[code] = rows
    missing = [c for c in range(FIRST, LAST + 1)
               if c not in glyphs or len(glyphs[c]) != ROWS]
    if missing:
        raise FontError("%s: glyph %d is missing or not %d rows"
                        % (path, missing[0], ROWS))
    return glyphs


def parse_rom(path):
    d = open(path, 'rb').read()
    off = ROM_OFF if len(d) == 8192 else 0xFA6E
    if off + (LAST + 1) * 8 > len(d):
        raise FontError("%s: too small to hold an 8x8 set at F000:FA6E" % path)
    return {c: list(d[off + c * 8: off + c * 8 + 8]) for c in range(FIRST, LAST + 1)}


def drawn(g):
    return sum(1 for b in g if b)


def spill(g, shift):
    """rows whose low `shift` bits are set - each is a second byte written"""
    mask = (1 << shift) - 1
    return sum(1 for b in g if b & mask) if shift else 0


def corpus(root):
    """Character frequency of the text os8088 actually draws.

    Its own string literals - menu items, window titles, status lines, the
    Task Manager's columns.  This is what makes the difference between a face's
    HEADLINE number and the one a session feels: UI text is mostly lowercase,
    and lowercase is usually where two 8x8 faces agree.
    """
    freq = collections.Counter()
    try:
        files = subprocess.run(['git', 'ls-files', '*.inc', '*.asm'],
                               capture_output=True, text=True,
                               cwd=root, timeout=60).stdout.split()
    except Exception:
        return freq
    lit = re.compile(r"'([^']*)'")
    dbl = re.compile(r'^\s*\w*:?\s*db\s')
    for f in files:
        try:
            src = open(os.path.join(root, f), errors='ignore').read()
        except OSError:
            continue
        for line in src.splitlines():
            if not dbl.search(line):
                continue
            for s in lit.findall(line):
                for ch in s:
                    if FIRST <= ord(ch) <= LAST:
                        freq[ord(ch)] += 1
    return freq


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('face', help='the .f8 to price')
    ap.add_argument('--rom', help='an 8KB U33 BIOS image to price against')
    ap.add_argument('--baseline', help='another .f8 to price against instead')
    ap.add_argument('--adapter', choices=sorted(US_PER_ROW), default='cga')
    a = ap.parse_args()

    try:
        F = parse_f8(a.face)
        if a.rom:
            B, bname, full = parse_rom(a.rom), os.path.basename(a.rom), True
        elif a.baseline:
            B, bname, full = parse_f8(a.baseline), os.path.basename(a.baseline), True
        else:
            B = {c: None for c in range(FIRST, LAST + 1)}
            bname, full = 'IBM ROM (tabulated)', False
    except (FontError, OSError) as e:
        sys.exit("os88fontcost: %s" % e)

    bdrawn = ((lambda c: drawn(B[c])) if full
              else (lambda c: IBM_DRAWN[c - FIRST]))
    us = US_PER_ROW[a.adapter]
    name = os.path.basename(a.face)
    # a BIOS dump's file name is routinely 40+ characters and every column
    # below is keyed on these two, so they are clipped once, here.
    short = lambda s: s if len(s) <= 13 else s[:12] + '~'
    name, bname = short(name), short(bname)

    tot = (LAST - FIRST + 1) * ROWS
    fb = tot - sum(drawn(F[c]) for c in range(FIRST, LAST + 1))
    bb = tot - sum(bdrawn(c) for c in range(FIRST, LAST + 1))
    print("os88fontcost: %s against %s, priced on %s (1 drawn row = %.2f us)"
          % (name, bname, a.adapter.upper(), us))
    print()
    print("  blank rows      %-14s %4d/%d = %.1f%%" % (bname, bb, tot, 100.0 * bb / tot))
    print("                  %-14s %4d/%d = %.1f%%" % (name, fb, tot, 100.0 * fb / tot))
    print()

    groups = [('space', [32]), ('digits', list(range(48, 58))),
              ('UPPERCASE', list(range(65, 91))),
              ('lowercase', list(range(97, 123))),
              ('punctuation', list(range(33, 48)) + list(range(58, 65)))]
    print("  rows drawn per glyph, by class")
    print("    %-13s %9s %9s %9s" % ('', bname[:9], name[:9], 'delta'))
    for gname, cs in groups:
        r = sum(bdrawn(c) for c in cs) / len(cs)
        t = sum(drawn(F[c]) for c in cs) / len(cs)
        print("    %-13s %9.2f %9.2f %+9.2f" % (gname, r, t, t - r))
    print()

    freq = corpus(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if freq:
        n = sum(freq.values())
        rw = sum(freq[c] * bdrawn(c) for c in freq) / n
        tw = sum(freq[c] * drawn(F[c]) for c in freq) / n
        d = (tw - rw) * us
        print("  weighted by os8088's own %d characters of UI text:" % n)
        print("    rows/glyph    %-14s %.3f" % (bname, rw))
        print("    rows/glyph    %-14s %.3f  (%+.1f%%)" % (name, tw, 100.0 * (tw - rw) / rw))
        print("    per cell      %+.2f us on a %.1f us cell = %+.2f%%"
              % (d, US_PER_CELL, 100.0 * d / US_PER_CELL))
        print()
        print("  ...and that applies to font_char / font_str ONLY.  On font_run's")
        print("  fast path (mono, x & 7 == 0) the cost is identical for any face:")
        print("  the loop stores every row unconditionally.  MEASURED at +0.00%.")
        print()

    if full:
        print("  second-byte spill, unaligned x (extra framebuffer writes per cell)")
        for sh in (1, 3, 5, 7):
            r = sum(freq[c] * spill(B[c], sh) for c in freq) / max(1, sum(freq.values())) if freq else 0
            t = sum(freq[c] * spill(F[c], sh) for c in freq) / max(1, sum(freq.values())) if freq else 0
            print("    x & 7 = %d   %9.2f %9.2f %+9.2f" % (sh, r, t, t - r))
        bad = [chr(c) for c in range(FIRST, LAST + 1) if any(b & 1 for b in F[c])]
        if bad:
            print("    column 7 is set in %s - SPEC.md 6.1.4 wants it blank"
                  % ''.join(bad))


if __name__ == '__main__':
    main()
