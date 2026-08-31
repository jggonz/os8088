#!/usr/bin/env python3
"""DOES A MAXIMIZE AND A RESTORE LEAVE THE PICTURE ALONE? (SPEC.md 42.8.6.1)

    make && python3 tests/paintsize.py [--machine os8088_xt_vga]

Maximizing Paint GROWS the canvas - 448x258 to 670x258 on a Hercules - and
restoring shrinks it back, and both go through `pt_resize`, which stages the
whole picture through the undo image on its in-place path. So the two clicks
walk `pt_ucopy` over every row of the canvas at two different strides, which
is the one thing that made SPEC.md 42.8.6.1 visible.

TWO ASSERTIONS, and the second is the one that would have caught it:

  CLEAN   a blank canvas is still blank afterwards - every byte 0xFF. The
          defect wrote a band of whatever was in memory past the undo image
          across the bottom of the picture (the canvas is stored bottom row
          first), and it was IN THE CANVAS: it survived a save and a reload.

  QUICK   the restore finishes inside a cycle budget. It was **97 seconds** on
          a 4.77MHz 8088, of which 79 were inside `pt_ucopy` doing two 64KB
          `rep movsw` per row for blocks the row did not have - reported from
          the field as a hard freeze, which it is not: the machine is alive
          throughout and the screen blanker takes over if it is left alone.

The budget is deliberately loose. What it is guarding against is a run away by
two orders of magnitude, not a regression of ten percent - and the honest cost
of a resize at these sizes is real work: 86,688 bytes staged, a wipe, a
copy-back and a full repaint.
"""
import argparse
import collections
import os
import sys

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
HZ = 4772727.0
BUDGET_MS = 20000.0             # 20 s: the defect took 97 and the fix ~1


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name):
    return int.from_bytes(m.read(_boff(seg, name), 2), "little")


def _canvas(m, seg):
    """(bad bytes, total, w, h, stride) - a blank canvas is all 0xFF past
    the 128-byte DIB header pt_bmp_hdr stamps at the front."""
    base, stride = _bss(m, seg, "pt_base"), _bss(m, seg, "pt_stride")
    ch, r0 = _bss(m, seg, "pt_ch"), _bss(m, seg, "pt_rowoff")
    n, d, off = stride * ch, b"", (base << 4) + r0
    while len(d) < n:
        d += m.read(off + len(d), min(32768, n - len(d)))
    c = collections.Counter(d[128:])
    return (len(d) - 128 - c.get(0xFF, 0), n, _bss(m, seg, "pt_cw"), ch,
            stride, c.most_common(2))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
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
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("paintsize: PAINT.O88 did not open")
        pw, seg = got

        print()
        bad, n, cw, ch, st, top = _canvas(m, seg)
        print("   opened          %dx%d stride=%d   %d of %d bytes not FF"
              % (cw, ch, st, bad, n))
        first = (cw, ch)

        # --- MAXIMIZE ---------------------------------------------------------
        wr = dispcp.win_rect(m, S, pw)
        mo.to(4, 4)
        os88marty.settle(m)
        mo.dblclick(wr[0] + 60, wr[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        big = dispcp.win_rect(m, S, pw)
        bad, n, cw, ch, st, top = _canvas(m, seg)
        print("   maximized       %dx%d stride=%d   %d of %d bytes not FF   %r"
              % (cw, ch, st, bad, n, big))
        if bad:
            fails.append("the maximize left %d bytes of %d in the canvas "
                         "(%s)" % (bad, n, top))
        if big == wr:
            fails.append("SETUP: the title double-click did not maximize, so "
                         "this run proves nothing")

        # --- RESTORE, timed ---------------------------------------------------
        mo.to(4, 4)
        os88marty.settle(m)
        t0 = m.status()["cycles"]
        mo.dblclick(big[0] + 60, big[1] + 9)
        idle = S("sch_idle_body.spin")
        for _ in range(240):                    # up to ~2 minutes of guest time
            m.advance(frames=30)
            m.run()
            r = m.regs()
            if (r["cs"] << 4) + r["ip"] == idle and _bss(m, seg, "pt_cw") != cw:
                break
        ms = (m.status()["cycles"] - t0) / HZ * 1000.0
        os88marty.settle(m)
        bad, n, cw2, ch2, st2, top = _canvas(m, seg)
        print("   restored        %dx%d stride=%d   %d of %d bytes not FF   %r"
              % (cw2, ch2, st2, bad, n, dispcp.win_rect(m, S, pw)))
        print("   the restore took %.0f ms of guest time (budget %.0f)"
              % (ms, BUDGET_MS))
        if bad:
            fails.append("the restore left %d bytes of %d in the canvas (%s) - "
                         "SPEC.md 42.8.6.1" % (bad, n, top))
        if (cw2, ch2) != first:
            fails.append("the canvas came back %dx%d, not the %dx%d it opened "
                         "at" % (cw2, ch2, first[0], first[1]))
        if ms > BUDGET_MS:
            fails.append("the restore took %.0f ms, over the %.0f budget"
                         % (ms, BUDGET_MS))

    print()
    if fails:
        for f in fails:
            print("   FAIL: %s" % f)
        return 1
    print("paintsize: PASS - a maximize and a restore leave a blank canvas "
          "blank,\n  bring it back to the size it opened at, and finish inside "
          "the budget")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
