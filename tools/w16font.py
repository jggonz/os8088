#!/usr/bin/env python3
"""w16font.py - WIN16's System font faces (SPEC.md 124.7, WIN16-PLAN.md S4).

Windows 3.0 showed its System font at the height the display driver asked for:
16 px on VGA, 12 on EGA and Hercules, 8 on CGA, every one of them proportional
with the SAME advance widths (D4). WIN16 draws text in that font, so a program
lays out as it did on Windows.

THE ART IS THE SOURCE. apps/win16/fonts/sys16.txt, sys12.txt and sys08.txt are
committed text art, one glyph a block, and nothing in the build reads anything
else. They were DRAFTED once, by this tool:

    python3 tools/w16font.py draft SYSTEM.SFD apps/win16/fonts/

  * sys16.txt is Wine's System font, the 16 px strike of fonts/system.sfd at
    the commit WIN16-PLAN.md pins (LGPL-2.1-or-later; Huw D M Davies and
    Dmitry Timoshkov), glyphs 32-126, copied as they are.
  * sys12.txt and sys08.txt are that same strike with its ROWS resampled to 12
    and to 8 (the advances, and so every string's width, are unchanged), and
    then fitted by hand where a letter closed up. Windows' own EGA and CGA
    faces are the same design squeezed for a taller pixel, which is what this
    does; no Microsoft glyph is in either file.

`build` turns one art file into the face part WIN16.O88 carries (the layout is
below and in SPEC.md 124.7.1). `fon` prints a Windows .FON's metrics and widths
- the user's own files, read for measurement and never copied into the tree.

    python3 tools/w16font.py build apps/win16/fonts/sys16.txt build/win16/sys16.bin [DIALOG.txt]
    python3 tools/w16font.py dlgdraft MS_SANS_SERIF.SFD apps/win16/fonts/
    python3 tools/w16font.py fon EGASYS.FON [--show]     (SZDD-packed or not)
    python3 tools/w16font.py check                       (the fast row's half)

THE PART (SPEC.md 124.7.1), every word little-endian:

    +0  db rows, ascent, avgwidth, maxwidth
    +4  db intlead, extlead, first (32), count (95)
    +8  db adv[count]               the advance of each glyph, 1..16
        (pad to even)
        dw off[count]               each glyph's bitmap, from the part's start
        bitmaps: rows x ((adv + 7) / 8) bytes, row-major, bit 7 leftmost,
        1 = ink

A glyph is never wider than its advance (Wine's strike has no overhang), so a
glyph IS its advance-wide cell, and the composer needs no second width.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

FIRST, LAST = 32, 126
COUNT = LAST - FIRST + 1

# The three faces, as Windows 3.0's own .FON files measure (D4, SPEC.md 124.7):
# rows, ascent, internal leading, external leading. Average 7, maximum 14 on all
# three, which the art's advances give by construction and `check` asserts.
FACES = {16: (16, 13, 3, 0), 12: (12, 10, 2, 0), 8: (8, 7, 1, 0)}

# THE DIALOG FACE (SPEC.md 124.22.17): what a 3.x dialog template's
# `DS_SETFONT` "MS Sans Serif" 8 (or 3.0's "Helv" 8) is drawn in. Windows 3.1
# drew a template's font BOLD, and its dialog units come off the bold face:
# ((the alphabet's width / 26) + 1) / 2 = 7 on VGA, the System font's 8 by
# the same formula. Wine's MS Sans Serif 13 px strike has Microsoft's own
# advances (SSERIFE.FON measures the same 323 over the alphabet), emboldened
# here as Windows synthesised it - each row OR-ed with itself a pixel to the
# right, the advance one wider. VGA is that strike, 13 rows, measured as
# SSERIFE.FON's (ascent 11, internal leading 2); EGA and Hercules 10 and CGA 8
# are drafted from it as the System font's shorter faces were, and are
# derived, not measured (no Windows 3.1 EGA or CGA face is installed here).
# rows, ascent, internal leading, external leading, and tmAveCharWidth: the
# bold face's, SSERIFE's 5 + 1
DFACES = {13: (13, 11, 2, 0), 10: (10, 8, 1, 0), 8: (8, 6, 0, 0)}
DAVG = 6
ZONES13 = ((0, 2), (2, 5), (5, 11), (11, 13))
DXHEIGHT = {13: 6, 10: 5, 8: 5}
DPART = {16: 13, 12: 10, 8: 8}          # the System face -> its dialog face


# --------------------------------------------------------------------------
# Wine's SFD bitmap strike
# --------------------------------------------------------------------------

def _b85(s):
    """FontForge's SFD bitmap encoding: ASCII85 with `z` for four zeros."""
    out = bytearray()
    i = 0
    s = "".join(s.split())
    while i < len(s):
        if s[i] == "z":
            out += b"\0\0\0\0"
            i += 1
            continue
        grp = s[i:i + 5]
        i += 5
        n = len(grp)
        grp = grp + "u" * (5 - n)
        v = 0
        for c in grp:
            v = v * 85 + (ord(c) - 33)
        out += struct.pack(">I", v & 0xFFFFFFFF)[:n - 1 if n < 5 else 4]
    return bytes(out)


