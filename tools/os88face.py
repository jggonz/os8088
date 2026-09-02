#!/usr/bin/env python3
"""Turn a readable proportional typeface into a `.F88` face file (SPEC.md 6.4).

This is the host half of SPEC.md 6.3's typesetting: `faces/<name>.t88` is ASCII
art with a per-glyph advance, and this reads it, refuses a face the library or
the applications could not use, and writes the binary the packages read off the
floppy.

    python3 tools/os88face.py faces/charter12.t88 -o build/CHARTER.F88
    python3 tools/os88face.py faces/charter12.t88 --preview /tmp/sheet.png --cga

The art is the source for the same reason tools/os88font.py's is (SPEC.md 6.2):
a typeface is the one asset in this tree whose defects are entirely visual, and
a letter with a hole in it is obvious in `.` and `#` and invisible in `0x6C`.
`--preview` sets real sentences in the face, at the real advances; `--cga`
renders them at 640x200's 2.4:1 pixel aspect, because that is the adapter this
is most likely to be read on and a face that is balanced on VGA is spindly
there.

The `.t88` format, all of it:

    # comments and blank lines are ignored anywhere
    face Charter            <- family name, <= 16 chars, what a Font menu shows
    rows 12                 <- cell height, 8..16, EVEN
    ascent 9                <- baseline row from the top of the cell
    leading 2               <- extra rows between baselines; rows+leading EVEN
    aspect 0                <- 0 square, 1 Hercules, 2 CGA

    style regular           <- regular | bold | italic | bolditalic
    psname Charter          <- the base-14 name an exporter should ask for
    char 65 'A' adv 8 em 667
    ..####..                <- exactly `rows` lines of exactly 8 (or 16) chars
    ..####..
    ...

`adv` is the pen advance in pixels and MUST BE EVEN (SPEC.md 6.4): a pen that
starts even and advances evenly only ever lands on four of the eight bit
phases, which halves the library's pre-shifted table. `em` is the same advance
in AFM 1/1000-em units and defaults to `adv * 1000 / rows`, the em square being
the cell.
"""
import argparse
import re
import sys

FIRST, LAST = 32, 126
NGLYPH = LAST - FIRST + 1
STYLES = ('regular', 'bold', 'italic', 'bolditalic')
MINADV = 4                      # TY_MINADV: Word's WD_MAXCOL is sized from it
MAXOVER = 2                     # the most a glyph may overhang its advance
HDRSZ = 64
BLANK_ROW_FLOOR = 0.20


class FaceError(Exception):
    pass


# --- the source --------------------------------------------------------------

