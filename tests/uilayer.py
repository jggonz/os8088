#!/usr/bin/env python3
"""Does tools/os88ui.py do what it says - and is it cheaper than settling?

    make && python3 tests/uilayer.py

TWO QUESTIONS, and the second is why this row exists at all.

**Does every verb work, and does every failure NAME ITSELF?** The layer's
whole claim is that a mis-aimed click raises where it happened instead of
surfacing twenty steps later as the feature under test. That claim is only
worth anything if the failure paths are exercised, so half of this file asks
for things that are not there - a window that is not open, a drive with no
zone, a menu item that does not exist, a DISABLED item, a file that is not in
the folder - and requires a UIError naming what the guest actually holds.
A verb that TIMED OUT instead would pass a weaker test and be useless in the
field.

**And is confirming cheaper than settling?** It ought to be, and the reason is
worth stating because it is the opposite of what "add a check" usually costs:

  * `os88marty.settle` waits for two identical frames a second apart, and
    reads the framebuffer over the socket to compare them. It cannot know what
    it is waiting FOR, so it waits for the whole screen to go quiet - and then
    waits `quiet` more seconds to be sure.
  * reading `wm_wins` to see whether the window that was supposed to open has
    opened is a 408-byte read, it answers the actual question, and it returns
    the instant the answer is yes.

So arm B is not "arm A plus a check"; it is arm A with the check REPLACING the
wait. This row runs the same navigation both ways on one machine and prints
host seconds and GUEST cycles for each. It asserts the state reached is
identical and that B is not slower; the sizes are reported rather than
asserted, because a ratio is a property of the box.

WHAT IT DOES NOT COVER. Anything needing a second display, a driver, or a
package that is not on the shipped apps floppy. The layer's own geometry comes
from os88geom, which `tests/unit/t_mirror.py` already checks against the
kernel source in the fast tier - so a constant going stale is caught there,
not here.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

import os88marty                                                 # noqa: E402
import os88ui                                                    # noqa: E402
from os88mouse import Mouse                                      # noqa: E402
import os88geom as geom                                          # noqa: E402

IMG = "build/os8088-360.img"
APPS = "build/apps360.img"

fails = []


def check(ok, what):
    print("   %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        fails.append(what)


def expect_uierror(what, fn, must_say=None):
    """A verb that cannot do what it was asked must RAISE, and the message
    must carry the state that explains it."""
    try:
        fn()
    except os88ui.UIError as e:
        msg = str(e)
        if must_say and must_say.lower() not in msg.lower():
            check(False, "%s raised, but did not mention %r: %s"
                  % (what, must_say, msg))
            return
        print("   ok   %s -> %s" % (what, msg.split("\n")[0][:110]))
        return
    except Exception as e:                       # a timeout, a TypeError, an
        check(False, "%s raised %s, not a UIError: %s"     # AttributeError -
              % (what, type(e).__name__, e))               # all of them are
        return                                             # the failure this
    check(False, "%s did not raise at all" % what)          # row is about


# =============================================================================
def arm_old(m, mo, ui):
    """The way 175 scripts do it: a click, then settle, and no confirmation.

    Written out here rather than imported so the comparison is honest - this
    IS the shape in the tree, coordinates derived from os88geom (which is
    already the good half) and every wait a settle.
    """
    t0, c0 = time.time(), m.status()["cycles"]
    x, y = geom.drive_pt(m, "B")
    mo.dblclick(x, y, settle=0)
    os88marty.settle(m)
    win = [w for w in geom.windows(m) if w.visible][-1]
    row = [r[0] for r in ui.listing()].index("APPS")
    rx, ry = ui.row_xy(win, row)
    mo.dblclick(rx, ry, settle=0)
    os88marty.settle(m)
    row = [r[0] for r in ui.listing()].index("CALC.O88")
    rx, ry = ui.row_xy(win, row)
    mo.dblclick(rx, ry, settle=0)
    os88marty.settle(m)
    return (time.time() - t0, m.status()["cycles"] - c0,
            sorted(w.title for w in geom.windows(m)))


def arm_new(ui):
    """The same navigation through the layer."""
    m = ui.m
    t0, c0 = time.time(), m.status()["cycles"]
    ui.open_drive("B")
    ui.open("APPS")
    ui.open("CALC.O88")
    return (time.time() - t0, m.status()["cycles"] - c0,
            sorted(w.title for w in ui.windows()))


def clear(ui):
    """Close every window, so the two arms start from the same desktop."""
    for _ in range(12):
        wins = ui.windows()
        if not wins:
            return
        ui.close(wins[-1])
    raise RuntimeError("could not get back to a bare desktop")


def main():
    for p in (IMG, APPS):
        if not os.path.exists(p):
            print("uilayer: %s is missing - run `make` first" % p)
            return 1

    with os88ui.boot(IMG, apps=APPS,
                     machine="os8088_5150_cga", verbose=False) as ui:
        m, mo = ui.m, ui.mo

        # --- 1. the verbs -----------------------------------------------
        print("\n=== every verb, and what it confirmed ===\n")
        d = ui.open_drive("B")
        check(d.title.strip() != "", "open_drive B: -> a window titled %r"
              % d.title)
        names = [r[0] for r in ui.listing()]
        check("APPS" in names, "listing reads the root: %r" % names)

        ui.open("APPS")
        names = [r[0] for r in ui.listing()]
        check(".." in names and "CALC.O88" in names,
              "open('APPS') navigated in place: %d entries" % len(names))

        w = ui.open("CALC.O88")
        check(w.title == "Calculator",
              "open('CALC.O88') -> the package's window %r" % w.title)

        # a drag lands where SPEC.md 11.94 SNAPS it, not where it was asked
        was = (w.x, w.y)
        w = ui.drag_window(w, 20, 12)
        check((w.x, w.y) == (geom.snapx(was[0] + 20), was[1] + 12),
              "drag +20+12 from %r -> %r (11.94 snaps x)" % (was, (w.x, w.y)))

        # THE COVERED TITLE BAR, which is the case a hand-written raise gets
        # wrong: put the Disk window on top of the Calculator, then ask for
        # the Calculator back. Its bar is behind, so the aim has to move.
        ui.raise_window("APPS")
        check(ui.front().title == "APPS", "raise_window('APPS') -> front")
        pt, how = ui._raise_point(ui.window("Calculator"))
        ui.raise_window("Calculator")
        check(ui.front().title == "Calculator",
              "raise_window('Calculator') from behind -> front, via its %s at "
              "%r" % (how, pt))

        # a menu picked by NAME off the live bar
        bar = [c[0] for c in ui.menus()]
        check("Calc" in bar, "menus() reads the package's own bar: %r" % bar)
        ui.menu_pick("Calc", "Close")
        check(all(x.title != "Calculator" for x in ui.windows()),
              "menu_pick('Calc', 'Close') closed it: %r" % ui.titles())

        ui.raise_window("APPS")
        ui.menu_pick("Apple", "Task Manager")
        tm = ui.wait_window("Task")
        check(tm.title.startswith("Task"),
              "menu_pick('Apple','Task Manager') -> %r" % tm.title)
        ui.close(tm)
        check(all(not x.title.startswith("Task") for x in ui.windows()),
              "close(Task Manager) -> %r" % ui.titles())

        # THE ACTING DISK WINDOW IS NOT THE FRONT WINDOW, which is what five
        # scripts in this tree carry their own `raise_win` for. Open a folder
        # with the Calculator in front: `fm_vp_set` never ran for the
        # Calculator, so [fm_vinst] still names the Disk window - and a verb
        # computing a row off `front()` would aim inside the Calculator.
        w = ui.open("CALC.O88")             # (menu_pick closed the first one)
        ui.raise_window(w)
        check(ui.front().title == "Calculator"
              and ui.disk_window().title == "APPS",
              "front is %r while the acting Disk window is %r"
              % (ui.front().title, ui.disk_window().title))
        back = ui.open("..")
        check(back.title != "Calculator" and "APPS" in
              [r[0] for r in ui.listing()],
              "open('..') with a package in front navigated the DISK window "
              "-> %r" % [r[0] for r in ui.listing()])
        ui.open("APPS")
        ui.close(ui.window("Calculator"))

        # the toast strip: TWO facts, and a script reading only the buffer
        # cannot tell "the machine is saying X" from "it said X a minute ago"
        txt, on = ui.toast()
        check(isinstance(txt, str) and isinstance(on, bool),
              "toast() reads both halves: %r, showing=%s" % (txt, on))

        # --- 2. the failures --------------------------------------------
        print("\n=== and what happens when it cannot ===\n")
        expect_uierror("window('Nope')", lambda: ui.window("Nope"),
                       "what is open")
        expect_uierror("open_drive('Z')", lambda: ui.open_drive("Z"),
                       "no desktop zone")
        expect_uierror("open('NOSUCH.O88')", lambda: ui.open("NOSUCH.O88"),
                       "not in this folder")
        expect_uierror("menu_pick('Nonesuch','x')",
                       lambda: ui.menu_pick("Nonesuch", "x"), "the bar is")
        expect_uierror("menu_pick('Apple','Nonesuch')",
                       lambda: ui.menu_pick("Apple", "Nonesuch"), "it holds")
        # ...and a SEPARATOR, which carries SPEC.md 12's MENU_DIS prefix:
        # menu_hover will not stop on it, so a drag aimed there releases over
        # a neighbour and runs a command nobody asked for.
        sep = [t for t, e in dict(
            (c[0], c[3]) for c in ui.menus())["Apple"] if not e]
        if sep:
            expect_uierror("menu_pick('Apple', a disabled item)",
                           lambda: ui.menu_pick("Apple", sep[0]), "DISABLED")
        else:
            check(False, "the Apple menu has no disabled item to try")
        # ...and a wait that can never come true has to end AS A UIError with
        # the state in it, not as a bare timeout - that is the difference
        # between "the toast never said X" and "something took too long".
        expect_uierror("wait_toast('nothing ever says this')",
                       lambda: ui.wait_toast("nothing ever says this",
                                             limit=4.0),
                       "toast_buf")

        clear(ui)

        # --- 3. settle vs confirm ---------------------------------------
        print("\n=== the same navigation, both ways ===\n")
        ah, ac, astate = arm_old(m, mo, ui)
        clear(ui)
        bh, bc, bstate = arm_new(ui)
        clear(ui)
        print("   settle : %6.1fs host, %11d guest cycles (%.1f guest s)"
              % (ah, ac, ac / os88marty.GUEST_HZ))
        print("   confirm: %6.1fs host, %11d guest cycles (%.1f guest s)"
              % (bh, bc, bc / os88marty.GUEST_HZ))
        print("   ratio  :  %.2fx host, %.2fx guest"
              % (bh / ah if ah else 0, bc / ac if ac else 0))
        check(astate == bstate,
              "both arms reach the same desktop: %r" % (bstate,))
        # NOT a tight bound. What is being refused is a REGRESSION - a layer
        # that confirms by settling anyway would land at ~1.0 and a slower one
        # above it. The win measured here is ~0.4x, so 0.9 has margin for a
        # loaded box without letting that through.
        check(bh <= ah * 0.9,
              "confirming is not slower than settling (%.2fx host)"
              % (bh / ah if ah else 0))

    print("")
    if fails:
        print("uilayer: %d FAILED" % len(fails))
        for f in fails:
            print("  FAIL:", f)
        return 1
    print("uilayer: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
