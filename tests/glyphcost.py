#!/usr/bin/env python3
"""WHAT DID SPEC.md 13.15.1 COST A CONTROL GLYPH, PER CALL, IN GUEST CYCLES?

    make && make glyphbn && python3 tests/glyphcost.py

os88ui_glyph used to be four 12x12 bitmaps put down through the masked sprite
pass - ONE drawing call - and is three to eight fills now. That trade was taken
on LOOK and on SIZE (docs/plans/completed/CTRL-GLYPH-PLAN.md 2: the bitmap machinery is
414 bytes a copy in twenty-three copies), with the owner saying in as many
words that a control drawn once and then sat on is not priced like a hot loop.
So this row exists to catch a REGRESSION, and its output is a table rather than
a verdict on the design.

WHY THE NUMBER IS TRUSTWORTHY, which is the only interesting thing about the
method. tests/glyphbn carries BOTH routines in ONE package - the pre-13.15.1
one lifted verbatim beside today's - so an arm differs from its twin in the
code being timed and in nothing else: same kernel, same boot, same adapter,
same coordinates, same clip state. A cross-checkout A/B could not say that,
because this arc took the whole gfx_line family out of the kernel underneath.

AND WHY IT IS CYCLES AND NOT MILLISECONDS OFF A CLOCK. MartyPC's counter does
not advance while a breakpoint holds the guest, so the span between two marker
addresses is exactly the cycles the enclosed code executed - PERFORMANCE.md
Set 141's instrument. The host is 4x real time and irrelevant to the answer.

WHAT IT WOULD CATCH: an arm that stops running (the span goes to the empty
loop's floor); a routine that gets slower than its bitmap predecessor by more
than the margin the owner set; and a marker pair that never fires, which is a
bench that measured nothing and is failed rather than reported as zero.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, os88geom, os88build, dispcp   # noqa: E402
from cycweb import Pkg, u16, shot                               # noqa: E402

HZ = 4772727.0                  # the 5150's 8088, PERFORMANCE.md Part 2
BOX = 12                        # OS88UI_GW: a control glyph is 12x12
GB_SX, GB_SX2, GB_SY, GB_SPITCH = 8, 28, 8, 16      # ...the shape grid's
N = 8                           # GB_N: calls an arm
BAR = 2.0                       # ms a call the fill path may be
                                # SLOWER by. The owner's number, and
                                # a difference rather than a ceiling
FAIL = []

# The arms, in the order gb_onkey runs them. `old` names the arm this one is
# to be read against; the empty loop is read against nothing.
ARMS = [
    ("gb_m_nul",   "the empty loop",        None),
    ("gb_m_ocoff", "check, clear  BITMAP",  None),
    ("gb_m_ocon",  "check, set    BITMAP",  None),
    ("gb_m_oroff", "radio, clear  BITMAP",  None),
    ("gb_m_oron",  "radio, set    BITMAP",  None),
    ("gb_m_ncoff", "check, clear  FILLS",   "gb_m_ocoff"),
    ("gb_m_ncon",  "check, set    FILLS",   "gb_m_ocon"),
    ("gb_m_nroff", "radio, clear  FILLS",   "gb_m_oroff"),
    ("gb_m_nron",  "radio, set    FILLS",   "gb_m_oron"),
]


def check(ok, what):
    print("   %-56s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def box(px, W, x, y):
    """The 12x12 at (x,y) as rows of 0/1. Polarity does not matter here - both
    columns are read the same way and only their DIFFERENCE is asserted - so
    this does not invert for Hercules the way tests/radio.py has to."""
    return [[1 if px[(y + r) * W + (x + c)] else 0 for c in range(BOX)]
            for r in range(BOX)]


def pkg_syms(src, incs=("apps/",)):
    """tests/radio.py's, and for its reason: a package's symbols are not in
    os88sym's kernel map, so the source is re-assembled with `[map symbols]`."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in incs:
            cmd += ["-I", i]
        cmd += ["-o", bp, cp]
        subprocess.run(cmd, check=True)
        syms = {}
        for ln in open(mp):
            f = ln.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                syms[f[2]] = int(f[0], 16)
        return syms, open(bp, "rb").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/glyphbn360.img")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    S = os88sym.linear

    syms, image = pkg_syms("tests/glyphbn/glyphbn.asm")
    # THE DISK MUST CARRY WHAT THE SYMBOLS DESCRIBE (tests/radio.py's trap,
    # one row along): `make` does not build glyphbn, so a fresh symbol table
    # against a stale image prices whatever is at those addresses now.
    try:
        built = open(os88build.at("build/glyphbn.bin"), "rb").read()
    except OSError:
        sys.exit("glyphcost: no build/glyphbn.bin - run `make glyphbn`")
    if built != image:
        sys.exit("glyphcost: build/glyphbn.bin is %d bytes and the source "
                 "assembles to %d - the disk is BEHIND THE TREE. "
                 "Run `make glyphbn`." % (len(built), len(image)))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "GLYPHBN.O88")))
        mo.dblclick(rx, ry)
        seg = None

        def launched(_):
            nonlocal seg
            for w in os88geom.windows(m, S):
                if w.title.startswith("GlyphBench"):
                    seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                     + os88geom.W_SEG, 2))
            return bool(seg)
        try:
            os88marty.until(m, launched, "GlyphBench's window", poll=0.3,
                            limit=120)
        except os88marty.MartyError:
            pass                        # ...and the next line says so
        if not seg:
            sys.exit("glyphcost: GLYPHBN did not launch")
        p = Pkg(m, seg, syms)
        mo.to(4, 4)             # the arrow off the glyph: it is nobody's
        os88marty.settle(m)     # business, and it would be redrawn under us

        base = seg << 4
        marks = []
        for name, _, _ in ARMS:
            marks += [base + syms[name], base + syms[name + "_e"]]

        runs = p.rw("gb_runs")
        # EVERY LOOKUP BELOW IS BY ADDRESS AND NOT BY NAME. bp_trace names a
        # hit out of `_by_addr`, which only knows the KERNEL symbols it
        # resolved; a package address passed as a raw int comes back as
        # "?0A3F1", so tr.count/tr.first/tr.span would every one of them
        # answer 0 or None here - and a span of None reads as "the bench
        # never ran" rather than as the wrong key having been asked.
        end = base + syms["gb_m_nron_e"]
        hit = lambda ad: [h for h in tr.hits if h["addr"] == ad]
        with os88marty.bp_trace(m, *marks) as tr:
            m.key("KeyA")               # gb_onkey IS the bench
            tr.until(lambda: p.rw("gb_runs") > runs and bool(hit(end)),
                     "the bench to run all nine arms", 240)

        # A SPAN IS FIRST-ENTRY TO ITS FIRST EXIT AFTER IT, and not
        # first-to-LAST: the arms all draw at one point, so a repaint arriving
        # mid-block would run them again and the last exit could belong to a
        # second pass - which would price two runs as one.
        def span(name):
            i = hit(base + syms[name])
            if not i:
                return None
            i = i[0]
            j = next((h for h in hit(base + syms[name + "_e"])
                      if h["cycles"] > i["cycles"]), None)
            return None if j is None else j["cycles"] - i["cycles"]

        cyc = {name: span(name) for name, _, _ in ARMS}
        missing = [n for n, c in cyc.items() if c is None]
        if missing:
            sys.exit("glyphcost: no span for %s - the bench did not run, so "
                     "nothing here was measured" % ", ".join(missing))

        floor = cyc["gb_m_nul"]
        print()
        print("   %d calls an arm; the empty loop is %d cycles and is taken "
              "off every row" % (N, floor))
        print()
        print("   %-22s %10s %10s %9s" % ("arm", "cycles", "per call", "ms @4.77"))
        print("   " + "-" * 54)
        per = {}
        for name, label, _ in ARMS:
            if name == "gb_m_nul":
                continue
            c = (cyc[name] - floor) / float(N)
            per[name] = c
            print("   %-22s %10d %10.0f %9.3f"
                  % (label, cyc[name], c, 1000.0 * c / HZ))
        print()
        print("   %-22s %10s %10s %9s" % ("", "bitmap", "fills", "change"))
        print("   " + "-" * 54)
        for name, label, old in ARMS:
            if not old:
                continue
            o, n = per[old], per[name]
            print("   %-22s %10.0f %10.0f %+8.1f%%"
                  % (label.replace("FILLS", "").strip(), o, n,
                     100.0 * (n - o) / o))
        print()

        # --- AND WHAT THE TWO ACTUALLY DRAW ----------------------------------
        # A cost table needs its pictures beside it, because the two arms do
        # NOT draw the same glyph and a reader would reasonably assume they
        # did. 13.15.1 picked shapes a FILL can lay - runs and rectangles -
        # where the old ones were arbitrary 12x12 bitmaps, so only the open
        # square comes out identical; the ring, the dot and the check's mark
        # are all redrawn. That is the design and not a defect, so this prints
        # both columns rather than asserting they match.
        #
        # WHAT IS ASSERTED is that each column drew a glyph at all - some ink
        # and some ground in the box. A column that silently drew NOTHING
        # would make its cost arm meaningless in exactly the way that looks
        # like a fast routine, and neither `did real work` nor the bar above
        # would notice: a fully-clipped fill still costs cycles.
        shapes = p.rw("gb_shapes")
        m.key("KeyB")
        os88marty.settle(m)
        if p.rw("gb_shapes") != shapes + 1:
            sys.exit("glyphcost: the shape grid did not draw, so nothing "
                     "below compared two pictures")
        mo.to(4, 4)             # the arrow off the grid before a capture: it
        os88marty.settle(m)     # is the kernel's pixels, not the control's
        W, H, px = shot(m)
        gx, gy = p.rw("gb_gx"), p.rw("gb_gy")   # the guest's own content origin
        print()
        print("   the shape grid at (%d,%d), screen %dx%d - BITMAP | FILLS"
              % (gx, gy, W, H))
        print()
        for i, kind in enumerate(("radio clear", "radio set",
                                  "check clear", "check set")):
            y = gy + GB_SY + i * GB_SPITCH
            a = box(px, W, gx + GB_SX, y)
            b = box(px, W, gx + GB_SX2, y)
            d = sum(1 for r in range(BOX) for c in range(BOX)
                    if a[r][c] != b[r][c])
            print("   %s   (%d of %d px differ)" % (kind, d, BOX * BOX))
            for r in range(BOX):
                print("      %s   %s"
                      % ("".join("#" if v else "." for v in a[r]),
                         "".join("#" if v else "." for v in b[r])))
            print()
            for col, g in (("bitmap", a), ("fills", b)):
                lit = sum(sum(r) for r in g)
                check(0 < lit < BOX * BOX,
                      "%-11s %-6s: drew a glyph (%d of %d px ink)"
                      % (kind, col, lit, BOX * BOX))

        # --- the assertions -------------------------------------------------
        # A bench that RAN is the first thing, because every number above is
        # plausible when it did not.
        check(p.rw("gb_runs") == runs + 1,
              "the bench ran exactly once (%d -> %d)"
              % (runs, p.rw("gb_runs")))
        for name, label, _ in ARMS:
            if name == "gb_m_nul":
                continue
            check(cyc[name] > floor * 2,
                  "%s did real work (%d cycles vs the loop's %d)"
                  % (label, cyc[name], floor))
        # ...and the regression bar, which is a DIFFERENCE and not an absolute.
        # The owner set it in those terms - *"between 1ms and 2ms is not huge -
        # this is not a live drawing, it's drawn once then it sits there until
        # they interact with it"* - so what is gated is how much SLOWER the
        # fill path may be than the bitmap one it replaced, on a control that
        # is drawn once. An absolute ceiling would fail this row on the
        # BITMAP's own 6ms, which is the code 13.15.1 removed.
        for name, label, old in ARMS:
            if not old:
                continue
            d = 1000.0 * (per[name] - per[old]) / HZ
            check(d < BAR, "%s costs less than %.0fms more than the bitmap "
                           "did (%+.3f ms)"
                  % (label.replace("FILLS", "").strip(), BAR, d))

    print()
    if FAIL:
        print("glyphcost: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("glyphcost: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
