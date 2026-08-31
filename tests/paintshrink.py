#!/usr/bin/env python3
"""DOES A SHRINK GIVE BACK WHAT IT CAN? (SPEC.md 42.17)

    make && python3 tests/paintshrink.py [--machine os8088_xt_vga]

SPEC.md 42.9's rule is that a shrink which would throw ink away is refused per
axis. What "refused" USED to mean was pinned back to the size it already had,
so one stroke near the middle of the picture kept the whole canvas: drag the
grow box in by 200px over ink 120px from the right edge and nothing moved.

42.17 makes the axis give back everything it can - the smallest width for
which pt_lose_w says nothing is dropped, found by binary search over that
predicate.

THE SHAPE OF THE TEST is a stroke laid at a KNOWN place and then a drag that
asks for far less than the ink allows:

  1. ink a short stroke near the canvas's top-left, so the right and bottom
     thirds are provably white
  2. drag the grow box hard in - past the ink on both axes
  3. the canvas must SHRINK, and stop where the ink is, not where it started

  ASSERTED:  the canvas is strictly smaller than it was    (it moved at all)
             the canvas still contains every inked column and row
             ...and it is TIGHT: one column narrower would have lost ink,
             which is the difference between "gave back what it could" and
             "gave back something"

The last one is what makes this a test of 42.17 rather than of 42.9: a build
that shrank to any safe-but-arbitrary size passes the first two.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
CWHITE = 15                     # apps/paint: white is 0x0F in a nibble
EQU = ["PT_SZ_Y", "PT_SZ_DY", "PT_SZ_BH", "PT_SZ_BX", "PT_SZ_BW", "PT_SZ_AY",
       "PT_SZ_AH", "PT_CW_MIN", "PT_CH_MIN"]


def _equates(src="apps/paint/paint.asm", incs=("apps/",)):
    """A package's numeric `equ`s, by making nasm EMIT them.

    They are not labels, so `[map]` does not carry them and pkg_syms cannot
    help; and a test that mirrors `PT_SZ_Y equ PT_DIM_Y` by hand is a test
    that clicks the wrong pixel the day the palette gains a row. Appending a
    word per constant and reading them back out of the binary cannot go
    stale.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        body = open(src).read() + "\nsection .text\n__equ_probe:\n"
        body += "".join("    dw %s\n" % n for n in EQU)
        open(cp, "w").write(body + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], []) + ["-o", bp, cp],
                       check=True, capture_output=True)
        off = None
        for line in open(mp):
            f = line.split()
            if len(f) >= 3 and f[-1] == "__equ_probe":
                off = int(f[0], 16)
        if off is None:
            sys.exit("paintshrink: nasm emitted no __equ_probe")
        data = open(bp, "rb").read()
        return {n: int.from_bytes(data[off + i * 2:off + i * 2 + 2], "little")
                for i, n in enumerate(EQU)}


def _type(m, n):
    for ch in str(n):
        m.key("Digit%s" % ch)
        m.advance(frames=2)
        m.run()


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name, w=2):
    return int.from_bytes(m.read(_boff(seg, name), w), "little")


