#!/usr/bin/env python3
"""SPEC.md 42.22.5: the small build of Paint GROWS its canvas.

`APP_SMALL` used to drop the canvas ceiling (`PT_CW_MAX`/`PT_CH_MAX`) to the
DEFAULT canvas, 448x280, to save 1,256 bytes of package bss.  A fresh Paint
opens at that default, so on kern_small `[pt_cwmax]` came out equal to
`[pt_cw]` and every grow - a drag of the grow box, or a size typed into the
boxes and applied - was clamped straight back to the size the canvas already
had.  pt_sizeask reports a memory cut and an ink guard, and a clamp is
neither, so the refusal came with no message.  The heap was never the reason:
`[pt_growp]` is hundreds of KB on any machine with the RAM to say so.

So this opens the small build's Paint on kern_small, asks for a canvas 16
columns WIDER through the size boxes' own Enter path (pt_szkey -> pt_szapply
-> pt_setsize, the same pt_sizeask a drag goes through) and reads the canvas
back.  Then a shrink, so a fix that broke the other direction is red too.

The width is the axis asked about because the HEIGHT on Hercules is the
screen's own bound (`[pt_chmax]` = 258 there, the desktop band) - a grow that
stops there is the screen, not this defect.

BREAK IT ON PURPOSE: put `PT_CW_MAX equ PT_CW_DEF` / `PT_CH_MAX equ
PT_CH_DEF` back under `%ifdef APP_SMALL` and the grow reads 448 -> 448.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import os88build as _B                                   # noqa: E402

# kern_small's symbols, not build/kernel.bin's (tests/paint1small.py's note)
_B.use_build("build/smallk")
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88marty                                         # noqa: E402
import os88ui                                            # noqa: E402
import dispapps                                          # noqa: E402


def main(argv):
    machine = "os8088_5150_herc_gla"
    if "--machine" in argv:
        machine = argv[argv.index("--machine") + 1]
    os.chdir(ROOT)
    fails = []

    # The system disk carries the SMALL build of Paint in APPS/ - the one
    # under test - so no apps disk is mounted.
    with os88ui.boot("build/small360.img", machine=machine) as ui:
        m = ui.m
        ui.path("A:/APPS/PAINT.O88")
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            print("paintgrowsmall: Paint did not open")
            return 1
        seg = got[1]
        img = dispapps.img_size("paint", small=True)

        def off(n):
            return (seg << 4) + img + dispapps.bss_off("paint", n, small=True)

        def w16(n):
            return int.from_bytes(m.read(off(n), 2), "little")

        def ask(nw, nh):
            m.write(off("pt_wtxt"), b"%d\0" % nw)
            m.write(off("pt_htxt"), b"%d\0" % nh)
            m.write(off("pt_fbox"), b"\x01")     # the width box has focus...
            m.key("Enter")                        # ...and Enter applies both
            os88marty.ui_done(m)
            return w16("pt_cw"), w16("pt_ch")

        cw, ch = w16("pt_cw"), w16("pt_ch")
        cwmax = w16("pt_cwmax")
        print("paintgrowsmall: opened %dx%d, ceiling %dx%d, grow bound %d "
              "paragraphs" % (cw, ch, cwmax, w16("pt_chmax"), w16("pt_growp")))
        if cwmax <= cw:
            fails.append("[pt_cwmax] = %d is no wider than the canvas Paint "
                         "OPENS at (%d), so no grow can ever be honoured "
                         "(SPEC.md 42.22.5)" % (cwmax, cw))

        gw, gh = ask(cw + 16, ch)
        print("paintgrowsmall: asked %dx%d, got %dx%d" % (cw + 16, ch, gw, gh))
        if gw != cw + 16 or gh != ch:
            fails.append("a 16-column grow was refused: %dx%d -> %dx%d"
                         % (cw, ch, gw, gh))

        sw, sh = ask(gw - 40, gh - 20)
        print("paintgrowsmall: asked %dx%d, got %dx%d"
              % (gw - 40, gh - 20, sw, sh))
        if (sw, sh) != (gw - 40, gh - 20):
            fails.append("a shrink of a blank canvas was refused: %dx%d -> "
                         "%dx%d" % (gw, gh, sw, sh))

    for f in fails:
        print("paintgrowsmall: " + f)
    if fails:
        print("paintgrowsmall: FAIL")
        return 1
    print("paintgrowsmall: PASS - the small build's Paint grows and shrinks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
