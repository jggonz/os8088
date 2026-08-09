#!/usr/bin/env python3
"""Turn a readable 8x8 typeface into the kernel's baked-in glyph table.

`make FONT=<name>` builds a kernel whose typeface is `fonts/<name>.f8` rather
than whatever the machine's ROM happens to hold (SPEC.md 6.2).  This is the
host half of that: it reads the `.f8`, refuses a font that would draw wrong or
cost more than the ROM's, and writes the `db` rows the kernel `%include`s into
its boot overlay.

The source is ASCII art on purpose.  760 bytes of hex is not reviewable and a
typeface is the one asset in this tree whose defects are entirely visual - a
letter with a hole in it is obvious in `.` and `#` and invisible in `0x6C`.
`--preview` renders a proof sheet, including a CGA row: 640x200 pixels are
2.4:1 tall, so a face that is balanced on VGA can be spindly there, and that
is the adapter it will be read on.

    python3 tools/os88font.py fonts/os8088.f8 -o build/font8x8.inc
    python3 tools/os88font.py fonts/os8088.f8 --preview /tmp/sheet.png

The `.f8` format, all of it:

    # comments and blank lines are ignored anywhere
    char 65 'A'          <- the code point; the rest of the line is a comment
    .####...             <- exactly 8 rows of exactly 8 '.' or '#'
    ##..##..
    ...

Glyphs 32..126 must all be present and nothing outside that range may appear:
the kernel's table is FONT_FIRST..FONT_LAST and a `.f8` that disagrees with it
would be copied into the wrong 760 bytes.
"""
import argparse
import re
import sys

FIRST, LAST = 32, 126          # kernel/font.inc's FONT_FIRST / FONT_LAST
ROWS = 8

# What the tree already assumes about the shapes, and therefore what a new
# face may not quietly break.  Both are SPEC.md 6.2 rules and both are
# WARNINGS rather than errors: they cost speed and looks, not correctness, and
# a font being tried out should render before it is defended.
COL7_OK = ('_',)               # SPEC.md 6.1.4 - column 7 is the inter-glyph
                               # gap, so a run at an unaligned x spills nothing
                               # into a second framebuffer byte.  '_' is the
                               # one exception worth having: underscores have
                               # to join across cells or a file name looks
                               # broken.
BLANK_ROW_FLOOR = 0.20         # a blank row costs nothing to draw (font.inc's
                               # bb path skips it whole), so an unusually inky
                               # face is an unusually slow one.  The ROM's is
                               # 25%.


class FontError(Exception):
    pass


def parse(path):
    """-> {code: [8 ints]}, raising on anything ambiguous."""
    glyphs, code, rows = {}, None, []
    head = re.compile(r'^\s*char\s+(\d+)')
    with open(path) as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip('\n')
            art = line.strip()
            # A PIXEL ROW IS TESTED FOR FIRST, and that ordering is the whole
            # of why this is not a one-line `startswith('#')`: a row of solid
            # ink IS `########`, so a comment rule applied first eats it and
            # the glyph silently arrives six rows tall.  Which is exactly what
            # happened on this file's first run.
            row = len(art) == ROWS and not (set(art) - set('.#'))
            if not row and (not art or art.startswith('#')):
                continue
            m = head.match(line)
            if m:
                if code is not None and len(rows) != ROWS:
                    raise FontError("%s:%d: char %d has %d rows, not %d"
                                    % (path, n, code, len(rows), ROWS))
                code, rows = int(m.group(1)), []
                if code in glyphs:
                    raise FontError("%s:%d: char %d appears twice"
                                    % (path, n, code))
                glyphs[code] = rows
                continue
            if code is None:
                raise FontError("%s:%d: pixel row before any `char` line"
                                % (path, n))
            if not row:
                raise FontError("%s:%d: a row is exactly %d of '.' and '#', "
                                "got %r" % (path, n, ROWS, art))
            if len(rows) == ROWS:
                raise FontError("%s:%d: char %d has more than %d rows"
                                % (path, n, code, ROWS))
            rows.append(sum(0x80 >> i for i, ch in enumerate(art) if ch == '#'))
    if code is not None and len(rows) != ROWS:
        raise FontError("%s: char %d ends with %d rows, not %d"
                        % (path, code, len(rows), ROWS))

    want = set(range(FIRST, LAST + 1))
    missing, extra = want - set(glyphs), set(glyphs) - want
    if missing:
        raise FontError("%s: no glyph for %s"
                        % (path, ', '.join(str(c) for c in sorted(missing))))
    if extra:
        raise FontError("%s: %s is outside the kernel's %d..%d table"
                        % (path, ', '.join(str(c) for c in sorted(extra)),
                           FIRST, LAST))
    return glyphs


