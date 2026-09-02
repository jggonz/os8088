#!/usr/bin/env python3
"""Fit an outline typeface onto os8088's 8-pixel grid, as reviewable `.t88` art.

    python3 tools/os88ttf.py Tinos-Regular.ttf --ppem 11 --rows 12 --ascent 9 \
        --name Times --psname Times-Roman -o faces/times.t88
    python3 tools/os88ttf.py IBMPlexMono-Medium.ttf --fixed 8 --ppem 13 \
        --rows 14 --ascent 11 --name 'Plex Mono' -o faces/plexmono.t88

This is a STARTING POINT and not a pipeline stage. Nothing in the Makefile runs
it: it is run once by hand, its output is committed as `faces/<name>.t88`, and
from then on **the art is the source** - hand-fitted like any other bitmap face
(SPEC.md 6.4). The header it writes records the font, its licence and the exact
command, so the first draft can always be reproduced and compared against; it
does not claim the file is still that draft.

WHY A TOOL AT ALL, when faces/charter.t88 was drawn by hand: an outline face
carries a thousand decisions about proportion that a person re-typing it into
`#` and `.` will get wrong, and FreeType's monochrome hinter - which is what
renders the glyph here - already knows how to put a stem on a pixel boundary at
11 ppem. What it does NOT know is os8088's grid, and that is the whole of this
file:

  * **8 pixels is the whole width there is.** `stride` 2 is in the format and
    refused by the library (SPEC.md 6.4), so a glyph is 8 pixels wide and an
    advance is 4..8 - narrower than any real proportional face at a cap height
    of 8. `M`, `W`, `m`, `%`, `&` and `@` overflow that in every serif tried,
    and `--condense` merges columns out of them the way a bitmap designer does.
  * **Advances are EVEN** (SPEC.md 6.4), because a pen that starts even and
    advances evenly only ever lands on four of the eight bit phases and that
    halves the library's pre-shifted table (SPEC.md 6.5.2).
  * **Stems want to be 2 pixels** (SPEC.md 6.2): on a CGA 640x200 the pixel is
    2.4:1 tall and a 1-pixel stem reads spindly. The answer is NOT to dilate
    the regular weight - that closes the counters at an x-height of 6 - but to
    render a HEAVIER master at the same ppem, which is what `--wght` is for.
    Dilating the regular afterwards was tried and is in the field notes as a
    dead end: at an x-height of 6 the counter of `o`, `e` and `a` is ONE pixel,
    so widening the stem beside it closes the letter and the word turns into a
    row of blocks. The stem report below says what the render actually
    produced, so the weight is chosen by looking rather than by taste.

The art it writes is what tools/os88face.py reads, and everything that tool
refuses is checked here first, so a face that assembles here compiles there.
"""
import argparse
import hashlib
import os
import re
import sys
import textwrap

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:                             # pragma: no cover
    sys.exit("os88ttf: needs Pillow (python3 -m pip install pillow)")

FIRST, LAST = 32, 126
MINADV, MAXADV = 4, 8                           # SPEC.md 6.4, at stride 1
MAXOVER = 2

# What a glyph is called in the art, so the file reads as a font and not as a
# table of code points. Anything not here gets its own character in quotes.
NAMES = {32: 'space', 34: 'quote', 39: 'apos', 92: 'backslash'}


class FitError(Exception):
    pass


# --- the render ---------------------------------------------------------------

def load(path, ppem, axes):
    f = ImageFont.truetype(path, ppem)
    if axes:
        try:
            have = [a['name'].decode() if isinstance(a['name'], bytes)
                    else str(a['name']) for a in f.get_variation_axes()]
        except OSError:
            raise FitError("%s is not a variable font, so --wght/--wdth "
                           "cannot apply - point at a static weight instead"
                           % os.path.basename(path))
        vals = []
        for a in f.get_variation_axes():
            nm = a['name'].decode() if isinstance(a['name'], bytes) else str(a['name'])
            vals.append(axes.get(nm.lower(), a['default']))
        f.set_variation_by_axes(vals)
        sys.stderr.write("os88ttf: axes %s <- %s\n" % (have, vals))
    return f


