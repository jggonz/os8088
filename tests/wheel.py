#!/usr/bin/env python3
"""The WHEEL scrolls what a key would reach (SPEC.md 9.5.5, 10.4, 13.18).

    python3 tests/wheel.py [--machine NAME] [-q]

MartyPC, with tools/martypc/patches/15-serial-mouse-wheel.patch: the serial
Microsoft mouse there sends a FOURTH byte - the IntelliMouse (`MZ`) packet -
when the debug server is asked for `wz`, which is the exact stream a real
serial wheel mouse puts on the wire. Nothing else here can: QEMU's msmouse has
no wheel at all (tests/wheelps2.py is the PS/2 half, on QEMU).

TWO LEGS, one per kind of window:

  D  the DISK WINDOW (SPEC.md 13.18.3) - a kernel window, so this is the
     wheel with no package involved. B:/APPS in the list view: two notches
     toward the user move [FS_SCRL] down by exactly 2 x 3 rows, one away
     moves it back 3, and the SELECTION ([FS_SEL]) does not move - the arrow
     keys would have moved it (SPEC.md 22.26), the wheel must not
  N  NOTE PAD (SPEC.md 13.18.2), the worked example of a package's handler
     and the one whose extent is a LOWER BOUND while its count runs: three
     notches put [np_top] at exactly 3 x WHEEL_STEPS = 9 rows, and five back
     clamp it at 0, not at -6 read as 65,530 (SPEC.md 27.7.6.1's trap,
     which the wheel would walk straight into through any other path)

How it goes red (docs/WRITING-TESTS.md 1), CHECKED when the row was
written: with mou_byte's phase-3 `jmp mou_whl_add` made a bare `ret`, both
legs fail naming the word that did not move ("[FS_SCRL] at 0, want 6 ...
NOTHING moved", "[np_top] at 0, want 9"). A MartyPC built WITHOUT patch 15
fails the same way: its debug server ignores the unknown `wz` and sends a
three-byte packet with no motion - so rebuild with `make marty` first.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88geom as geom                                     # noqa: E402
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
from npscroll import Note                                   # noqa: E402

IMG = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
WHEEL_STEPS = 3                 # apps/os88api.inc - lines a notch


def u16(b):
    return b[0] | (b[1] << 8)


def wait(ui, cond, what, guest=6.0):
    try:
        os88marty.until(ui.m, lambda _: cond(), what, guest=guest)
        return True
    except Exception:
        return cond()


def leg_d(ui, say):
    w = ui.path("B:/APPS")
    ui.settle()
    rows = len(ui.listing(w))
    fit = ui._word("fm_fit")
    base = ui._fsblk(w)
    view = ui.m.read(base + geom.FS_VIEW, 1)[0]
    if view != 0:
        return "the Disk window opened in the icon view (FS_VIEW %d); this " \
               "leg is written for the list's three rows a notch" % view
    if rows - fit < 2 * WHEEL_STEPS + 1:
        return "B:/APPS lists %d rows against %d visible - too short to " \
               "scroll 6 and still prove the clamp is not what stopped it" \
               % (rows, fit)
    sel0 = u16(ui.m.read(base + geom.FS_SEL, 2))
    s0 = ui.scroll(w)
    say("Disk window: %d rows, %d fit, scroll %d, sel %d" % (rows, fit, s0, sel0))
    ui.m.wheel(2)
    want = s0 + 2 * WHEEL_STEPS
    wait(ui, lambda: ui.scroll(w) == want, "[FS_SCRL] = %d" % want)
    s1 = ui.scroll(w)
    if s1 != want:
        return "two notches toward the user left [FS_SCRL] at %d, want %d " \
               "(%s)" % (s1, want, "NOTHING moved: no wheel reached the "
                         "window" if s1 == s0 else "the wrong step or sign")
    ui.m.wheel(-1)
    want = s1 - WHEEL_STEPS
    wait(ui, lambda: ui.scroll(w) == want, "[FS_SCRL] = %d" % want)
    if ui.scroll(w) != want:
        return "one notch back left [FS_SCRL] at %d, want %d" \
               % (ui.scroll(w), want)
    sel1 = u16(ui.m.read(base + geom.FS_SEL, 2))
    if sel1 != sel0:
        return "the wheel moved the SELECTION %d -> %d: it must move the " \
               "view and leave the selection (SPEC.md 13.18.3)" % (sel0, sel1)
    say("Disk window: +2 -> %d, -1 -> %d, selection kept" % (s1, want))
    return None


def leg_n(ui, say):
    ui.open_drive("A")
    ui.open("README.TXT")                   # SPEC.md 54's association
    ui.settle()
    np_ = Note(ui.m)
    np_.check()
    t0 = np_.w("np_top")
    if t0 != 0:
        return "Note Pad opened at [np_top] %d, not the top" % t0
    ui.m.wheel(3)
    want = 3 * WHEEL_STEPS
    wait(ui, lambda: np_.w("np_top") == want, "[np_top] = %d" % want)
    t1 = np_.w("np_top")
    if t1 != want:
        return "three notches left [np_top] at %d, want %d (%s)" % (
            t1, want, "NOTHING moved: Note Pad's W_ONWH never ran"
            if t1 == 0 else "the wrong step")
    ui.m.wheel(-5)
    wait(ui, lambda: np_.w("np_top") == 0, "[np_top] = 0")
    t2 = np_.w("np_top")
    if t2 != 0:
        return "five notches back from %d left [np_top] at %d, want 0 - a " \
               "negative target read UNSIGNED (SPEC.md 27.7.6.1)" % (t1, t2)
    say("Note Pad: +3 -> %d, -5 -> 0 (clamped)" % t1)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine",
                    default=os88marty.machine("os8088_5150_cga"))
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()
    say = (lambda s: None) if a.quiet else (lambda s: print("   ", s, flush=True))
    bad = []
    with os88ui.boot(IMG, apps=APPS, machine=a.machine,
                     verbose=not a.quiet) as ui:
        for name, leg in (("D", leg_d), ("N", leg_n)):
            why = leg(ui, say)
            if why:
                bad.append("leg %s: %s" % (name, why))
    for b in bad:
        print("FAIL", b)
    print("wheel:", "ok - the Disk window and Note Pad both scroll three "
          "lines a notch on a serial MZ mouse" if not bad
          else "%d leg(s) FAILED" % len(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
