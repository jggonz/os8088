#!/usr/bin/env python3
"""DOES THE 'SAVE CHANGES?' ALERT ZOOM OPEN? (SPEC.md 11.99.2, 11.99.2.1)

    make && python3 tests/alertanim.py [--machine os8088_5150_herc_gla]

Reported from the field as: the dialog that comes up when you close a dirty
document plays the window-opening animation, and it feels wrong because THE
USER DID NOT ASK FOR THAT WINDOW - they clicked the close box, and a third of
a second of outline arrives in front of the interruption.

SPEC.md 11.99.2's WF_NOANIM was already the answer and already carried the
Standard File dialog and the notice window. What it could not carry was this
one: the alert is PACKAGE code (apps/os88ui.inc), the window record is the
kernel's, and a settable W_FLAGS bit the SDK publishes is a slot's to write.
11.99.2.1 is the slot that closes that, and this row is its A/B.

**THE PAIR IS THE EXPERIMENT AND NEITHER HALF IS ONE ALONE.** A launch MUST
still animate - a zero there means the theme is off, the build is ANIMOFF, or
the breakpoint never armed, and the alert's zero would then be a null rather
than a result. And the alert must actually BE on the glass at the end, or a
dialog that failed to open reads exactly like a dialog that opened quietly.

Paint is the alert that is easiest to raise, as it is in tests/alertbtn.py;
the control under test is os88ui.inc's and is shared by five packages.

os88marty.bp_count is the counter, and its two hardenings were found HERE:
a poll landing inside the driving thread's own advance(frames=) read a state
that is not "running", and a stop was reported twice when a resume had not
landed by the next status(). Both put their spare stop in whichever gesture
was running, which for the B half below is a false FAILURE of the thing under
test. Its docstring is the account.
"""
import argparse
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
        anim = S("wm_anim")
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)

        # A - the CONTROL. A window the user asked for still zooms open.
        n = os88marty.bp_count(m, anim, lambda: (mo.dblclick(x, y),
                                                 m.advance(frames=250),
                                                 m.run()))
        print("   launching PAINT              wm_anim x%d   (SPEC.md 11.99)"
              % n)
        if n == 0:
            fails.append("a LAUNCH did not animate either, so this run cannot "
                         "say anything about the alert - the theme's zoom is "
                         "off (SPEC.md 76.11), the kernel is ANIMOFF, or the "
                         "breakpoint never armed")
        os88marty.settle(m)
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]
        pw = dispcp.win_list(m, S)[-1]

        def boff(nm):
            return ((seg << 4) + dispapps.img_size("paint")
                    + dispapps.bss_off("paint", nm))

        cx0 = int.from_bytes(m.read(boff("pt_cx0"), 2), "little")
        cy0 = int.from_bytes(m.read(boff("pt_cy0"), 2), "little")
        mo.to(cx0 + 30, cy0 + 25)               # ink it, so the close ASKS
        os88marty.settle(m)
        mo._edge(True)
        for _ in range(4):
            m.mouse(dx=8, dy=6, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        wr = dispcp.win_rect(m, S, pw)

        # B - the SUBJECT. The close box the user meant as a close.
        n = os88marty.bp_count(m, anim, lambda: (mo.click(wr[0] + 8, wr[1] + 9),
                                                 m.advance(frames=400),
                                                 m.run()))
        os88marty.settle(m)
        up = [w for w in dispcp.win_list(m, S) if w not in (pw, disk)]
        print("   close box -> the alert       wm_anim x%d   alert up: %s"
              % (n, bool(up)))
        if not up:
            fails.append("no alert came up to measure - the gesture missed, "
                         "or Paint did not think the picture was dirty "
                         "(SPEC.md 42.16); the count above is a null")
        elif n != 0:
            fails.append("the 'Save changes?' alert zoomed open %d time(s): "
                         "os88ui_ask is not calling OSAPI_WM_NOANIM, or the "
                         "flag is not reaching wm_an_ok - SPEC.md 11.99.2.1"
                         % n)

    if fails:
        for f in fails:
            print("alertanim: %s" % f)
        return 1
    print("alertanim: PASS - a launch zooms, the alert the user never asked "
          "for does not")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