def check(glyphs, quiet=False):
    """The two properties the rest of the tree costs itself against."""
    warn = []
    spill = [c for c in range(FIRST, LAST + 1)
             if chr(c) not in COL7_OK and any(b & 1 for b in glyphs[c])]
    if spill:
        warn.append("column 7 is set in %s - SPEC.md 6.1.4 wants it blank so "
                    "an unaligned run stays inside one framebuffer byte"
                    % ', '.join(repr(chr(c)) for c in spill))
    if glyphs[32] != [0] * ROWS:
        warn.append("the space (32) is not blank - font_run paints the "
                    "background with it, so an inky space is an opaque block")
    total = ROWS * (LAST - FIRST + 1)
    blank = sum(1 for c in range(FIRST, LAST + 1) for b in glyphs[c] if not b)
    if blank < BLANK_ROW_FLOOR * total:
        warn.append("only %d of %d glyph rows are blank (%.0f%%, the ROM font "
                    "is 25%%) - a blank row is the one the renderer skips "
                    "whole, so this face costs more per cell"
                    % (blank, total, 100.0 * blank / total))
    if not quiet:
        for w in warn:
            sys.stderr.write("os88font: warning - %s\n" % w)
    return blank, total


def emit(glyphs, name, src):
    out = ["; GENERATED by tools/os88font.py from %s - do not edit." % src,
           "; The kernel's 8x8 typeface, glyphs %d..%d, 8 rows each, row 0"
           % (FIRST, LAST),
           "; first and bit 7 leftmost (SPEC.md 6.2).  %s" % name,
           ""]
    for c in range(FIRST, LAST + 1):
        art = ''.join('#' if glyphs[c][0] & (0x80 >> i) else '.'
                      for i in range(ROWS))
        out.append("    db  %s   ; %3d %-4s %s"
                   % (','.join("0x%02X" % b for b in glyphs[c]), c,
                      repr(chr(c)), art))
    return '\n'.join(out) + '\n'


# --- the proof sheet ---------------------------------------------------------

SAMPLES = ["Locator  File  Edit  View  Special",
           "Disk B:  14 files, 63K free",
           "The quick brown fox jumps over the lazy dog",
           "MY_FILE.TXT  {a+b}*(c-d)/2 = 100%  @#&|~^",
           "0123456789  Il1 O0 S5 Z2 B8  \"quoted\"  'x'"]


def render(glyphs, path, zoom=2, cga=False):
    """A specimen sheet as a PNG, with no dependency but this file."""
    lines = []
    for row in range(FIRST, LAST + 1, 32):        # the table itself, 32/line
        lines.append(''.join(chr(c) for c in range(row, min(row + 32, LAST + 1))))
    lines.append('')
    lines.extend(SAMPLES)

    w = 8 * max(len(s) for s in lines) + 16
    h = 8 * len(lines) + 16
    bits = [bytearray(w) for _ in range(h)]
    for i, s in enumerate(lines):
        for j, ch in enumerate(s):
            gl = glyphs.get(ord(ch), [0] * ROWS)
            for r in range(ROWS):
                for k in range(ROWS):
                    if gl[r] & (0x80 >> k):
                        bits[8 + i * 8 + r][8 + j * 8 + k] = 1

    ys = zoom * (3 if cga else 1)                 # CGA 640x200: 2.4:1 pixels
    rows = []
    for line in bits:
        px = bytearray()
        for v in line:
            px.extend([0 if v else 255] * zoom)
        rows.extend([bytes(px)] * ys)
    _png(path, w * zoom, h * ys, rows)


def _png(path, w, h, rows):
    import struct
    import zlib
    raw = b''.join(b'\x00' + r for r in rows)

    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I',
                                                              zlib.crc32(c))
    with open(path, 'wb') as fh:
        fh.write(b'\x89PNG\r\n\x1a\n')
        fh.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)))
        fh.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        fh.write(chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('source', help='a fonts/*.f8 typeface')
    ap.add_argument('-o', '--out', help='write the NASM include here')
    ap.add_argument('--preview', metavar='PNG', help='render a proof sheet')
    ap.add_argument('--cga', action='store_true',
                    help='preview with CGA 640x200 pixel aspect (2.4:1 tall)')
    ap.add_argument('--zoom', type=int, default=2)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    try:
        glyphs = parse(a.source)
    except (FontError, IOError) as e:
        sys.stderr.write("os88font: %s\n" % e)
        return 1
    blank, total = check(glyphs, a.quiet)

    if a.out:
        name = a.source.rsplit('/', 1)[-1]
        with open(a.out, 'w') as fh:
            fh.write(emit(glyphs, name, a.source))
        if not a.quiet:
            print("os88font: %s -> %s (%d glyphs, %d bytes, %d%% blank rows)"
                  % (a.source, a.out, LAST - FIRST + 1, total, 100 * blank
                     // total))
    if a.preview:
        render(glyphs, a.preview, a.zoom, a.cga)
        if not a.quiet:
            print("os88font: proof sheet -> %s" % a.preview)
    return 0


if __name__ == '__main__':
    sys.exit(main())
