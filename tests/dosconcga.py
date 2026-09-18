#!/usr/bin/env python3
"""THE CONSOLE BAND ON THE SHORT ADAPTER (SPEC.md 96.33.8).

CGA's 200 lines leave the DOS box **17 of the console's 25 rows**, and
`[con_vtop]` is which buffer row the band starts at.  It was `CON_ROWS -
[con_vrows]` - the bottom of the BUFFER - which is the right number only once
the console has scrolled a whole screenful.  At the first paint the cursor is
on row 4 and the view started at row 8, so every live row was above the fold
and **the band was an empty black rectangle**.  Reported from the field in
those words.

`doscon` cannot see this: it runs on `os8088_5150_herc_gla`, where 348 pixels
hold all 25 rows, `vrows` is 25 and `vtop` is 0 under either rule.  That is
§39's standing trap - three adapters, one binary - and this row is the second
adapter.

THREE ASSERTIONS, and the third is the one that keeps the fix honest:

  1  AT THE FIRST PAINT the cursor is inside the view and the banner and
     prompt are on rows the band is showing.  This is the reported bug.
  2  THE BAND HAS LIT PIXELS IN IT.  A buffer that is correct and a band that
     is pointed at the wrong rows draw the SAME black rectangle, so the
     assertion has to reach the glass at least once - it is `doscon`'s own
     `con_font` lesson one defect along.
  3  AND IT STILL TRACKS ONCE IT SCROLLS.  A fix that only works before the
     console fills is not a fix: enough output is pushed to drive the cursor
     past `vrows`, and the cursor must stay in view at every step while
     `vtop` actually moves.  Without the `[con_scrl]` half this still passes
     on the buffer and tears on the glass, which is why 2 is re-checked at
     the end.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
BOX = "A:/APPS/DOS.O88"
MACHINE = "os8088_5150_cga_gla"          # THE POINT: 200 lines, 17 rows of 25


def fail(msg):
    print("dosconcga: FAIL: %s" % msg)
    sys.exit(1)


class Box(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    @property
    def base(self):
        # **RESOLVED PER ACCESS, NOT CACHED** (SPEC.md 66.6.1.2): the DOS
        # box's region MOVES now - it is a re-homed carve and was pinned only
        # until that section - so a base banked in __init__ names the bytes
        # the package used to occupy, and decodes as plausible rubbish.
        return dosmap.instance(self.m) << 4

    def w(self, name):
        return int.from_bytes(self.m.read(self.base + self.dm[name], 2),
                              "little")

    def rows(self):
        scr = self.m.read(self.base + self.dm["con_scr"], 80 * 25 * 2)
        out = []
        for r in range(25):
            row = scr[r * 160:(r + 1) * 160]
            out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                               for i in range(0, 160, 2)).rstrip())
        return out

    def visible(self):
        """The rows the band is actually showing, and where the cursor is."""
        vt, vr = self.w("con_vtop"), self.w("con_vrows")
        return vt, vr, self.w("con_cy"), self.rows()[vt:vt + vr]


def band_lit(m, bx, rect=None):
    """Lit pixels inside the console band, found by its own shape.

    **IMPORTED BY tests/dosdirsw.py** as well as used here, which is why it
    takes `bx` rather than reading module state: two rows now need to ask
    whether the band actually has ink in it, and a third copy of a
    modal-extent scan is a third place to get it subtly wrong.

    doscon.py's band_ink, and it is copied rather than imported for that
    file's own reason: the rendered frame is not in guest coordinates, so a
    rectangle read at [dos_conx] lands on the window's white margin and 40
    pixels of margin look exactly like 40 pixels of text.  The band is found
    by the MODAL dark extent instead - the window's bottom border is one
    full-width dark row and "widest" picks it.
    """
    w, h, px = m.fbuf()
    y0 = bx.w("dos_cony")
    rws = bx.w("dos_conrows") * 8
    lo, hi = max(0, y0 - 6), min(h, y0 + rws + 6)

    def runs(y):
        row = px[y * w * 3:(y + 1) * w * 3]
        out, st = [], None
        for x in range(w):
            if row[x * 3] <= 128:
                if st is None:
                    st = x
            elif st is not None:
                out.append((st, x - 1))
                st = None
        if st is not None:
            out.append((st, w - 1))
        return out

    # **THE RECTANGLE IS LEARNED ONCE, ON A BAND THAT IS MOSTLY EMPTY.**  The
    # modal-dark scan below needs blank rows to find the band's x extent by:
    # a row with text in it has its dark run BROKEN by every glyph, so once the
    # band fills, the widest dark run is a gap between two words and the tally
    # picks a sliver.  Measured: the same full band read 90,053 dark pixels
    # with the rect learned empty and 67,531 with it re-derived - and the
    # "lit" count that came with the sliver was 108, which reads exactly like
    # a band that never painted.  That cost a whole A/B against innocent
    # kernel code.  So a caller that will fill the band learns the rect first
    # and passes it back in.
    if rect is not None:
        x1, x2 = rect
    else:
        tally = {}
        for y in range(lo, hi):
            r = runs(y)
            if not r:
                continue
            a, b = max(r, key=lambda ab: ab[1] - ab[0])
            if b - a >= 200:
                tally[(a, b)] = tally.get((a, b), 0) + 1
        if not tally:
            return 0, 0
        x1, x2 = max(tally, key=lambda k: (tally[k], k[1] - k[0]))
    span = x2 - x1 + 1
    lit = dark = 0
    for y in range(lo, hi):
        row = px[y * w * 3:(y + 1) * w * 3]
        d = sum(1 for x in range(x1, x2 + 1) if row[x * 3] <= 128)
        if d * 10 < span * 6:
            continue
        lit += span - d
        dark += d
    return lit, dark


def band_rect(m, bx):
    """The band's (x1, x2), learned while it is mostly empty - see band_lit."""
    w, h, px = m.fbuf()
    y0 = bx.w("dos_cony")
    rws = bx.w("dos_conrows") * 8
    lo, hi = max(0, y0 - 6), min(h, y0 + rws + 6)
    tally = {}
    for y in range(lo, hi):
        row = px[y * w * 3:(y + 1) * w * 3]
        out, st = [], None
        for x in range(w):
            if row[x * 3] <= 128:
                if st is None:
                    st = x
            elif st is not None:
                out.append((st, x - 1))
                st = None
        if st is not None:
            out.append((st, w - 1))
        if not out:
            continue
        a, b = max(out, key=lambda ab: ab[1] - ab[0])
        if b - a >= 200:
            tally[(a, b)] = tally.get((a, b), 0) + 1
    if not tally:
        return None
    return max(tally, key=lambda k: (tally[k], k[1] - k[0]))


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - a plain `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch %s" % BOX)
        os88marty.settle(m)
        bx = Box(m)

        # --- 1: the first paint shows the prompt ----------------------------
        vt, vr, cy, vis = bx.visible()
        live = [r for r in vis if r.strip()]
        print("dosconcga: vrows=%d vtop=%d cy=%d, band shows %d live row(s)"
              % (vr, vt, cy, len(live)))
        for r in live[:4]:
            print("   | %s" % r)
        if vr >= 25:
            fail("con_vrows is %d, so this machine is NOT short and the row "
                 "cannot answer the question it exists for - CGA's 200 lines "
                 "should leave 17 of 25 (SPEC.md 96.33.8)" % vr)
        if not (vt <= cy < vt + vr):
            fail("the cursor is on buffer row %d and the band shows rows "
                 "%d..%d, so the prompt is off screen (SPEC.md 96.33.8)"
                 % (cy, vt, vt + vr - 1))
        if not any("DOS Version" in r for r in vis):
            fail("the band shows rows %d..%d and the version banner is not "
                 "among them: %r" % (vt, vt + vr - 1, live))
        if not any(r.rstrip().endswith(">") for r in vis):
            fail("no prompt row in the band's rows %d..%d: %r"
                 % (vt, vt + vr - 1, live))

        # --- 2: ...and there are pixels ------------------------------------
        lit, dark = band_lit(m, bx)
        print("dosconcga: the band is %d lit / %d dark pixels" % (lit, dark))
        if lit < 200:
            fail("the band has %d lit pixels: the buffer can be perfect and "
                 "the band still black, which is the whole reason this check "
                 "reads the glass" % lit)
        if dark < lit:
            fail("the band is %d lit against %d dark - a console is white on "
                 "BLACK (SPEC.md 96.33)" % (lit, dark))

        # --- 3: and it tracks once the console scrolls ----------------------
        seen = set()
        for n in range(7):
            m.type_text("VER\n")
            os88marty.settle(m)
            vt, vr, cy, vis = bx.visible()
            seen.add(vt)
            if not (vt <= cy < vt + vr):
                fail("after %d command(s) the cursor is on row %d and the band "
                     "shows %d..%d - the view stopped following it"
                     % (n + 1, cy, vt, vt + vr - 1))
            if not any(r.rstrip().endswith(">") for r in vis):
                fail("after %d command(s) no prompt is in view: rows %d..%d"
                     % (n + 1, vt, vt + vr - 1))
        print("dosconcga: vtop took %s as the console filled"
              % sorted(seen))
        if len(seen) < 2:
            fail("vtop never moved (%s) in %d commands, so the SCROLLING half "
                 "was never exercised and this row proves only the first paint"
                 % (sorted(seen), 7))

        lit, dark = band_lit(m, bx)
        print("dosconcga: after scrolling, %d lit / %d dark" % (lit, dark))
        if lit < 200:
            fail("the band went blank once it scrolled: %d lit pixels. The "
                 "viewport shift is spent as [con_scrl] and a shift that is "
                 "not spent leaves the band showing the old rows (96.33.8)"
                 % lit)

        # --- 4: HELP FITS THIS BAND, WHICH IS THE SHORTEST ONE (96.33.23.1) -
        # HELP has no pager, and the only reason it may not have one is that
        # its sixteen lines and the prompt after them fit a CGA's 17 rows.
        # THIS is the machine that decides it - a Hercules has 25 and could
        # never show the constraint. The assembler counts the lines (DHL's
        # DH_LINES); this counts what a user can SEE, which is the thing the
        # count is a proxy for.
        m.type_text("HELP\n")
        os88marty.settle(m)
        time.sleep(2.0)
        os88marty.settle(m)
        vt, vr, cy, vis = bx.visible()
        first = any("CD [path]" in r for r in vis)
        last = any("cmd > file" in r for r in vis)
        prompt = any(r.rstrip().endswith(">") for r in vis)
        print("dosconcga: HELP - first line %s, last line %s, prompt %s, "
              "in %d rows" % (first, last, prompt, vr))
        if not first:
            fail("HELP's FIRST line has scrolled off a %d-row band: the text "
                 "is over sixteen lines, or the prompt after it takes more "
                 "than one (SPEC.md 96.33.23.1). Rows %d..%d: %r"
                 % (vr, vt, vt + vr - 1, [r for r in vis if r.strip()][:3]))
        if not last:
            fail("HELP's LAST line is not in the band, so it printed short "
                 "(SPEC.md 96.33.23)")
        if not prompt:
            fail("HELP left no prompt in view - a built-in owes one, having "
                 "no exit line to bring it back (SPEC.md 96.33.17.1)")

    print("dosconcga: ok - 17 rows of 25, the prompt in view at the first "
          "paint, still in view after the console scrolled, and HELP fitting "
          "the band whole")


if __name__ == "__main__":
    main()