def sfd_strike(path, size):
    """{code: (advance, rows)} of the strike `size`, rows top-first as strings."""
    lines = open(path, encoding="latin-1").read().split("\n")
    i = 0
    while i < len(lines) and not lines[i].startswith("BitmapFont: %d " % size):
        i += 1
    if i == len(lines):
        raise SystemExit("w16font: no %d px strike in %s" % (size, path))
    _, px, _n, asc, desc = lines[i].split()[:5]
    asc, desc = int(asc), int(desc)
    glyphs = {}
    i += 1
    while i < len(lines) and not lines[i].startswith("EndBitmapFont"):
        if lines[i].startswith("BDFChar:"):
            f = lines[i].split()
            enc, adv = int(f[2]), int(f[3])
            x0, x1, y0, y1 = (int(v) for v in f[4:8])
            data = []
            i += 1
            while i < len(lines) and not lines[i].startswith(("BDFChar:", "EndBitmapFont")):
                data.append(lines[i])
                i += 1
            if not FIRST <= enc <= LAST:
                continue
            raw = _b85("".join(data))
            w, h = x1 - x0 + 1, y1 - y0 + 1
            bpr = (w + 7) // 8
            cell = [["."] * adv for _ in range(asc + desc)]
            if raw.strip(b"\0") or (w > 0 and h > 0 and len(raw) >= bpr * h and x1 >= x0):
                for r in range(h):
                    for c in range(w):
                        if r * bpr + c // 8 < len(raw) and raw[r * bpr + c // 8] & (0x80 >> (c % 8)):
                            y = asc - 1 - (y1 - r)
                            x = x0 + c
                            if 0 <= y < asc + desc and 0 <= x < adv:
                                cell[y][x] = "#"
                            else:
                                raise SystemExit("w16font: glyph %d ink outside its cell" % enc)
            glyphs[enc] = (adv, ["".join(r) for r in cell])
            continue
        i += 1
    return asc + desc, asc, glyphs


# --------------------------------------------------------------------------
# Windows' .FON (measurement only)
# --------------------------------------------------------------------------

def szdd(d):
    """Expand an SZDD file (COMPRESS.EXE's LZSS); a plain file passes through."""
    if d[:8] != b"SZDD\x88\xf0\x27\x33":
        return d
    n = struct.unpack_from("<I", d, 10)[0]
    win = bytearray(b" " * 4096)
    pos = 4096 - 16
    out = bytearray()
    i = 14
    while i < len(d) and len(out) < n:
        ctl = d[i]
        i += 1
        for b in range(8):
            if i >= len(d) or len(out) >= n:
                break
            if ctl & (1 << b):
                c = d[i]
                i += 1
                out.append(c)
                win[pos] = c
                pos = (pos + 1) & 4095
            else:
                if i + 1 >= len(d):
                    break
                lo, hi = d[i], d[i + 1]
                i += 2
                src = lo | ((hi & 0xF0) << 4)
                ln = (hi & 0x0F) + 3
                for k in range(ln):
                    c = win[(src + k) & 4095]
                    out.append(c)
                    win[pos] = c
                    pos = (pos + 1) & 4095
    return bytes(out)


