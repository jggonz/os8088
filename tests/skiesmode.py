#!/usr/bin/env python3
"""THE MODE ROW IS HIDDEN WHERE THE DISPLAY HAS ONE (SPEC.md 88.13.11).

    python3 tests/skiesmode.py [--machine ...] [--modes 0|1] [--clobber-hide]

Clear Skies' Settings page has four drop-downs and the last chooses between
the two rasters a display offers - Mode X and CGA320 on a VGA, CGA320 and
88.15's text hack on a CGA. A HERCULES HAS ONE, and the row used to be
greyed there. §47 rule 2 greys a control the machine could use in another
state, and the adapter is not a state: it is fixed for the session, so a
greyed Mode row is a promise the machine can never keep.

It is left off the page entirely now, which is also what makes it SAFE: a
row that is never painted never has OS88UI_DR_DIS written, so §13.14.5's
refusal would not fire for it - but a row outside `cs_nsets`' count is
reached by no walk on the page at all.

  1. on a one-mode display the Mode row is not laid out - no rect;
  2. ...and a press where it would have been opens nothing;
  3. on a display that HAS two, the row is laid out as before, which is what
     stops "hide it" from meaning "delete it".

Both arms are registered, because a change that hid the row everywhere would
pass arm 1 and is exactly what this is guarding against.

--clobber-hide makes `cs_nsets` answer the full count whatever the display -
`dec cx` NOPed out - which is the tree before 88.13.11, and arm 1 goes red.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DR_OPEN = 16
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--modes", type=int, default=0,
                    help="1 where this display offers a second raster")
    ap.add_argument("--clobber-hide", action="store_true",
                    help="cs_nsets stops hiding: arm 1 goes red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        m.advance(frames=40)
        m.run()

        if a.clobber_hide:
            # cs_nsets' `dec cx` - the one instruction that leaves the Mode
            # row off. 49 is a byte, and it is the only one between the call
            # to cs_modechoice and the `mov di, cx` that follows
            lo = mp["cs_nsets"]
            code = m.read(lin + lo, 0x20)
            i = code.find(b"\x49")
            if i < 0:
                sys.exit("skiesmode: cs_nsets does not drop the row the way "
                         "this patch expects")
            m.pause()
            m.write(lin + lo + i, b"\x90")
            m.run()
            print("  (cs_nsets stops hiding the row: this run must fail)")

        ui.raise_window("Clear Skies")
        ui.menu_pick("Flight", "Settings")
        m.advance(frames=60)
        m.run()

        rec = mp["cs_drmode"]
        rect = [int.from_bytes(m.readseg(seg, rec + 2 * i, 2), "little")
                for i in range(4)]
        laid = rect[2] > rect[0]
        if a.modes:
            check(laid,
                  "3. this display HAS two rasters, so the Mode row is laid "
                  "out (%s)" % rect)
        else:
            check(not laid,
                  "1. one raster, so the Mode row is not laid out (%s)" % rect)
            # ...and nothing opens where it used to be. The other three rows
            # give its position away: Mode sat under Size, in the right column
            sz = mp["cs_drsize"]
            szr = [int.from_bytes(m.readseg(seg, sz + 2 * i, 2), "little")
                   for i in range(4)]
            cx = (szr[0] + szr[2]) // 2
            cy = szr[3] + (szr[3] - szr[1]) + 20      # a row further down
            ui.mo.to(cx, cy)
            if ui.mo.where()[2] & 1:
                ui.mo._edge(False)
            ui.mo._edge(True)
            m.advance(frames=30)
            m.run()
            opened = [n for n in ("cs_drmode", "cs_drbld", "cs_drlod",
                                  "cs_drsize")
                      if m.readseg(seg, mp[n] + DR_OPEN, 1)[0] != 0]
            ui.mo._edge(False)
            m.advance(frames=20)
            m.run()
            check(not opened,
                  "2. ...and a press at (%d,%d), where it used to be, opens "
                  "nothing (%s)" % (cx, cy, opened or "nothing"))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
