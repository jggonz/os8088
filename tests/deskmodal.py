#!/usr/bin/env python3
"""SPEC.md 110.3 / 11.103.4: a widget's MODAL QUESTION lands above every
application and takes the bar - and the widget stays on the desk.

    make && python3 tests/deskmodal.py

The SHIPPED 360KB system disk, a calendar holding one appointment today,
MartyPC's VGA XT, and a Disk window in FRONT the whole time. Every assertion
is the kernel's own state: wm_zord (bottom first), [menu_win], and the
kernel's wm_top answered by tools/os88geom.py's mirror of it.

  A  the widget is at wm_zord[0] - under the Disk window - and the Disk
     window is wm_top.
  B  a press on the widget's appointment selects it and hands the widget
     the bar; Del puts `Delete this appointment?` up (os88ui_ask, an
     ordinary window): it is the TOP of wm_zord and OWNS THE BAR, the
     widget is still wm_zord[0], and the Disk window is still above the
     widget.
  C  Enter (Yes, the default) answers it: the question is gone, the
     appointment is deleted, and the bar goes to wm_top - the Disk window,
     never the widget.

**BREAK IT ON PURPOSE**: create the question with the widget as its window
manager parent so it lands at the desk (z 0..) and B fails on the top of
wm_zord; give the bar back to the widget at the answer and C fails.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88stik    # noqa: E402
from deskcaledit import Cal, disk_with, TITLE, TODAY, appts_of   # noqa: E402

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def zord(ui):
    m = ui.m
    return list(m.read(ui._S("wm_zord"), m.read(ui._S("wm_zn"), 1)[0]))


def main():
    tmp = tempfile.mkdtemp(prefix="deskmodal-")
    try:
        img = disk_with(tmp, "modal.img", os88stik.dat(os88stik.deskcal(
            [(TODAY, 600, "Dentist")])))
        with os88ui.boot(img, machine="os8088_xt_vga") as ui:
            c = Cal(ui, tmp, check=check)
            disk = ui.open_drive("A")
            disk = ui.move_window(disk, 20, 150)    # clear of the widget's
                                                    # cards: it is UNDER it

            print("A: the widget on the desk, the Disk window in front")
            z = zord(ui)
            check(z[:1] == [c.slot], "the widget is wm_zord[0] (%r)" % (z,))
            check(ui.front().i == disk.i, "the Disk window is wm_top")

            print("B: Del on the widget's appointment: the question")
            c.front()
            c.press(c.line(0))
            check(c.bss(os88stik.DESKCAL_BSS_SELK)[0] == 1,
                  "the appointment is selected")
            c.key("Delete")
            ui._wait(lambda: len([w for w in ui.windows()
                                  if w.title == TITLE]) == 2,
                     "the Delete question", 30)
            q = [w for w in ui.windows() if w.title == TITLE
                 and w.i != c.slot][0]
            z = zord(ui)
            check(z[-1:] == [q.i], "the question is the TOP of wm_zord (%r, "
                  "it is slot %d)" % (z, q.i))
            check(ui._word("menu_win") == ui._wrec(q),
                  "...and owns the bar (menu_win %#06x)" % ui._word("menu_win"))
            check(z[:1] == [c.slot] and z.index(disk.i) > 0,
                  "the widget is still wm_zord[0], under the Disk window "
                  "(%r)" % (z,))

            print("C: answered: the bar to wm_top, never to the widget")
            ui.m.key("Enter")
            ui._wait(lambda: len([w for w in ui.windows()
                                  if w.title == TITLE]) == 1,
                     "the question to be answered", 30)
            os88marty.pace(ui.m, 1.0)
            check(ui._word("menu_win") == ui._wrec(disk),
                  "the bar went to the Disk window, wm_top (menu_win %#06x)"
                  % ui._word("menu_win"))
            check(zord(ui)[:1] == [c.slot], "the widget is still at the desk")
            got = appts_of(c.saved("the delete's save"))
            check(got == [], "Yes deleted the appointment (%r)" % (got,))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskmodal: %s" % ("PASS" if not fails else
                             "FAIL (%d): %s" % (len(fails),
                                                "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
