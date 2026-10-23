#!/usr/bin/env python3
"""SPEC.md 110.3 / 11.103.4: a widget's DELETE with an application in front
- it asks no window any more, so it changes NOTHING about the stacking or
the bar, and the widget stays on the desk.

    make && python3 tests/deskmodal.py

The SHIPPED 360KB system disk, a calendar holding one appointment today,
MartyPC's VGA XT, and a Disk window in FRONT the whole time. Every assertion
is the kernel's own state: wm_zord (bottom first), [menu_win], and the
kernel's wm_top answered by tools/os88geom.py's mirror of it.

  A  the widget is at wm_zord[0] - under the Disk window - and the Disk
     window is wm_top.
  B  a press on the widget's appointment selects it and hands the widget
     the bar; Del arms Delete (`Delete again to confirm`, a toast) and
     opens NO window: wm_zord is what it was, the widget still owns the
     bar and is still wm_zord[0] under the Disk window.
  C  Del again deletes it: still no window, the widget still owns the bar
     (Esc gives it back, deskcaledit H), and nothing moved in wm_zord.

It was this row's subject that Del put up an `os88ui_ask` question which
landed ABOVE every application and took the bar; the size pass
(docs/plans/completed/DESKCAL-SIZE-PLAN.md) replaced that question with pressing
twice, and nothing in the shipped tree creates a window from a widget now.

**BREAK IT ON PURPOSE**: put an os88ui_ask back in dc_askdel and B fails on
the window count and wm_zord; make the second Del give the bar away and C
fails.
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
        # PACKED, as the package writes it (SPEC.md 110.5.3): the load's
        # dc_spread is what puts Dentist on line 0
        img = disk_with(tmp, "modal.img", os88stik.dat(os88stik.deskcal_pack(
            os88stik.deskcal([(TODAY, 600, "Dentist")]))))
        with os88ui.boot(img, machine="os8088_xt_vga") as ui:
            c = Cal(ui, tmp, check=check)
            disk = ui.open_drive("A")
            disk = ui.move_window(disk, 20, 150)    # clear of the widget's
                                                    # cards: it is UNDER it

            print("A: the widget on the desk, the Disk window in front")
            z = zord(ui)
            check(z[:1] == [c.slot], "the widget is wm_zord[0] (%r)" % (z,))
            check(ui.front().i == disk.i, "the Disk window is wm_top")

            print("B: Del on the widget's appointment: armed, no window")
            c.front()
            c.press(c.line(0))
            check(c.bss(os88stik.DESKCAL_BSS_SELK)[0] == 1,
                  "the appointment is selected")
            z0 = zord(ui)
            rec = ui._wrec(c.win())
            check(ui._word("menu_win") == rec, "the widget owns the bar "
                  "(menu_win %#06x)" % ui._word("menu_win"))
            ui.m.key("Delete")
            txt = ui.wait_toast(says="again")
            check(txt == "Delete again to confirm", "Del says %r" % txt)
            check(len([w for w in ui.windows() if w.title == TITLE]) == 1,
                  "...and opens no window")
            check(zord(ui) == z0 and z0[:1] == [c.slot]
                  and z0.index(disk.i) > 0, "wm_zord is as it was, the "
                  "widget at [0] under the Disk window (%r)" % (zord(ui),))
            check(ui._word("menu_win") == rec, "the widget still owns the "
                  "bar (menu_win %#06x)" % ui._word("menu_win"))

            print("C: Del again: deleted, and still nothing moved")
            ui.m.key("Delete")
            got = appts_of(c.saved("the delete's save"))
            check(got == [], "the second Del deleted the appointment (%r)"
                  % (got,))
            check(zord(ui) == z0, "wm_zord is as it was (%r)" % (zord(ui),))
            check(ui._word("menu_win") == rec, "the widget still owns the "
                  "bar (menu_win %#06x)" % ui._word("menu_win"))
            check(ui.front().i == disk.i, "the Disk window is still wm_top")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskmodal: %s" % ("PASS" if not fails else
                             "FAIL (%d): %s" % (len(fails),
                                                "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
