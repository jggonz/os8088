#!/usr/bin/env python3
"""SPEC.md 42.9.1: Paint can be grown over the dock, to the screen's bottom.

The kernel lets a grow box be dragged to the display's last row, and Note Pad
goes there. Paint's own ceiling `[pt_chmax]` was taken from the DOCK row, so
its negotiator clamped the same drag back above the strip (frame bottom 323 on
Hercules' 348), and Apply could not get there either. This row opens Paint
and checks four things, off guest state:

  1. a FRESH window still opens above the dock (`[pt_chband]` is the bound);
  2. dragging the grow box to the bottom row ends the frame on that row;
  3. Apply with a height of 999 gives a frame `vid_h - MBAR_H` tall at the
     menu bar - wm_resize honours it because Paint is WF_KEEPH (11.93.1);
  4. the canvas inside is that frame less the chrome.

`--small` runs the kern_small build of Paint on small360.img.

BREAK IT ON PURPOSE: compute `[pt_chmax]` from the dock row again in
pt_screen, or drop WF_KEEPH from pt_wcls - the first fails 2 and 3, the second
fails 3 (wm_resize cuts the frame at the dock).
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

SMALL = "--small" in sys.argv
if SMALL:
    import os88build as _B                               # noqa: E402
    _B.use_build("build/smallk")
    os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88marty                                         # noqa: E402
import os88ui                                            # noqa: E402
import os88geom as geom                                  # noqa: E402
import dispapps                                          # noqa: E402

MBAR_H = 20
PT_CHROME_H = 42


def main(argv):
    machine = "os8088_5150_herc_gla"
    if "--machine" in argv:
        machine = argv[argv.index("--machine") + 1]
    os.chdir(ROOT)
    fails = []
    if SMALL:
        img, apps, path = "build/small360.img", None, "A:/APPS/PAINT.O88"
    else:
        img, apps, path = ("build/os8088-360.img", "build/apps360.img",
                           "B:/APPS/PAINT.O88")

    with os88ui.boot(img, apps=apps, machine=machine) as ui:
        m = ui.m
        vh = geom.word(m, "vid_h")
        ui.path(path)
        w = ui.front()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            print("paintdock: Paint did not open")
            return 1
        seg = got[1]
        base = dispapps.img_size("paint", small=SMALL)

        def off(n):
            return (seg << 4) + base + dispapps.bss_off("paint", n, small=SMALL)

        def r(n):
            return int.from_bytes(m.read(off(n), 2), "little")

        dock = r("pt_dockr")
        w = ui._refresh(w)
        print("paintdock: opened frame y=%d h=%d (bottom %d), dock row %d, "
              "screen %d" % (w.y, w.h, w.y + w.h, dock, vh))
        if w.y + w.h > dock:
            fails.append("a FRESH Paint opens over the dock: bottom %d, dock "
                         "%d" % (w.y + w.h, dock))

        # 2. the grow box to the bottom row
        gx, gy = w.x + w.w - 8, w.y + w.h - 8
        ui._grab(w, gx, gy)
        ui.mo.to(gx, vh - 2, l=True)
        os88marty.settle(m)
        ui.mo._edge(False)
        os88marty.ui_done(m)
        w = ui._refresh(w)
        print("paintdock: dragged to row %d: frame bottom %d, canvas %dx%d"
              % (vh - 2, w.y + w.h, r("pt_cw"), r("pt_ch")))
        if w.y + w.h != vh:
            fails.append("the grow box dragged to row %d left the frame "
                         "ending on %d, not the screen's bottom %d"
                         % (vh - 2, w.y + w.h, vh))

        # 3. Apply a height bigger than any screen
        m.write(off("pt_wtxt"), b"%d\0" % r("pt_cw"))
        m.write(off("pt_htxt"), b"999\0")
        m.write(off("pt_fbox"), b"\x01")
        m.key("Enter")
        os88marty.ui_done(m)
        w = ui._refresh(w)
        ch = r("pt_ch")
        print("paintdock: Apply 999: frame y=%d h=%d (bottom %d), canvas "
              "height %d" % (w.y, w.h, w.y + w.h, ch))
        if (w.y, w.h) != (MBAR_H, vh - MBAR_H):
            fails.append("Apply 999 gave a frame y=%d h=%d, not y=%d h=%d - "
                         "the menu bar to the screen's bottom"
                         % (w.y, w.h, MBAR_H, vh - MBAR_H))
        if ch != vh - MBAR_H - PT_CHROME_H:
            fails.append("the canvas is %d rows in that frame, not %d"
                         % (ch, vh - MBAR_H - PT_CHROME_H))

    for f in fails:
        print("paintdock: " + f)
    if fails:
        print("paintdock: FAIL")
        return 1
    print("paintdock: PASS - Paint grows over the dock to the screen's bottom")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
