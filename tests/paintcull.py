#!/usr/bin/env python3
"""DOES THE CULL COST PAINT ITS FOUR PLANES? (SPEC.md 5.4.3.3, 42.13.1)

    make && python3 tests/paintcull.py [--machine os8088_xt_vga]

On a colour adapter Paint stores its canvas as four planes and repaints it
through `gfx_blitp` - 164 ms for a picture that costs 1,161 through
`gfx_blit4` (SPEC.md 42.13). `pt_topacked` converts it back to nibbles on any
refusal and does NOT convert back until the adapter changes, because every
refusal `gfx_blitp` has is a fact about the machine: a mono card, an x off the
byte grid, a straddled seam, a nested display hook, a `kern_small` kernel.

SPEC.md 11.3.3's cull put a sixth one under that which is a fact about the
CALL. It arms a clip region around one `W_PAINT` inside `wm_dmg_wins`, and an
armed region is a refusal - so an ordinary damage repaint told Paint its
machine could not hold a planar canvas, and Paint believed it for the rest of
the session.

THE FLOW IS THE OWNER'S: open Paint, draw one stroke, double-click the title
bar to maximize, double-click again to restore. The stroke matters - without
ink SPEC.md 42.15 answers the repaint with one `gfx_fill` and never reaches
the blit at all, so a version of this test that does not draw passes against
the defect.

TWO ASSERTIONS:

  PLANAR   [pt_planar] is 1 at every step. It went to 0 at the restore.

  WHY      if it did go to 0, say which of `gfx_blitp`'s guards fired, by
           reading them at `pt_topacked`. Every geometric one passed when this
           was found - `vid_mono` 0, `vid_ndisp` 1, `gfx_dnest` 0, the block on
           the byte grid - and only `wm_clip_n` was set, which is the whole
           finding and not a detail.

VGA, because a 1bpp adapter stores the canvas packed by design and has nothing
to lose here.
"""
import argparse
import os
import sys
import threading
import time

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


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name, w=2):
    return int.from_bytes(m.read(_boff(seg, name), w), "little")


def _kern(m, name, w=2):
    return int.from_bytes(m.read(S(name), w), "little")


def _open_paint(m, mo):
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


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails, steps, why = [], [], None
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        _open_paint(m, mo)
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]
        pw = dispcp.win_list(m, S)[-1]
        pm = dispapps._map("paint")
        base = seg << 4

        if _bss(m, seg, "pt_mono", 1):
            print("paintcull: SKIP - %s is a 1bpp machine, where 42.13 stores"
                  " the canvas packed by design" % a.machine)
            return 0

        def inked():
            """Is anything inked? SPEC.md 42.18's TABLE, not the flag.

            [pt_iall] was one bit and any pixel set it; the band table replaced
            it, so the same question is now 'does any band hold a range', and
            an empty one is x1=1, x2=0 (pt_iclear).
            """
            nband = (_bss(m, seg, "pt_ch") >> 4) + 1     # PT_IBSH is 4, and
            x1 = m.read(_boff(seg, "pt_ibx1"), nband * 2)  # a band past the
            x2 = m.read(_boff(seg, "pt_ibx2"), nband * 2)  # canvas is empty
            for i in range(nband):
                a = int.from_bytes(x1[i * 2:i * 2 + 2], "little")
                b = int.from_bytes(x2[i * 2:i * 2 + 2], "little")
                if a <= b:
                    return 1
            return 0

        def note(what):
            steps.append((what, _bss(m, seg, "pt_planar", 1),
                          _bss(m, seg, "pt_cx0"), inked()))

        note("opened")
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        mo.to(cx0 + 40, cy0 + 30)                   # ONE STROKE, and 42.15 is
        os88marty.settle(m)                         # why it has to be here
        mo._edge(True)
        for _ in range(6):
            m.mouse(dx=8, dy=8, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        note("after a stroke")

        wr = dispcp.win_rect(m, S, pw)
        mo.dblclick(wr[0] + 60, wr[1] + 9)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        note("maximized")

        # THE RESTORE, with pt_topacked watched. The breakpoint is pumped from
        # this thread while a daemon does the click: os88mouse proves each
        # button edge by polling mouse_btn, and a guest stopped at a
        # breakpoint never advances far enough to answer.
        big = dispcp.win_rect(m, S, pw)
        m.bp_exec(base + pm["pt_topacked"])
        done = []
        threading.Thread(target=lambda: (
            time.sleep(1.0),
            mo.dblclick(big[0] + 60, big[1] + 9),
            done.append(1)), daemon=True).start()
        for _ in range(3000):
            if done and m.status()["state"] == "running":
                break
            if not m.wait_stop(limit=10.0):
                continue
            r = m.regs()
            if (r["cs"] << 4) + r["ip"] == base + pm["pt_topacked"] and not why:
                sx = _bss(m, seg, "pt_bx0") + _bss(m, seg, "pt_cx0")
                why = dict(wm_clip_n=_kern(m, "wm_clip_n"),
                           vid_mono=_kern(m, "vid_mono", 1),
                           vid_ndisp=_kern(m, "vid_ndisp", 1),
                           gfx_dnest=_kern(m, "gfx_dnest", 1),
                           screen_x=sx, x_and_7=sx & 7,
                           w=_bss(m, seg, "pt_bwid"))
            m.run()
        m.breakpoints([])
        m.run()
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        note("restored")

    for (what, planar, cx0, iall) in steps:
        print("   %-16s pt_planar=%d  canvas x=%d (x&7=%d)  inked=%d"
              % (what, planar, cx0, cx0 & 7, iall))
    if why:
        print("   pt_topacked ran. gfx_blitp's guards at that moment:")
        for k in ("wm_clip_n", "vid_mono", "vid_ndisp", "gfx_dnest",
                  "screen_x", "x_and_7", "w"):
            print("      %-10s = %d" % (k, why[k]))
        if (why["wm_clip_n"] and not why["vid_mono"] and why["vid_ndisp"] <= 1
                and not why["gfx_dnest"] and not why["x_and_7"]):
            print("      -> ONLY the clip region fired, and SPEC.md 5.4.3.3"
                  " says the cull is not one")
    for (what, planar, _cx0, _iall) in steps:
        if not planar:
            fails.append("the canvas stopped being four planes at %r - "
                         "SPEC.md 5.4.3.3" % what)
    if steps and not steps[1][3]:
        fails.append("SETUP: the stroke did not ink the canvas, so 42.15"
                     " answered every repaint with a fill and the blit was"
                     " never reached")
    if len(steps) == 4 and steps[3][2] == steps[2][2]:
        fails.append("SETUP: the second title double-click did not restore -"
                     " the canvas is still at the maximized origin")
    if fails:
        for f in fails:
            print("paintcull: %s" % f)
        return 1
    print("paintcull: PASS - the canvas is four planes through a maximize and"
          " a restore, and pt_topacked never ran")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