def parse(path):
    """-> (head dict, {style: {code: (adv, em, [row ints])}})"""
    head = {'face': None, 'rows': None, 'ascent': None,
            'leading': 0, 'aspect': 0}
    styles, cur, code, art, meta = {}, None, None, [], None
    psnames = {}
    hdr = re.compile(r"^\s*char\s+(\d+)(?:\s+'.'|\s+\"[^\"]*\")?"
                     r"\s+adv\s+(\d+)(?:\s+em\s+(\d+))?\s*$")
    width = [None]

    def endglyph(n):
        if code is None:
            return
        if len(art) != head['rows']:
            raise FaceError("%s:%d: char %d has %d rows, not %d"
                            % (path, n, code, len(art), head['rows']))
        styles[cur][code] = (meta[0], meta[1], list(art))

    with open(path) as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip('\n')
            s = line.strip()
            # A PIXEL ROW IS TESTED FOR FIRST, exactly as in os88font.py and
            # for the same reason: a row of solid ink IS `########`, so a
            # comment rule applied first eats it and the glyph silently
            # arrives short.
            isrow = (s and not (set(s) - set('.#'))
                     and (width[0] is None or len(s) == width[0])
                     and len(s) in (8, 16))
            if isrow and code is not None:
                if width[0] is None:
                    width[0] = len(s)
                if len(art) >= head['rows']:
                    raise FaceError("%s:%d: char %d has more than %d rows"
                                    % (path, n, code, head['rows']))
                art.append(sum(1 << (width[0] - 1 - i)
                               for i, ch in enumerate(s) if ch == '#'))
                continue
            if not s or s.startswith('#'):
                continue
            m = hdr.match(line)
            if m:
                if cur is None:
                    raise FaceError("%s:%d: a char before any `style` line"
                                    % (path, n))
                endglyph(n)
                code = int(m.group(1))
                adv = int(m.group(2))
                em = int(m.group(3)) if m.group(3) else None
                if code in styles[cur]:
                    raise FaceError("%s:%d: char %d appears twice in style %s"
                                    % (path, n, code, cur))
                art, meta = [], (adv, em)
                continue
            word = s.split()
            key = word[0]
            if key == 'style':
                endglyph(n)
                code, art = None, []
                cur = word[1]
                if cur not in STYLES:
                    raise FaceError("%s:%d: style %r is not one of %s"
                                    % (path, n, cur, ', '.join(STYLES)))
                styles.setdefault(cur, {})
                continue
            if key == 'psname':
                if cur is None:
                    raise FaceError("%s:%d: psname before any `style`"
                                    % (path, n))
                psnames[cur] = ' '.join(word[1:])
                continue
            if key == 'face':
                head['face'] = ' '.join(word[1:])
                continue
            if key in ('rows', 'ascent', 'leading', 'aspect'):
                head[key] = int(word[1])
                continue
            raise FaceError("%s:%d: %r is not a .t88 keyword" % (path, n, key))
        endglyph(n)

    if head['face'] is None or head['rows'] is None or head['ascent'] is None:
        raise FaceError("%s: `face`, `rows` and `ascent` are all required"
                        % path)
    head['stride'] = (width[0] or 8) // 8
    return head, styles, psnames


# --- what the library and the applications rely on ---------------------------

def check(head, styles, quiet=False):
    rows, warn = head['rows'], []
    if not 8 <= rows <= 16 or rows % 2:
        raise FaceError("rows is %d - SPEC.md 6.4 wants 8..16 and EVEN "
                        "(font.inc's dither phase, and the row advance)"
                        % rows)
    if not 1 <= head['ascent'] <= rows:
        raise FaceError("ascent %d is outside 1..%d" % (head['ascent'], rows))
    if not 0 <= head['leading'] <= 8 or (rows + head['leading']) % 2:
        raise FaceError("leading %d gives an odd row advance"
                        % head['leading'])
    if 'regular' not in styles:
        raise FaceError("no `style regular` - style 0 must be present")
    if len(head['face']) > 16:
        raise FaceError("family name %r is longer than 16 characters"
                        % head['face'])

    maxadv, minadv, maxover = 0, 99, 0
    for name, gl in sorted(styles.items()):
        want = set(range(FIRST, LAST + 1))
        missing = want - set(gl)
        extra = set(gl) - want
        if missing:
            raise FaceError("style %s: no glyph for %s" % (name, ', '.join(
                str(c) for c in sorted(missing))))
        if extra:
            raise FaceError("style %s: %s is outside %d..%d" % (
                name, ', '.join(str(c) for c in sorted(extra)), FIRST, LAST))
        if any(gl[32][2]):
            raise FaceError("style %s: the space (32) is not blank" % name)
        for c in range(FIRST, LAST + 1):
            adv, em, art = gl[c]
            if adv % 2:
                raise FaceError("style %s: char %d has an ODD advance %d - "
                                "SPEC.md 6.4 requires even, so the pen only "
                                "ever lands on four bit phases"
                                % (name, c, adv))
            if not MINADV <= adv <= head['stride'] * 8:
                raise FaceError("style %s: char %d advance %d is outside "
                                "%d..%d (TY_MINADV is what Word's WD_MAXCOL "
                                "is sized from)"
                                % (name, c, adv, MINADV, head['stride'] * 8))
            w = head['stride'] * 8
            for r in art:
                for k in range(w):
                    if r & (1 << (w - 1 - k)):
                        over = k + 1 - adv
                        if over > maxover:
                            maxover = over
            maxadv, minadv = max(maxadv, adv), min(minadv, adv)
    if maxover > MAXOVER:
        raise FaceError("a glyph overhangs its advance by %d pixels; "
                        "SPEC.md 6.4 allows %d" % (maxover, MAXOVER))

    total = rows * NGLYPH
    blank = sum(1 for c in range(FIRST, LAST + 1)
                for b in styles['regular'][c][2] if not b)
    if blank < BLANK_ROW_FLOOR * total:
        warn.append("only %d of %d rows of the regular style are blank "
                    "(%.0f%%) - a blank row is one the compose still pays "
                    "for here, but an inky face reads heavy on a 1bpp "
                    "adapter" % (blank, total, 100.0 * blank / total))
    # ...and the stem check is LETTERFORMS ONLY. SPEC.md 6.2's 2px finding is
    # about how a stroke reads against a 2.4:1 CGA pixel in a letter; `#`,
    # `+`, `|`, `/` and their kind are drawn 1px by everybody, including the
    # ROM font, and warning about them would train the reader to ignore the
    # warning that matters.
    for name, gl in sorted(styles.items()):
        thin = [c for c in range(FIRST, LAST + 1)
                if chr(c).isalnum()
                and _has_1px_stem(gl[c][2], head['stride'] * 8)]
        if thin:
            warn.append("style %s has a 1-pixel vertical stem in %s - on CGA "
                        "640x200 the pixels are 2.4:1 tall and a 1px stem "
                        "reads far thinner than a 1px bar (SPEC.md 6.2)"
                        % (name, ', '.join(repr(chr(c)) for c in thin[:8])
                           + (', ...' if len(thin) > 8 else '')))
    if not quiet:
        for w in warn:
            sys.stderr.write("os88face: warning - %s\n" % w)
    return dict(maxadv=maxadv, minadv=minadv, maxover=maxover,
                blank=blank, total=total)


