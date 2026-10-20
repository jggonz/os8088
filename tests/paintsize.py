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

WHAT THE CLOCK ACTUALLY COVERS, and it is not mostly `pt_ucopy`. The bracket
starts before the double-click on purpose - the window manager acts on the
second click's RELEASE, so a regression inside the click handler is work this
row has to be able to see - and `os88mouse` spends guest time waiting on the
guest's own published `mouse_btn` for every packet it sends. Measured on the
Hercules machine: **10,217 ms of gesture and 125 ms of resize.** So the
budget's headroom is against a 97-second `pt_ucopy`, which trips it nine times
over, and NOT against a doubling of the copy, which it cannot see and was
never claimed to.

THE DETECTOR USED TO BE UNREACHABLE, and the number it printed was its own
timeout. It waited for the guest's IP to equal `sch_idle_body.spin` EXACTLY
while sampling a RUNNING guest - and since SPEC.md 8.1.2 the idle task spends
96.9% of its time in the `hlt` six bytes earlier, so the sample never landed
on that one byte: measured 0 hits in 240 samples, with `pt_cw` already back at
its opened value on the FIRST of them. The loop therefore ran to exhaustion
every time and reported 158 seconds, which is 240 advances of 30 frames plus
the free-running `run()` between them, for a restore that had finished before
the second sample. `[sch_cur] == [sch_idleslot]` is the same question asked of
the SCHEDULER instead of the program counter: it is a whole task's worth of
state rather than one instruction, so a sample cannot fall between it.

A run that never sees the machine go quiet now SAYS SO instead of printing its
own timeout as a measurement.
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
BUDGET_MS = 20000.0             # 20 s. UNCHANGED: the defect took 97 s and
                                # the bracket now measures 10.3 - see the
                                # docstring for what is gesture and what
                                # is resize. Nine times the headroom the
                                # 97-second walk needs to trip it


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
        # QUIET, not an address: the restore is over when the canvas has come
        # back AND the scheduler has gone back to the idle task. Three
        # consecutive samples, because a single one taken between two chunks
        # of the repaint would answer "idle" in the middle of the work; the
        # guest is stopped between samples, so nothing accrues to the clock
        # that this loop did not advance.
        cur, islot = S("sch_cur"), S("sch_idleslot")
        quiet = 0
        for _ in range(600):                    # ~24 s of guest time
            m.advance(frames=2)
            if (_bss(m, seg, "pt_cw") != cw
                    and m.read(cur, 1)[0] == m.read(islot, 1)[0]):
                quiet += 1
                if quiet == 3:
                    break
            else:
                quiet = 0
        else:
            fails.append("the machine never came back to the idle task with "
                         "the canvas restored - this run has NOT measured the "
                         "restore, and the time below is this loop's own "
                         "timeout")
        ms = (m.status()["cycles"] - t0) / HZ * 1000.0
        m.run()                 # the sampling loop above ends on an
                                # `advance`, which leaves the emulator
                                # STOPPED - and a stopped guest cannot settle.
                                # It used to settle instantly on a frozen
                                # screen and everything below measured a
                                # machine that was not running.
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
