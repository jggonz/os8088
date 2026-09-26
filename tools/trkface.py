#!/usr/bin/env python3
"""Check (or regenerate) Tracker's body tiles against its layouts (SPEC.md 45.21).

    python3 tools/trkface.py            # check: exit 1 naming the first fault
    python3 tools/trkface.py --emit     # print the tables the sources should hold

WHY THIS EXISTS. The windowed face never fills a ground that something is then
drawn over (SPEC.md 45.21 rule 1): every element draws its own rect opaque,
and the BODY between them is a table of tiles that covers exactly the pixels
no element owns. A tile that overlaps an element is a double write on every
full paint; a gap is a strip of whatever was on the screen before, which on a
freshly opened window is the desktop's dither. Neither shows up anywhere but
the glass, and the tables are 70-odd rectangles typed as numbers - so the
cover is CHECKED, from the same numbers the assembler reads.

The element rects are derived from the layout records in apps/tracker/
trkwin.inc (and the editor's constants in trklist.inc) exactly as tw_rects,
tw_lcd, tw_sliders and tw_status place them, so a layout edit that forgets the
tiles fails here, and --emit prints the tiles that layout now needs.
"""
import re
import sys
from os.path import abspath, dirname, join

ROOT = dirname(dirname(abspath(__file__)))
WIN = join(ROOT, "apps", "tracker", "trkwin.inc")
LST = join(ROOT, "apps", "tracker", "trklist.inc")


