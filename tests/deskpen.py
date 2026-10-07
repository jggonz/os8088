#!/usr/bin/env python3
"""The gfx_blit1 PEN is scoped to a callback, not to a lock hold (SPEC.md 5.4.2.2.2)

    make && python3 tests/deskpen.py

The pen dies with the gfx lock, and a lock hold is wider than one caller: a
repaint pass calls several packages' paints inside one, and a drag holds the
lock from press to release. So a package that set a pen and returned handed it
to everything drawn after it in the hold - another package's default-pen text,
and the desktop's drive cells, which desk_draw_zone emits with gfx_blit1. VGA
only: on one plane the pen is not read.

Two legs, each a pen POKED into the hold the way a package would leave it
(CBLUE ink on CBLACK paper - a set bit that should be white comes out with no
red in it, which is the channel tools/deskclip.verify compares):

  desk     at desk_draw_zone's entry during a zoomed window's restore; the
           screen must match a whole repaint.
  callback at wm_pkgcall's entry before a package's W_ONKEY; at the package's
           own dispatcher the pen must already be the resting one, 0F00.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), measured: without the bank
in desk_draw_zone the desk leg reads 1,596 pixels differing, every one in the
two drive cells; without the bank in wm_pkgcall the callback leg reads the
poked 0100 at the dispatcher.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88ui               # noqa: E402
import deskclip as dc       # noqa: E402
import os88geom as geom     # noqa: E402

BDIR = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
MACHINE = "os8088_xt_vga"
POKE = bytes([1, 0])        # ink CBLUE, paper CBLACK
REST = "0f00"               # ink CWHITE, paper CBLACK


def boot():
    return os88ui.boot(os.path.join(BDIR, "os8088-360.img"),
                       apps=os.path.join(BDIR, "apps360.img"),
                       machine=MACHINE)


def leg_desk():
    with boot() as ui:
        m = ui.m
        ui.open_drive("A")
        w = ui.open_drive("B")
        w = ui.move_window(w, 300, 120)
        ui.mo.dblclick(w.x + 40, w.y + 5)       # zoom over the cells...
        ui.settle()
        w = ui._refresh(w)
        m.bp_exec("desk_draw_zone")
        ui.mo.dblclick(w.x + 40, w.y + 5)       # ...and the restore redraws
        if m.wait_stop(30) is None:             # them
            print("   FAIL  desk_draw_zone was never reached")
            return 1
        m.write(m.sym("gfx_b1ink"), POKE)
        m.bp_exec()
        m.run()
        ui.settle()
        bad = dc.verify(ui)
        print("   %s  desk: cells drawn under a leftover pen match a whole "
              "repaint" % ("ok " if not bad else "FAIL"))
        return 1 if bad else 0


def leg_callback():
    with boot() as ui:
        m = ui.m
        calc = ui.path("B:/APPS/CALC.O88")
        rec = m.read(m.sym("wm_wins") + calc.i * geom.WIN_SIZE, geom.WIN_SIZE)
        pseg = rec[geom.W_SEG] | (rec[geom.W_SEG + 1] << 8)
        pen = m.sym("gfx_b1ink")
        m.bp_exec("wm_pkgcall")
        m.key("Digit1")                         # W_ONKEY, through wm_pkgcall
        if m.wait_stop(30) is None:
            print("   FAIL  wm_pkgcall was never reached")
            return 1
        m.write(pen, POKE)
        m.bp_exec()
        at = None
        for _ in range(400):                    # step to the package's own
            m.step(1)                           # dispatcher: the first
            st = m.status()                     # instruction in its segment
            if st["cs"] == pseg:
                at = (st["cs"], st["ip"])
                break
        got = m.read(pen, 2).hex()
        m.run()
        if at is None:
            print("   FAIL  never left the kernel in 400 steps")
            return 1
        ok = got == REST
        print("   %s  callback: the pen at %04x:%04x is %s (rest %s)"
              % ("ok " if ok else "FAIL", at[0], at[1], got, REST))
        return 0 if ok else 1


def main():
    print(MACHINE)
    bad = leg_desk() + leg_callback()
    print("deskpen: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
