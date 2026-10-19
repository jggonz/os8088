"""Redline Apply's screenshots, for tools/guide/manuals/RLAPPLY_O88.md.

    python3 tools/guide/capture/run.py RLAPPLY_O88 --apps build/redline360.img

RLAPPLY.O88 is in A:\\SYSTEM on the system disk; REDLINE.O88 (whose Tune tab
writes what RLAPPLY puts back) is on its own disk, in B:.
"""

import os88marty


def capture(ui, shot):
    w = ui.path("A:/SYSTEM")
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.to(x + ww + 40, y + hh + 40)      # the pointer, out of the picture
    ui.settle()
    shot("rlapply-folder", rect=(x - 6, y - 6, ww + 14, hh + 14))

    # It has no window: it says what it did on the menu bar, and ends.
    # MartyPC's XT has never been tuned, so there is no REDLINE.CFG.
    # (expect=None: the toast lasts ~3 seconds, so wait for it here
    # rather than for a window that never comes)
    ui.open("RLAPPLY.O88", expect=None)
    for _ in range(400):
        txt, on = ui.toast()
        if on and "REDLINE" in txt.upper():
            break
        ui.m.advance(frames=2)
    ui.m.advance(frames=6)
    shot("rlapply-toast", rect=(0, 0, 640, 40), settle=False)
    ui.m.run()                        # advance() leaves the machine paused

    # REDLINE's Tune tab, where the setting comes from
    ui.path("B:/REDLINE.O88")
    ui.settle()
    ui.m.type_text("h")
    os88marty.guest_sleep(ui.m, 10.0)
    ui.settle()
    shot("rlapply-tune", window="REDLINE", pad=4)