def consts(text):
    out = {}
    for m in re.finditer(r"^(\w+)\s+equ\s+(\d+)\b", text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def words_after(text, label, n):
    """The first n numeric `dw` operands after `label:` (symbols skipped)."""
    i = text.index("\n" + label + ":")
    vals = []
    for line in text[i:].split("\n")[1:]:
        m = re.match(r"\s+dw\s+(.*?)(;|$)", line)
        if not m:
            if vals:
                break
            continue
        for tok in m.group(1).split(","):
            tok = tok.strip()
            vals.append(int(tok) if tok.isdigit() else tok)
        if len(vals) >= n:
            break
    return vals[:n]


def tiles_of(text, label):
    i = text.index("\n" + label + ":")
    out, pend = [], None
    for line in text[i:].split("\n")[2:]:
        m = re.match(r"\s+db\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*$", line)
        if m:                           # the face's 4-byte form: x1/4, (x2+1)/4
            a, b, y1, y2 = (int(g) for g in m.groups())
            out.append((a * 4, y1, b * 4 - 1, y2))
            continue
        m = re.match(r"\s+dw\s+(\d+),\s*(\d+)\s*$", line)
        if m:
            pend = (int(m.group(1)), int(m.group(2)))
            continue
        m = re.match(r"\s+db\s+(\d+),\s*(\d+)\s*$", line)
        if m and pend:
            out.append((pend[0], int(m.group(1)), pend[1], int(m.group(2))))
            pend = None
            continue
        break
    return out


def face_elements(text, label, c):
    L = words_after(text, label, 28)
    lx1, ly1, lx2, ly2, _ln, ty1, ty2 = L[0:7]
    vol, scr, viz = tuple(L[7:11]), tuple(L[11:15]), tuple(L[15:19])
    oy1, oh, opy, on, sty = L[19:24]
    els = [(lx1, ly1, lx2, ly2), vol, scr, viz,
           (c["TW_TX"], sty, c["TW_TX"] + c["TW_CELLS"] * 8 - 1, sty + 7)]
    for i in range(c["TW_NTB"]):
        x = c["TW_BX0"] + c["TW_BPITCH"] * i
        els.append((x, ty1, x + c["TW_BW"] - 1, ty2))
    for j in range(on):
        x = c["TW_OX1"] + (c["TW_OPX"] if j & 1 else 0)
        y = oy1 + (j >> 1) * opy
        els.append((x, y, x + c["TW_OW"] - 1, y + oh - 1))
    return els


def list_elements(c):
    els = [(c["TPL_RX"] - 1, c["TPL_RY"] - 1,
            c["TPL_RX"] + c["TPL_RCELLS"] * 8, c["TPL_RY"] + c["TPL_MAX"] * 8)]
    for i in range(c["TPL_NB"]):
        y = c["TPL_RY"] + i * c["TPL_BPY"]
        els.append((c["TPL_BX"], y, c["TPL_BX"] + c["TPL_BW"] - 1,
                    y + c["TPL_BH"] - 1))
    return els


def make_tiles(W, H, els):
    """The body as rectangles: per row the gaps between elements, each gap
    carried down while the SAME x-interval stays a gap."""
    rows = []
    for y in range(H):
        occ = sorted((a, b) for (a, y1, b, y2) in els if y1 <= y <= y2)
        gaps, x = [], 0
        for a, b in occ:
            if a > x:
                gaps.append((x, a - 1))
            x = max(x, b + 1)
        if x < W:
            gaps.append((x, W - 1))
        rows.append(set(gaps))
    out, open_ = [], {}
    for y in range(H + 1):
        cur = rows[y] if y < H else set()
        for k in sorted(open_):
            if k not in cur:
                out.append((k[0], open_.pop(k), k[1], y - 1))
        for k in sorted(cur):
            open_.setdefault(k, y)
    return out


def cover_fault(W, H, els, tiles):
    cov = [[0] * W for _ in range(H)]
    for (a, y1, b, y2) in list(els) + list(tiles):
        if a < 0 or y1 < 0 or b >= W or y2 >= H or b < a or y2 < y1:
            return "rect %r is outside the %dx%d content" % ((a, y1, b, y2), W, H)
        for y in range(y1, y2 + 1):
            for x in range(a, b + 1):
                cov[y][x] += 1
    for y in range(H):
        for x in range(W):
            if cov[y][x] != 1:
                return ("pixel (%d,%d) is covered %d times - the body tiles and "
                        "the elements must cover the content exactly once"
                        % (x, y, cov[y][x]))
    return None


def main(argv):
    win, lst = open(WIN).read(), open(LST).read()
    c = consts(win)
    c.update(consts(lst))
    faces = [("tw_tiles_full", "tw_lay_full", c["TW_W"], c["TW_HFULL"]),
             ("tw_tiles_comp", "tw_lay_comp", c["TW_W"], c["TW_HCOMP"])]
    sets = [(t, win, face_elements(win, lay, c), W, H) for t, lay, W, H in faces]
    sets.append(("tpl_tiles", lst, list_elements(c), c["TPL_W"], c["TPL_H"]))
    if "--emit" in argv:
        for name, _src, els, W, H in sets:
            t = make_tiles(W, H, els)
            print("%s:%s; %d tiles" % (name, " " * max(1, 26 - len(name)), len(t)))
            for (a, y1, b, y2) in t:
                if name.startswith("tw_"):
                    if a % 4 or (b + 1) % 4:
                        print("trkface: %s: tile %r is off the 4-pixel grid the "
                              "face's 4-byte tiles need" % (name, (a, y1, b, y2)),
                              file=sys.stderr)
                        return 1
                    print("    db %d, %d, %d, %d" % (a // 4, (b + 1) // 4, y1, y2))
                else:
                    print("    dw %d, %d\n    db %d, %d" % (a, b, y1, y2))
        return 0
    bad = 0
    for name, src, els, W, H in sets:
        tiles = tiles_of(src, name)
        declared = c.get({"tw_tiles_full": "TW_NT_FULL",
                          "tw_tiles_comp": "TW_NT_COMP",
                          "tpl_tiles": "TPL_NT"}[name])
        fault = cover_fault(W, H, els, tiles)
        if fault is None and declared != len(tiles):
            fault = "the table holds %d tiles and its count equ says %s" % (
                len(tiles), declared)
        if fault:
            print("trkface: %s: %s (--emit prints what the layout needs)"
                  % (name, fault))
            bad = 1
        else:
            print("trkface: %s: %d tiles + %d elements cover %dx%d exactly once"
                  % (name, len(tiles), len(els), W, H))
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