def render(f, ch):
    """-> (cols, base, adv): ink columns as row-bitmasks, baseline row, advance.

    Monochrome and HINTED, which is the point of going through FreeType at all:
    at 11 ppem the hinter is what puts a stem on a pixel boundary instead of
    across two of them at half coverage.
    """
    asc, dsc = f.getmetrics()
    bb = f.getbbox(ch)
    W = max(1, int(f.getlength(ch)) + 16)
    H = asc + dsc + 8
    im = Image.new('1', (W, H), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'                            # FT_RENDER_MODE_MONO + hinting
    d.text((4, 0), ch, font=f, fill=1)
    px = im.load()
    cols = []
    for x in range(W):
        v = 0
        for y in range(H):
            if px[x, y]:
                v |= 1 << y
        cols.append(v)
    return cols, asc, f.getlength(ch)


def trim(cols):
    """-> (cols with blank edges removed, how many were removed on the left)"""
    lo = 0
    while lo < len(cols) and not cols[lo]:
        lo += 1
    hi = len(cols)
    while hi > lo and not cols[hi - 1]:
        hi -= 1
    return cols[lo:hi], lo


# --- the 8-pixel grid ---------------------------------------------------------

def condense(cols, want):
    """Merge columns until the ink is `want` wide, cheapest merge first.

    A bitmap designer condensing `W` does not scale it - scaling at 8 pixels
    turns a diagonal into a stair with a hole in it. They take a column OUT and
    let the two halves meet, and the column they take is the one whose
    neighbours already agree. That is what the cost is: merging two columns
    costs the ink that lands on top of itself (`&`, which is ink LOST) far more
    than the ink that merely joins (`^`), so a merge inside a diagonal is
    preferred to one that flattens a stem into its counter.
    """
    cols = list(cols)
    while len(cols) > want:
        best, bi = None, 0
        for i in range(len(cols) - 1):
            a, b = cols[i], cols[i + 1]
            cost = 4 * bin(a & b).count('1') + bin(a ^ b).count('1')
            # never merge the two halves of a stem into one another at the
            # very edge: the side bearings are what keep letters apart
            if best is None or cost < best:
                best, bi = cost, i
        cols[bi:bi + 2] = [cols[bi] | cols[bi + 1]]
    return cols


def fit(f, ch, rows, ascent, fixed, gap, condense_ok):
    """-> (adv, [row bitmasks], notes)"""
    cols, base, natural = render(f, ch)
    ink, lsb = trim(cols)
    notes = []
    if not ink:
        return (fixed or MINADV), [0] * rows, notes

    if fixed:
        adv = fixed
    else:
        adv = int(round(natural / 2.0)) * 2
        adv = max(MINADV, min(MAXADV, adv))

    # --- vertical: the baseline is a row of the cell, not a number to round --
    # ink row r of the render is target row r - base + ascent, so the last ink
    # row of a glyph that does not descend lands on ascent-1 (row 8 of 12,
    # which is what faces/charter.t88 calls THE BASELINE ROW).
    shift = ascent - base
    top = min((y for c in ink for y in range(64) if c & (1 << y)), default=0)
    bot = max((y for c in ink for y in range(64) if c & (1 << y)), default=0)
    if top + shift < 0:
        notes.append("%d row(s) above the cell" % -(top + shift))
    if bot + shift > rows - 1:
        notes.append("%d row(s) below the cell" % (bot + shift - (rows - 1)))

    # --- horizontal ---------------------------------------------------------
    room = min(MAXADV, adv + MAXOVER)
    if len(ink) > room:
        if not condense_ok:
            notes.append("ink %d wide, room %d" % (len(ink), room))
        else:
            notes.append("condensed %d->%d" % (len(ink), room))
            ink = condense(ink, room)
    # THE GAP IS THE FACE'S RHYTHM, and it is bought with the ADVANCE and
    # never with the glyph. An advance is EVEN (SPEC.md 6.4), so spacing comes
    # in 2-pixel steps: a digit whose outline wants 6.5 pixels is set at 6, its
    # ink fills the cell, and a row of figures sets solid. Condensing it into a
    # blank column instead was tried and is much worse - a `0` at 5 pixels is a
    # blob and an `a` loses its bowl - so the glyph is left alone and the
    # advance goes up a step. It costs 2 pixels of width on a handful of
    # characters and it is what stops `0123456789` reading as one word.
    if not fixed and len(ink) > adv - gap and adv < MAXADV:
        adv += 2
    # the left side bearing is kept where it fits and given up where it does
    # not: a letter that touches the one before it is worse than one sitting a
    # pixel further left than the outline asked for.
    x0 = max(0, min(lsb - 4, MAXADV - len(ink)))
    if fixed:                                   # a monospaced face centres
        x0 = max(0, (adv - len(ink)) // 2)
    if x0 + len(ink) > MAXADV:
        x0 = MAXADV - len(ink)

    out = [0] * rows
    for i, c in enumerate(ink):
        x = x0 + i
        if not 0 <= x < MAXADV:
            continue
        for y in range(64):
            if c & (1 << y):
                r = y + shift
                if 0 <= r < rows:
                    out[r] |= 1 << (MAXADV - 1 - x)
    return adv, out, notes


# --- what SPEC.md 6.2 wants to see before this is called a face ---------------

def stemreport(glyphs, rows):
    """How many vertical stems came out 1 pixel wide, and in what."""
    thin = []
    for c in sorted(glyphs):
        if not chr(c).isalnum():
            continue
        art = glyphs[c][1]
        for k in range(MAXADV):
            run = 0
            for r in art:
                here = r & (1 << (MAXADV - 1 - k))
                left = r & (1 << (MAXADV - k)) if k else 0
                right = r & (1 << (MAXADV - 2 - k)) if k < MAXADV - 1 else 0
                run = run + 1 if (here and not left and not right) else 0
                if run >= 3:
                    thin.append(chr(c))
                    break
            else:
                continue
            break
    return thin


# --- the art -----------------------------------------------------------------

def emit(fh, path, args, sha, glyphs):
    lic = args.licence or 'see the source font'
    fh.write("# faces/%s - %s, fitted to SPEC.md 6.4's 8-pixel grid\n#\n"
             % (os.path.basename(args.out), args.name))
    for para in (args.note or '').split('\n\n'):
        for line in textwrap.wrap(' '.join(para.split()), 74):
            fh.write("# %s\n" % line)
    if args.note:
        fh.write("#\n")
    fh.write("# SOURCE   %s\n" % os.path.basename(path))
    if args.url:
        fh.write("#          %s\n" % args.url)
    fh.write("# LICENCE  %s\n" % lic)
    fh.write("# SHA-256  %s\n" % sha)
    fh.write("# DRAFTED  %s\n" % drafted(path, args))
    fh.write("#\n"
             "# THE ART IS THE SOURCE from here on. The line above reproduces\n"
             "# the first draft, not this file: a glyph the 8-pixel grid could\n"
             "# not take from the outline is hand-fitted here, which is how a\n"
             "# bitmap face has always been made (SPEC.md 6.2).\n\n")
    fh.write("face %s\n" % args.name)
    fh.write("rows %d\n" % args.rows)
    fh.write("ascent %d\n" % args.ascent)
    fh.write("leading %d\n" % args.leading)
    fh.write("aspect %d\n\n" % args.aspect)
    fh.write("style regular\n")
    if args.psname:
        fh.write("psname %s\n" % args.psname)
    fh.write("\n")
    for c in range(FIRST, LAST + 1):
        adv, art = glyphs[c]
        if c in NAMES:
            label = '"%s"' % NAMES[c]
        elif c == 39:
            label = '"apos"'
        else:
            label = "'%s'" % chr(c)
        fh.write("char %d %s adv %d\n" % (c, label, adv))
        for r in art:
            fh.write(''.join('#' if r & (1 << (MAXADV - 1 - k)) else '.'
                             for k in range(MAXADV)) + "\n")
        fh.write("\n")


def drafted(path, args):
    """The command that redraws the FIRST DRAFT of this file.

    Only the arguments that move a pixel are in it - the licence, the URL and
    the prose are already above, and repeating them here made the line 900
    characters long and hid the four numbers a reader actually wants to change.
    """
    out = ['python3 tools/os88ttf.py', os.path.basename(path),
           '--name', _quote(args.name)]
    for k in ('psname', 'ppem', 'rows', 'ascent', 'leading', 'aspect',
              'fixed', 'gap', 'wght', 'wdth'):
        v = getattr(args, k)
        if v not in (None, 0) or k in ('ppem', 'rows', 'ascent'):
            out.append('--%s %s' % (k, _quote(str(v))))
    return ' '.join(out) + ' -o ' + os.path.join(
        'faces', os.path.basename(args.out))


def _quote(a):
    # the note arrives with newlines in it and this line is a COMMENT: a bare
    # \n here ends the comment and the next word of prose becomes a keyword
    # os88face.py refuses. Flatten first, quote second.
    a = ' '.join(a.split())
    return "'%s'" % a if ' ' in a else a


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('font', help='a .ttf or .otf to fit')
    ap.add_argument('-o', '--out', required=True, help='the .t88 to write')
    ap.add_argument('--name', required=True, help='family name, <= 16 chars')
    ap.add_argument('--psname', help='the base-14 name an exporter asks for')
    ap.add_argument('--ppem', type=int, default=11, help='render size')
    ap.add_argument('--rows', type=int, default=12)
    ap.add_argument('--ascent', type=int, default=9)
    ap.add_argument('--leading', type=int, default=2)
    ap.add_argument('--aspect', type=int, default=0)
    ap.add_argument('--fixed', type=int, default=0,
                    help='one advance for every glyph (a monospaced face)')
    ap.add_argument('--wght', type=float, help='variable weight axis')
    ap.add_argument('--wdth', type=float, help='variable width axis')
    ap.add_argument('--gap', type=int, default=1,
                    help='blank columns kept to the right of the ink (0 for '
                         'a monospaced face, which centres instead)')
    ap.add_argument('--url', help='where the source font came from')
    ap.add_argument('--licence', help='its licence, for the art header')
    ap.add_argument('--note', help='lines of prose for the art header')
    a = ap.parse_args()

    axes = {}
    if a.wght is not None:
        axes['weight'] = a.wght
    if a.wdth is not None:
        axes['width'] = a.wdth

    try:
        f = load(a.font, a.ppem, axes)
    except FitError as e:
        sys.exit("os88ttf: %s" % e)

    glyphs, trouble = {}, []
    for c in range(FIRST, LAST + 1):
        adv, art, notes = fit(f, chr(c), a.rows, a.ascent, a.fixed,
                              0 if a.fixed else a.gap, True)
        if c == 32:
            art = [0] * a.rows                  # the space is blank, always
        glyphs[c] = (adv, art)
        for n in notes:
            trouble.append("  %-3d %r  %s" % (c, chr(c), n))

    advs = [glyphs[c][0] for c in glyphs]
    over = 0
    for c in glyphs:
        adv, art = glyphs[c]
        for r in art:
            for k in range(MAXADV):
                if r & (1 << (MAXADV - 1 - k)):
                    over = max(over, k + 1 - adv)

    sha = hashlib.sha256(open(a.font, 'rb').read()).hexdigest()
    with open(a.out, 'w') as fh:
        emit(fh, a.font, a, sha, glyphs)

    thin = stemreport(glyphs, a.rows)
    blank = sum(1 for c in glyphs for r in glyphs[c][1] if not r)
    sys.stderr.write(
        "os88ttf: %s -> %s (%d rows, adv %d..%d, over %d, %d%% blank)\n"
        % (os.path.basename(a.font), a.out, a.rows, min(advs), max(advs), over,
           100 * blank // (95 * a.rows)))
    if thin:
        sys.stderr.write("os88ttf: 1-PIXEL STEMS in %s%s - SPEC.md 6.2 wants "
                         "2 on a 2.4:1 CGA pixel; try a heavier --wght\n"
                         % (''.join(thin[:24]),
                            '...' if len(thin) > 24 else ''))
    if trouble:
        sys.stderr.write("os88ttf: %d glyph(s) the grid had to argue with:\n%s\n"
                         % (len(trouble), '\n'.join(trouble[:40])))


if __name__ == '__main__':
    main()