def _has_1px_stem(art, w):
    """A column of ink >= 3 rows tall with nothing either side of it."""
    for k in range(w):
        run = 0
        for r in art:
            here = r & (1 << (w - 1 - k))
            left = r & (1 << (w - k)) if k else 0
            right = r & (1 << (w - 2 - k)) if k < w - 1 else 0
            run = run + 1 if (here and not left and not right) else 0
            if run >= 3:
                return True
    return False


# --- the binary --------------------------------------------------------------

def build(head, styles, psnames, facts):
    import struct
    rows, stride = head['rows'], head['stride']
    blocks, off = [], HDRSZ
    bmoff, mtoff, psoff = [0] * 4, [0] * 4, [0] * 4

    for i, name in enumerate(STYLES):
        if name not in styles:
            continue
        gl = styles[name]
        bm = bytearray()
        for c in range(FIRST, LAST + 1):
            art = gl[c][2]
            for col in range(stride):           # a 16px glyph is two 8px
                sh = 8 * (stride - 1 - col)     # half-cells, left one first
                for r in art:
                    bm.append((r >> sh) & 0xFF)
        bmoff[i] = off
        blocks.append(bytes(bm))
        off += len(bm)

        mt = bytearray(bytes(gl[c][0] for c in range(FIRST, LAST + 1)))
        mt.append(0)                            # pad to even
        for c in range(FIRST, LAST + 1):
            adv, em, _ = gl[c]
            mt += struct.pack('<H', em if em else (adv * 1000 + rows // 2)
                              // rows)
        mtoff[i] = off
        blocks.append(bytes(mt))
        off += len(mt)

        if psnames.get(name):
            psoff[i] = off
            b = psnames[name].encode('ascii') + b'\0'
            blocks.append(b)
            off += len(b)

    flags = 0
    if facts['maxadv'] == facts['minadv']:
        flags |= 1                              # fixed pitch
    flags |= 2                                  # AFM metrics are always here

    body = b''.join(blocks)
    total = HDRSZ + len(body)
    hdr = bytearray(HDRSZ)
    hdr[0:4] = b'F88\x1a'
    hdr[4] = 1
    hdr[5] = flags
    hdr[6] = rows
    hdr[7] = head['ascent']
    hdr[8] = head['leading']
    hdr[9] = stride
    hdr[10] = FIRST
    hdr[11] = LAST
    hdr[12] = facts['maxadv']
    hdr[13] = facts['minadv']
    hdr[14] = facts['maxover']
    hdr[15] = head['aspect']
    hdr[16:32] = head['face'].encode('ascii')[:16].ljust(16, b'\0')
    for i in range(4):
        hdr[32 + i * 2:34 + i * 2] = struct.pack('<H', bmoff[i])
        hdr[40 + i * 2:42 + i * 2] = struct.pack('<H', mtoff[i])
        hdr[48 + i * 2:50 + i * 2] = struct.pack('<H', psoff[i])
    hdr[56:58] = struct.pack('<H', total & 0xFFFF)
    hdr[58:60] = struct.pack('<H', sum(body) & 0xFFFF)
    return bytes(hdr) + body


# --- the proof sheet ---------------------------------------------------------

SAMPLES = ["Locator  File  Edit  View  Special",
           "The quick brown fox jumps over the lazy dog.",
           "Handgloves & Hamburgefonstiv - 0123456789",
           "MY_FILE.TXT  {a+b}*(c-d)/2 = 100%  @#&|~^",
           "Il1 O0 S5 Z2 B8  \"quoted\"  'x'  (parens)"]


def render(head, styles, path, zoom=2, cga=False):
    rows, stride = head['rows'], head['stride']
    w8 = stride * 8
    lines = []
    for row in range(FIRST, LAST + 1, 32):
        lines.append(('regular', ''.join(chr(c) for c in
                                         range(row, min(row + 32, LAST + 1)))))
    lines.append(('regular', ''))
    for s in SAMPLES:
        lines.append(('regular', s))
    for name in STYLES[1:]:
        if name in styles:
            lines.append((name, SAMPLES[1] + '   [' + name + ']'))

    def width(style, s):
        gl = styles[style]
        return sum(gl.get(ord(ch), gl[32])[0] for ch in s)

    W = max(width(st, s) for st, s in lines) + 16
    H = (rows + head['leading']) * len(lines) + 16
    bits = [bytearray(W) for _ in range(H)]
    for i, (st, s) in enumerate(lines):
        gl = styles[st]
        pen = 8
        top = 8 + i * (rows + head['leading'])
        for ch in s:
            adv, _, art = gl.get(ord(ch), gl[32])
            for r in range(rows):
                for k in range(w8):
                    if art[r] & (1 << (w8 - 1 - k)) and pen + k < W:
                        bits[top + r][pen + k] = 1
            pen += adv
        # no wrap: the samples are sized to fit
    ys = zoom * (3 if cga else 1)
    out = []
    for line in bits:
        px = bytearray()
        for v in line:
            px.extend([0 if v else 255] * zoom)
        out.extend([bytes(px)] * ys)
    _png(path, W * zoom, H * ys, out)


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
    ap.add_argument('source', help='a faces/*.t88 typeface')
    ap.add_argument('-o', '--out', help='write the .F88 here')
    ap.add_argument('--preview', metavar='PNG', help='render a proof sheet')
    ap.add_argument('--cga', action='store_true',
                    help='preview at CGA 640x200 pixel aspect (2.4:1 tall)')
    ap.add_argument('--zoom', type=int, default=2)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    try:
        head, styles, psnames = parse(a.source)
        facts = check(head, styles, a.quiet)
    except (FaceError, IOError) as e:
        sys.stderr.write("os88face: %s\n" % e)
        return 1

    if a.out:
        blob = build(head, styles, psnames, facts)
        with open(a.out, 'wb') as fh:
            fh.write(blob)
        if not a.quiet:
            print("os88face: %s -> %s (%s, %d rows, %d styles, adv %d..%d, "
                  "over %d, %d bytes)"
                  % (a.source, a.out, head['face'], head['rows'], len(styles),
                     facts['minadv'], facts['maxadv'], facts['maxover'],
                     len(blob)))
    if a.preview:
        render(head, styles, a.preview, a.zoom, a.cga)
        if not a.quiet:
            print("os88face: proof sheet -> %s" % a.preview)
    return 0


if __name__ == '__main__':
    sys.exit(main())
