#!/usr/bin/env python3
"""A DROP-DOWN DRAWN DISABLED TAKES NO PRESS (SPEC.md 13.14.5), on MartyPC.

    python3 tests/uidrdis.py [--clobber-dis]

`OS88UI_DIS` used to be a paint-time argument and nothing else: `os88ui_drop`
took it in DI and greyed the box, while `os88ui_drpress` took BX/CX/DX and
could not know. A press on a greyed control therefore ran the whole open
path - OPEN to 1, the clip armed, the pixels banked, the list drawn - which
is the field's "it still drops down, and then is not disabled".

The state is the CONTROL's now (`OS88UI_DR_DIS`, written by the painter), and
this row drives the library through the one package that greys one. It is
NOT a test of Clear Skies' Mode row: 88.13.11 hides that row on the display
where it has no meaning, so the row picks a LIVE drop-down and greys it by
hand, which is the library's contract and nobody else's.

  1. a live drop-down opens on a press - the control works at all;
  2. the same control, with OS88UI_DR_DIS set, does NOT open;
  3. ...and does not SPEND the press either, so the app may hand the point to
     whatever is underneath. That is the half a "return early" fix gets
     wrong, and the reason AL comes back 0FFh and not 0.

--clobber-dis is the red run (docs/WRITING-TESTS.md 1): it NOPs the two-byte
`je .live` that guards the refusal, so a disabled control opens again and
checks 2 and 3 go red.
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
DR_RECT, DR_OPEN, DR_DIS = 0, 16, 24
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
    ap.add_argument("--clobber-dis", action="store_true",
                    help="the refusal is patched out: checks 2 and 3 go red")
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

        def settings():
            ui.raise_window("Clear Skies")
            ui.menu_pick("Flight", "Settings")
            m.advance(frames=60)
            m.run()

        settings()

        # A LIVE control, and not the Mode row: 88.13.11 hides that one where
        # it has no meaning, and this row is about the LIBRARY
        rec = mp["cs_drbld"]

        def w(o):
            return int.from_bytes(m.readseg(seg, rec + o, 2), "little")

        def byte(o):
            return m.readseg(seg, rec + o, 1)[0]

        rect = [w(2 * i) for i in range(4)]
        check(any(rect), "the Buildings drop-down is laid out (%s)" % rect)
        cx, cy = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2

        def press(x, y):
            """The PRESS alone, held. A drop-down opens on the press and
            picks (or closes) on the RELEASE, so a click does both and reads
            OPEN=0 either way - which is check 2 passing for the wrong
            reason, and is how the first draft of this row was green on a
            build with the fix patched out."""
            ui.mo.to(x, y)
            if ui.mo.where()[2] & 1:
                ui.mo._edge(False)
            ui.mo._edge(True)
            m.advance(frames=30)
            m.run()

        def release(x, y):
            ui.mo.to(x, y)
            ui.mo._edge(False)
            m.advance(frames=30)
            m.run()

        def shut():
            """Take the list down without picking. The list slides UP over
            its own box (13.14.2), so a release where the press landed is a
            release over an ITEM - it picks, and leaves OPEN set. Releasing
            well outside is the only way back to a closed control, and check
            2 is meaningless without it: the first draft read `OPEN=1, was 1`
            and was measuring a list that never shut."""
            release(rect[0] - 40 if rect[0] > 60 else rect[2] + 40, rect[1])
            m.advance(frames=30)
            m.run()

        press(cx, cy)
        check(byte(DR_OPEN) != 0,
              "1. a LIVE drop-down opens on a press (OPEN=%d)" % byte(DR_OPEN))
        shut()

        # --- 2/3: THE SAME CONTROL, GREYED BY THE APP -----------------------
        # OS88UI_DR_DIS is DERIVED - os88ui_drop rewrites it from the
        # painter's DI on every repaint - so poking it from here disables
        # nothing, and a row that tried read PASS for check 1 and FAIL for
        # check 2 on a correct build. The app has to be made to paint it
        # greyed, so its own predicate is retargeted: `cmp bx, cs_drmode`
        # becomes `cmp bx, cs_drbld`, one immediate, one site in the image.
        # On a Hercules cs_modechoice then greys BUILDINGS, which is exactly
        # what it does to Mode on every other display.
        code = m.read(lin, 0x8000)
        pat = b"\x81\xFB" + mp["cs_drmode"].to_bytes(2, "little")
        i = code.find(pat)
        if i < 0 or code.find(pat, i + 1) >= 0:
            sys.exit("uidrdis: the app does not choose the greyed row the way "
                     "this patch expects (%d matches)" % code.count(pat))
        m.pause()
        m.write(lin + i + 2, mp["cs_drbld"].to_bytes(2, "little"))
        # ...and the control put back CLOSED. Poking OPEN/SEG/KB is a
        # precondition and not the subject: it is what a control that has
        # never been pressed looks like, and DIS - the thing under test - is
        # written by the PAINTER below, not from here.
        m.write(lin + rec + DR_OPEN, b"\x00")
        m.write(lin + rec + 18, b"\x00\x00\x00\x00")   # SEG, KB
        if a.clobber_dis:
            # ...and the library's refusal taken back out: 3E 80 7E 18 00 /
            # 74 xx, the guard this row is about
            lo = mp["os88ui_drpress"]
            c2 = m.read(lin + lo, 0x40)
            g = c2.find(b"\x3E\x80\x7E\x18\x00\x74")
            if g < 0:
                sys.exit("uidrdis: os88ui_drpress does not guard the way this "
                         "patch expects")
            # `je .live` -> `jmp .live`: the refusal is never taken, which
            # is the tree before 13.14.5. NOPing the `je` instead makes it
            # ALWAYS refuse - every control dead - and that passes checks 2
            # and 3 for the exact opposite reason, which is how this arm
            # first came back green
            m.write(lin + lo + g + 5, b"\xEB")
            print("  (the refusal is patched out: this run must fail)")
        m.run()
        ui.menu_pick("Flight", "Instructions")  # off the page and back, so the
        m.advance(frames=40)                    # painter runs with the new
        m.run()                                 # predicate
        settings()
        check(byte(DR_DIS) != 0,
              "the app now paints Buildings DISABLED (DIS=%d)" % byte(DR_DIS))
        check(byte(DR_OPEN) == 0,
              "...and it starts CLOSED, or check 2 measures nothing (OPEN=%d)"
              % byte(DR_OPEN))
        before = byte(DR_OPEN)
        press(cx, cy)
        check(byte(DR_OPEN) == 0,
              "2. ...and a DISABLED one does not (OPEN=%d, was %d)"
              % (byte(DR_OPEN), before))
        # the press must reach the page: the app's own dispatcher would have
        # turned nothing, so what proves it is that no OTHER control took it
        others = [n for n in ("cs_drlod", "cs_drsize")
                  if m.readseg(seg, mp[n] + DR_OPEN, 1)[0] != 0]
        check(not others,
              "3. ...and the press is not spent on anything else (%s)"
              % (others or "nothing opened"))
        shut()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
