#!/usr/bin/env python3
"""THE CARET'S BAR, AFTER AN EDIT THAT MOVED IT (SPEC.md 83.1.1).

`os88line_edit` repaints what one keystroke changed instead of the whole field
(96.19.1), and it was given the view and the length to work that out but NOT
the caret - which is the third thing a key moves.  The bar is a 1px
`gfx_fill` and nothing else on the machine knows it is there, so a cell the
narrow path does not repaint KEEPS it: type `ABCD` into any field, rub it out,
and what is left is four bars and no text.

**THE ASSERTION IS THREE HISTORIES AND ONE PICTURE**, which is what makes it
impossible to write vacuously.  Three different sequences of keys leave the
field in exactly the same state - `AB`, two characters, the caret at the end -
so the pixels must be identical, and no notion of what a caret looks like has
to be encoded here at all:

  1  typed        `AB`                     - two appends, the path the fast
                                             case is FOR, and the reference
  2  backspaced   `ABCD` then two rubouts  - the user's report
  3  moved        `AB`, Left, End          - the second leak, and the one
                                             nobody would have found: a caret
                                             move that lands at the END is the
                                             only shape of move that reaches
                                             the narrow path (any other lands
                                             away from it, which is already a
                                             full redraw)

A row that only compared the pixels could pass with the field blank, so the
guest's own `LN_LEN`, `LN_CAR` and buffer are asserted per history, and the
reference is required to DIFFER from the empty field - otherwise nothing was
being looked at.

**THE RESET BETWEEN HISTORIES IS Home THEN Delete**, and it is chosen because
it is clean WITH THE DEFECT IN: Home lands the caret away from the end, which
is `os88line_edit`'s own fall-back to a full redraw, and a Delete never moves
the caret, so neither can leave a bar.  Clearing with backspaces would carry
history 1's trail into history 2 and the comparison would then be between two
dirty pictures.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
BOX = "A:/APPS/DOS.O88"
# The menu bar carries a CLOCK, which is the one thing on this screen that
# changes for a reason that is not the field.  Everything below it - the
# window's chrome, its console band, the desktop either side - is static
# between captures taken seconds apart, so the comparison is the whole screen
# minus the bar.
SKIP_TOP = 24


def fail(msg):
    print("linecar: FAIL: %s" % msg)
    sys.exit(1)


class Field(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    @property
    def seg(self):
        # **RESOLVED PER ACCESS, NOT CACHED** (SPEC.md 66.6.1.2): the DOS
        # box's region MOVES now - it is a re-homed carve and was pinned only
        # until that section - so a base banked in __init__ names the bytes
        # the package used to occupy, and decodes as plausible rubbish.
        return dosmap.instance(self.m)

    @property
    def at(self):
        return (self.seg << 4) + self.dm["dos_pln"]

    def w(self, name):
        off = self.at + self.dm[name]
        return int.from_bytes(self.m.read(off, 2), "little")

    def text(self):
        base = (self.seg << 4) + self.w("LN_BUF")
        return self.m.read(base, 80).split(b"\0")[0].decode("latin-1")

    def state(self):
        return (self.w("LN_LEN"), self.w("LN_CAR"), self.text())


def shot(m):
    w, h, px = m.fbuf()
    return w, h, bytes(px)


def diff(a, b):
    """Where two captures disagree below the menu bar: (count, bbox)."""
    (w, h, pa), (_, _, pb) = a, b
    n, x0, y0, x1, y1 = 0, w, h, -1, -1
    for y in range(SKIP_TOP, h):
        row = y * w * 3
        if pa[row:row + w * 3] == pb[row:row + w * 3]:
            continue
        for x in range(w):
            i = row + x * 3
            if pa[i:i + 3] != pb[i:i + 3]:
                n += 1
                x0, x1 = min(x0, x), max(x1, x)
                y0, y1 = min(y0, y), max(y1, y)
    return n, (x0, y0, x1, y1)


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - `make` builds it" % SYS)

    with os88ui.boot(SYS, apps=SYS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch the DOS box")
        f = Field(m)
        x1, y1, x2, y2 = dosmap.rect(m, f.seg, f.dm, "dos_pln")
        os88mouse.Mouse(marty=m).click((x1 + x2) // 2, (y1 + y2) // 2)
        os88marty.settle(m)
        if not f.w("LN_FOCUS"):
            fail("clicking the path box did not give it the caret, so nothing "
                 "below is about a field with a bar in it")

        def reset():
            m.key("Home")
            for _ in range(f.w("LN_LEN") + 1):
                if not f.w("LN_LEN"):
                    break
                m.key("Delete")
            os88marty.settle(m)
            if f.state()[:2] != (0, 0):
                fail("the reset left the field at %r and not empty" % (f.state(),))

        reset()
        empty = shot(m)

        def history(name, keys):
            reset()
            for k in keys:
                if len(k) == 1:
                    m.type_text(k)
                else:
                    m.key(k)
            os88marty.settle(m)
            st = f.state()
            if st != (2, 2, "AB"):
                fail("history %r left the field at LEN %d CAR %d %r, and every "
                     "one of the three must leave it at LEN 2 CAR 2 'AB' - "
                     "otherwise the pictures are of different states and the "
                     "comparison below says nothing" % ((name,) + st))
            print("linecar: %-12s -> LEN %d CAR %d %r" % ((name,) + st))
            return shot(m)

        ref = history("typed", ["A", "B"])

        n, box = diff(ref, empty)
        if n < 40:
            fail("the field with 'AB' typed into it differs from the EMPTY "
                 "field in %d pixels. Two glyphs and a caret are more than "
                 "that, so the capture is not looking at the field and every "
                 "comparison below would pass on two identical blanks" % n)
        print("linecar: ...and it differs from the empty field in %d pixels, "
              "so the captures are looking at it" % n)

        # **EVERY HISTORY IS RUN, and a failing one does not end the row.**
        # They are two different leaks in the same routine - the backspace and
        # the caret move - so stopping at the first would hide whether the
        # other one is fixed, which is exactly the reading a partial fix wants
        # to be given.
        bad = []
        for name, keys in (("backspaced", ["A", "B", "C", "D", "\b", "\b"]),
                           ("moved", ["A", "B", "ArrowLeft", "End"])):
            got = history(name, keys)
            n, (bx0, by0, bx1, by1) = diff(ref, got)
            if n:
                bad.append("%s: %d pixels differ, in x %d..%d y %d..%d"
                           % (name, n, bx0, bx1, by0, by1))
                print("linecar: ...%s DIFFERS: %s" % (name, bad[-1]))
            else:
                print("linecar: ...%s is pixel-identical to it" % name)
        if bad:
            fail("the field reached LEN 2 CAR 2 'AB' by a route that does not "
                 "look the same as the field that was typed - %s. A cell the "
                 "narrow repaint skipped kept the 1px bar that was standing in "
                 "it (SPEC.md 83.1.1)" % "; ".join(bad))

    print("linecar: ok - three histories, one state, one picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