def _inked(m, seg):
    """(right-most inked column, bottom-most inked row) or (-1, -1).

    Read out of the canvas itself rather than out of Paint's own bounds, so
    the oracle is the picture and not the bookkeeping under test.
    """
    w, h = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
    stride = _bss(m, seg, "pt_stride")
    if _bss(m, seg, "pt_planar", 1):
        return None                                 # packed only, like 42.17
    rx, ry = -1, -1
    base = _bss(m, seg, "pt_rowoff")                # row 0's offset...
    for row in range(h):
        off = int.from_bytes(m.read(_boff(seg, "pt_rowoff") + row * 2, 2),
                             "little")
        seg2 = int.from_bytes(m.read(_boff(seg, "pt_rowseg") + row * 2, 2),
                              "little")
        data = m.read((seg2 << 4) + off, (w + 1) // 2)
        for i, byte in enumerate(data):
            if byte == 0xFF:
                continue
            hi, lo = byte >> 4, byte & 0x0F
            if lo != CWHITE and i * 2 + 1 < w:
                c = i * 2 + 1
            elif hi != CWHITE:
                c = i * 2
            else:
                continue
            rx = max(rx, c)
            ry = max(ry, row)
    return rx, ry


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]
        pw = dispcp.win_list(m, S)[-1]

        if _bss(m, seg, "pt_planar", 1):
            print("paintshrink: SKIP - %s gives Paint a PLANAR canvas and "
                  "42.17's predicate is exercised here on the packed one"
                  % a.machine)
            return 0

        w0, h0 = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        ASKW, ASKH = 60, 40                         # well under PT_CW/CH_MIN's
                                                    # floor and under the ink
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        mo.to(cx0 + 120, cy0 + 80)                  # a stroke well OUT in the
                                                    # picture, so the request
                                                    # below is asking to lose it
        os88marty.settle(m)
        mo._edge(True)
        for _ in range(8):
            m.mouse(dx=6, dy=6, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        ink = _inked(m, seg)
        print("   canvas %dx%d, ink reaches column %d row %d"
              % (w0, h0, ink[0], ink[1]))
        if ink[0] < 0:
            fails.append("SETUP: the stroke inked nothing, so there is no "
                         "artwork for the shrink to be stopped by")

        # THE SIZE BOXES AND NOT THE GROW BOX: the window has a minimum size,
        # so a drag can never ask for a canvas narrower than about 390 and the
        # clamp under test is unreachable that way. Typing is how SPEC.md 42.9
        # describes reaching it, and PT_CW_MIN is 46.
        #
        # ONE AXIS PER APPLY, which is Paint's design and not a limitation of
        # the harness: pt_szdraw rewrites the box that does NOT have focus from
        # the canvas's live size, so clicking the second box discards what was
        # typed into the first.
        #
        # AND THE ANSWER IS READ AT pt_resize, not off the canvas afterwards.
        # Applying a size RESIZES THE WINDOW to match (pt_szapply), and Paint's
        # minimum window width is wider than a narrow canvas - so pt_track
        # re-fits the width straight back up on the next paint and the clamped
        # value is gone before anything else can look. pt_resize is handed
        # exactly what 42.17 decided.
        E = _equates()
        pm = dispapps._map("paint")
        RZ = (seg << 4) + pm["pt_resize"]

        def apply(box, value):
            ox, oy = _bss(m, seg, "pt_ox"), _bss(m, seg, "pt_oy")
            bxm = ox + E["PT_SZ_BX"] + E["PT_SZ_BW"] // 2
            by = oy + E["PT_SZ_Y"] + E["PT_SZ_BH"] // 2
            if box:
                by += E["PT_SZ_DY"]
            mo.click(bxm, by)
            os88marty.settle(m)
            if _bss(m, seg, "pt_fbox", 1) != box + 1:
                return None
            _type(m, value)
            m.bp_exec(RZ)
            m.key("Enter")                          # pt_szkey: 13 is Apply
            out = None
            if m.wait_stop(limit=120.0):
                r = m.regs()
                out = (r["ax"], r["dx"])
            m.breakpoints([])
            m.run()
            m.advance(frames=600)
            m.run()
            os88marty.settle(m)
            return out

        gotw = apply(0, ASKW)
        goth = apply(1, ASKH)
        print("   width  box asked %3d -> pt_resize handed %r" % (ASKW, gotw))
        print("   height box asked %3d -> pt_resize handed %r" % (ASKH, goth))
        w1, h1 = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        print("   the canvas settled at %dx%d" % (w1, h1))
        ink2 = _inked(m, seg)
        print("   ink now reaches column %d row %d" % ink2)

    if ink and ink[0] >= 0:
        # An axis may not go below the ink (42.9, which 42.17 does not relax)
        # and may not stop ABOVE it either, once something smaller was asked
        # for - that second half is the whole of 42.17.
        wantw, wanth = max(ASKW, ink[0] + 1), max(ASKH, ink[1] + 1)
        if ASKW >= ink[0] + 1 or ASKH >= ink[1] + 1:
            fails.append("SETUP: %dx%d does not reach past the ink at %d,%d, "
                         "so the clamp under test never ran"
                         % (ASKW, ASKH, ink[0], ink[1]))
        for (nm, got, want, asked, i) in (("width", gotw, wantw, ASKW, 0),
                                          ("height", goth, wanth, ASKH, 1)):
            if got is None:
                # pt_setsize calls pt_resize only when the size MOVED, so "it
                # never ran" and "the canvas is where it started" together are
                # the axis being refused outright - which is precisely the
                # behaviour 42.17 replaced, not a broken harness.
                if (w1, h1)[i] == (w0, h0)[i]:
                    fails.append("the %s did not move at all: %d was asked "
                                 "for over ink reaching %d, and %d loses "
                                 "nothing - the axis was REFUSED outright, "
                                 "which is what SPEC.md 42.17 replaced"
                                 % (nm, asked, ink[i], want))
                else:
                    fails.append("SETUP: the %s moved to %d without pt_resize "
                                 "being reached" % (nm, (w1, h1)[i]))
                continue
            if got[i] < want:
                fails.append("the %s was handed %d, inside the ink that "
                             "reaches %d (SPEC.md 42.9)"
                             % (nm, got[i], ink[i]))
            elif got[i] > want:
                fails.append("the %s stopped at %d where %d loses nothing and "
                             "%d was asked for: %d kept for no reason - the "
                             "axis gave back something rather than everything "
                             "it could (SPEC.md 42.17)"
                             % (nm, got[i], want, asked, got[i] - want))
        if ink2 != ink:
            fails.append("the shrink LOST INK - it reached column %d row %d "
                         "and now reaches %d %d (SPEC.md 42.9)" % (ink + ink2))
    if fails:
        for f in fails:
            print("paintshrink: %s" % f)
        return 1
    print("paintshrink: PASS - over ink at %d,%d, a request of %d gave back "
          "the width to %d and a request of %d the height to %d"
          % (ink[0], ink[1], ASKW, gotw[0], ASKH, goth[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