def fon_fonts(path):
    """Every FONT resource of a .FON as (metrics, {code: (adv, rows)})."""
    import os88ne
    d = szdd(open(path, "rb").read())
    ne = os88ne.NE(d, os.path.basename(path))
    out = []
    for r in ne.resources:
        if r["type"] != "FONT":
            continue
        out.append(fnt(d, r["at"]))
    return out


def fnt(d, p):
    import os88ne
    m = os88ne._fnt(d, p)
    ver = struct.unpack_from("<H", d, p)[0]
    h = m["pixheight"]
    tab = p + (118 if ver < 0x300 else 148)
    esz = 4 if ver < 0x300 else 6
    glyphs = {}
    for k, code in enumerate(range(m["first"], m["last"] + 1)):
        if ver < 0x300:
            w, off = struct.unpack_from("<HH", d, tab + esz * k)
        else:
            w, off = struct.unpack_from("<HI", d, tab + esz * k)
        rows = []
        for y in range(h):
            s = ""
            for x in range(w):
                b = d[p + off + (x // 8) * h + y]
                s += "#" if b & (0x80 >> (x % 8)) else "."
            rows.append(s)
        glyphs[code] = (w, rows)
    return m, glyphs


# --------------------------------------------------------------------------
# The art
# --------------------------------------------------------------------------

def write_art(path, rows, asc, glyphs, header):
    with open(path, "w") as f:
        for h in header:
            f.write("# %s\n" % h if h else "#\n")
        f.write("face %d %d\n" % (rows, asc))
        for code in range(FIRST, LAST + 1):
            adv, cell = glyphs[code]
            f.write("\nchar %d %s %d\n" % (code, repr(chr(code)), adv))
            for r in cell:
                f.write(r + "\n")


def read_art(path):
    rows = asc = None
    glyphs = {}
    cur = None
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if rows is None and line.startswith("#"):
            continue                    # comments come before `face` only:
        if not line and cur is None:    #   a row of art can start with `#`
            continue
        if line.startswith("face "):
            rows, asc = (int(v) for v in line.split()[1:3])
            continue
        if line.startswith("char "):
            f = line.split()
            code, adv = int(f[1]), int(f[-1])
            cur = (code, adv, [])
            glyphs[code] = cur
            continue
        if not line:
            continue
        if cur is None:
            raise SystemExit("%s:%d: art before any `char`" % (path, n))
        if len(line) != cur[1] or set(line) - set(".#"):
            raise SystemExit("%s:%d: char %d's row is not %d of `.#`"
                             % (path, n, cur[0], cur[1]))
        cur[2].append(line)
    if rows is None:
        raise SystemExit("%s: no `face` line" % path)
    for code in range(FIRST, LAST + 1):
        if code not in glyphs:
            raise SystemExit("%s: char %d is missing" % (path, code))
        if len(glyphs[code][2]) != rows:
            raise SystemExit("%s: char %d has %d rows, not %d"
                             % (path, code, len(glyphs[code][2]), rows))
        if not 1 <= glyphs[code][1] <= 16:
            raise SystemExit("%s: char %d's advance %d is outside 1..16"
                             % (path, code, glyphs[code][1]))
    return rows, asc, {c: (a, r) for c, (_, a, r) in glyphs.items()}


# --------------------------------------------------------------------------
# Drafting the shorter faces
# --------------------------------------------------------------------------

# The 16 px design's four zones, by row: the internal leading (0-2), the
# capitals above the x-height (3-5), the x-height (6-12, the baseline under
# 12) and the descenders (13-15). Each face keeps rows zone by zone, so its
# x-height is where Windows' own face of that height has it: 6 rows of EGA's
# 8 capital rows and 5 of CGA's 6, as EGASYS and CGASYS measure.
ZONES16 = ((0, 3), (3, 6), (6, 13), (13, 16))
XHEIGHT = {16: 7, 12: 6, 8: 5}


def zones(n, faces=None, xh=None):
    """The same four zones on an n-row face, from its D4 metrics."""
    rows, asc, il, _ = (faces or FACES)[n][:4]
    x = (xh or XHEIGHT)[n]
    return ((0, il), (il, asc - x), (asc - x, asc), (asc, rows))


def _loss(cells, keep, z0, z1):
    """Ink lost by keeping only rows `keep` of zone [z0, z1): each dropped
    row's pixels that neither kept neighbour has."""
    lost = 0
    ks = sorted(keep)
    for cell in cells:
        for r in range(z0, z1):
            if r in keep:
                continue
            up = max([k for k in ks if k < r], default=None)
            dn = min([k for k in ks if k > r], default=None)
            for x, ch in enumerate(cell[r]):
                if ch == "#" and not ((up is not None and cell[up][x] == "#") or
                                      (dn is not None and cell[dn][x] == "#")):
                    lost += 1
    return lost


def rowplan(g16, n, src=ZONES16, faces=None, xh=None):
    """Which source rows each zone of an n-row face keeps: ONE choice for every
    glyph, so a stroke lands on the same row in all of them (the x-height, the
    bar of an e, the baseline), and the choice is the one losing least ink
    over the whole face."""
    from itertools import combinations
    cells = [cell for _, cell in g16.values()]
    plan = []
    for (s0, s1), (d0, d1) in zip(src, zones(n, faces, xh)):
        want = d1 - d0
        best = None
        for keep in combinations(range(s0, s1), want):
            loss = _loss(cells, set(keep), s0, s1)
            if best is None or loss < best[0]:
                best = (loss, keep)
        if want == 1 and s1 - s0 > 1:
            best = (0, (s0,))           # one row for the capitals above the
                                        # x-height: their TOP, where a round
                                        # letter turns, and not the row the
                                        # ink count prefers, which is a stem
        plan.append(best[1] if want else ())
    return plan


def resample(cell, plan, srczones):
    """One glyph through the plan: each kept row, OR-ed with the dropped rows
    it stands for when they carry ink it does not (a stroke is merged into
    its neighbour rather than lost). A zone squeezed to ONE row keeps this
    glyph's own outermost ink there instead - the top of a capital or a
    bracket, the foot of a descender or an underscore - which no single row
    chosen for the whole face can."""
    out = []
    last = len(srczones) - 1
    for zi, (keep, (s0, s1)) in enumerate(zip(plan, srczones)):
        keep = list(keep)
        if len(keep) == 1 and s1 - s0 > 1:
            ink = [r for r in range(s0, s1) if "#" in cell[r]]
            out.append(cell[(ink[-1] if zi == last else ink[0]) if ink else keep[0]])
            continue
        for i, k in enumerate(keep):
            lo = s0 if i == 0 else (keep[i - 1] + k) // 2 + 1
            hi = s1 - 1 if i == len(keep) - 1 else (k + keep[i + 1]) // 2
            row = list(cell[k])
            for r in range(lo, hi + 1):
                if r == k:
                    continue
                if not any(cell[r][x] == "#" and row[x] != "#" for x in range(len(row))):
                    continue
                # merge only a row whose ink would otherwise vanish entirely
                near = [j for j in keep if abs(j - r) <= 1]
                if any(all(cell[j][x] == "#" for x in range(len(row)) if cell[r][x] == "#")
                       for j in near):
                    continue
                row = [("#" if cell[r][x] == "#" or row[x] == "#" else ".")
                       for x in range(len(row))]
            out.append("".join(row))
    return out


def draft(sfd, outdir):
    rows, asc, g16 = sfd_strike(sfd, 16)
    assert (rows, asc) == FACES[16][:2], (rows, asc)
    src = ["sys16.txt - WIN16's System font for VGA (SPEC.md 124.7)", "",
           "Wine's System font, fonts/system.sfd's 16 px strike, at",
           "wine-mirror/wine 0738d528db522f52d9d75f2aec712688dd31d1d2.",
           "Copyright (C) 2004 Huw D M Davies, Dmitry Timoshkov;",
           "LGPL-2.1-or-later. Drafted by `tools/w16font.py draft`, and THE ART",
           "IS THE SOURCE from then on: `build` reads this file and nothing else.",
           "", "`face ROWS ASCENT`, then `char CODE 'c' ADVANCE` and ROWS rows of",
           "exactly ADVANCE pixels, `#` ink and `.` paper."]
    write_art(os.path.join(outdir, "sys16.txt"), rows, asc, g16, src)
    for n in (12, 8):
        nrows, nasc = FACES[n][:2]
        plan = rowplan(g16, n)
        g = {c: (a, resample(cell, plan, ZONES16)) for c, (a, cell) in g16.items()}
        hdr = ["sys%02d.txt - WIN16's System font for %s (SPEC.md 124.7)"
               % (n, "EGA and Hercules" if n == 12 else "CGA"), "",
               "Drafted by `tools/w16font.py draft` from sys16.txt (Wine's System",
               "font, LGPL-2.1-or-later): the same advances, the rows resampled",
               "to %d with %d above the baseline - Windows 3.0's own %s face's"
               % (nrows, nasc, "EGASYS" if n == 12 else "CGASYS"),
               "height and ascent (D4). Source rows kept, by zone: %s." % (plan,),
               "Then fitted by hand. THE ART IS THE SOURCE from then on. No",
               "Microsoft glyph is in this file."]
        # (each file's header then names the glyphs that were fitted)
        write_art(os.path.join(outdir, "sys%02d.txt" % n), nrows, nasc, g, hdr)


def embolden(cell):
    """Windows' synthesised bold: each row OR-ed with itself a pixel right,
    in a cell one pixel wider."""
    out = []
    for r in cell:
        w = r + "."
        out.append("".join("#" if w[x] == "#" or (x and w[x - 1] == "#") else "."
                           for x in range(len(w))))
    return out


def dlgdraft(sfd, outdir):
    rows, asc, g13 = sfd_strike(sfd, 13)
    assert (rows, asc) == DFACES[13][:2], (rows, asc)
    for n in (13, 10, 8):
        nrows, nasc = DFACES[n][:2]
        if n == 13:
            plan = None
            g = g13
        else:
            plan = rowplan(g13, n, ZONES13, DFACES, DXHEIGHT)
            g = {c: (a, resample(cell, plan, ZONES13)) for c, (a, cell) in g13.items()}
        g = {c: (a + 1, embolden(cell)) for c, (a, cell) in g.items()}
        hdr = ["dlg%02d.txt - WIN16's dialog face for %s (SPEC.md 124.22.17)"
               % (n, {13: "VGA", 10: "EGA and Hercules", 8: "CGA"}[n]), "",
               "Wine's MS Sans Serif, fonts/ms_sans_serif.sfd's 13 px strike, at",
               "wine-mirror/wine 0738d528db522f52d9d75f2aec712688dd31d1d2.",
               "Copyright (C) 2004 Huw D M Davies, Dmitry Timoshkov;",
               "LGPL-2.1-or-later. " + ("Its rows as they are." if n == 13 else
               "Its rows resampled to %d with %d above the baseline (derived; "
               "source rows kept, by zone: %s)." % (nrows, nasc, plan)),
               "Then emboldened as Windows 3.1 drew a dialog template's font: each",
               "row OR-ed with itself a pixel right, the advance one wider. Drafted",
               "by `tools/w16font.py dlgdraft`; THE ART IS THE SOURCE from then on.",
               "No Microsoft glyph is in this file."]
        write_art(os.path.join(outdir, "dlg%02d.txt" % n), nrows, nasc, g, hdr)


# --------------------------------------------------------------------------
# The part
# --------------------------------------------------------------------------

def part(path, dialog=None):
    """One face's bytes; with `dialog`, the dialog face's art appended at the
    next paragraph (SPEC.md 124.22.17), where WIN16 finds it from the System
    face's own last glyph."""
    rows, asc, g = read_art(path)
    face = FACES.get(rows)
    avg = 7                                                 # Windows' figure, D4
    if face is None or face[1] != asc:
        face = DFACES.get(rows)
        avg = DAVG
        if face is None or face[1] != asc:
            raise SystemExit("%s: a %d-row face with ascent %d is not one of WIN16's"
                             % (path, rows, asc))
    advs = [g[c][0] for c in range(FIRST, LAST + 1)]
    hdr = bytes([rows, asc, avg, max(advs), face[2], face[3], FIRST, COUNT])
    body = bytearray(hdr + bytes(advs))
    if len(body) & 1:
        body.append(0)
    offat = len(body)
    body += b"\0" * (2 * COUNT)
    for k, c in enumerate(range(FIRST, LAST + 1)):
        adv, cell = g[c]
        struct.pack_into("<H", body, offat + 2 * k, len(body))
        bpr = (adv + 7) // 8
        for r in cell:
            v = 0
            for x, ch in enumerate(r):
                if ch == "#":
                    v |= 1 << (bpr * 8 - 1 - x)
            body += v.to_bytes(bpr, "big")
    if dialog:
        body += b"\0" * (-len(body) % 16)
        body += part(dialog)
    return bytes(body)


def check(fontdir):
    """The art's own invariants: what `build` assumes and Windows measured."""
    bad = 0
    faces = {}
    for n in (16, 12, 8):
        p = os.path.join(fontdir, "sys%02d.txt" % n)
        rows, asc, g = read_art(p)
        faces[n] = g
        if (rows, asc) != FACES[n][:2]:
            print("w16font: %s is %d rows ascent %d, not %r" % (p, rows, asc, FACES[n][:2]))
            bad += 1
        if max(a for a, _ in g.values()) != 14:
            print("w16font: %s's widest advance is not Windows' 14" % p)
            bad += 1
    for c in range(FIRST, LAST + 1):
        a = {n: faces[n][c][0] for n in faces}
        if len(set(a.values())) != 1:
            print("w16font: char %d's advance differs between faces: %r" % (c, a))
            bad += 1
    dfaces = {}
    for n in (13, 10, 8):
        p = os.path.join(fontdir, "dlg%02d.txt" % n)
        rows, asc, g = read_art(p)
        dfaces[n] = g
        if (rows, asc) != DFACES[n][:2]:
            print("w16font: %s is %d rows ascent %d, not %r" % (p, rows, asc, DFACES[n][:2]))
            bad += 1
        al = sum(g[ord(ch)][0] for ch in
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
        if (al // 26 + 1) // 2 != 7:
            print("w16font: %s's dialog unit is not Windows 3.1's 7" % p)
            bad += 1
    for c in range(FIRST, LAST + 1):
        a = {n: dfaces[n][c][0] for n in dfaces}
        if len(set(a.values())) != 1:
            print("w16font: char %d's dialog advance differs between faces: %r" % (c, a))
            bad += 1
    return bad


def main(a):
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if a[0] == "draft" and len(a) == 3:
        draft(a[1], a[2])
        return 0
    if a[0] == "dlgdraft" and len(a) == 3:
        dlgdraft(a[1], a[2])
        return 0
    if a[0] == "build" and len(a) in (3, 4):
        open(a[2], "wb").write(part(a[1], a[3] if len(a) == 4 else None))
        return 0
    if a[0] == "check":
        d = a[1] if len(a) > 1 else os.path.join(HERE, "..", "apps", "win16", "fonts")
        return 1 if check(d) else 0
    if a[0] == "fon" and len(a) >= 2:
        for m, g in fon_fonts(a[1]):
            advs = [g[c][0] for c in range(FIRST, LAST + 1) if c in g]
            print("%s %d px ascent %d intlead %d extlead %d avg %d max %d "
                  "weight %d charset %d pitch %02xh first %d last %d res %dx%d"
                  % (m["face"], m["pixheight"], m["ascent"], m["intlead"],
                     m["extlead"], m["avgwidth"], m["maxwidth"], m["weight"],
                     m["charset"], m["pitchfamily"], m["first"], m["last"],
                     m["hres"], m["vres"]))
            print("advances 32-126:", " ".join(str(v) for v in advs))
            if "--show" in a:
                for c in range(FIRST, LAST + 1):
                    print("char %d %r %d" % (c, chr(c), g[c][0]))
                    for r in g[c][1]:
                        print(r)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
