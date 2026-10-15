#!/usr/bin/env python3
"""SPEC.md 5.4.2.5: `kern_small` has a `gfx_blit1` body, and Paint TAKES it.

Wave 1 of docs/plans/completed/GFX-EMBEDDABLE-PLAN.md reversed a recorded refusal: the
128 KB build carried the SLOT and not the BODY, so Paint's one-bit canvas
(SPEC.md 42.23) - the thing that build exists for - reached the screen through
`pt_ex1` and `gfx_blit4` a row at a time, at 24x the cost.  The thunk points at
the body on both builds now.

THAT THE THUNK POINTS SOMEWHERE IS NOT THE CLAIM.  What has to be true is that
Paint's repaint actually goes through it on that kernel, and the assembler
cannot say so: `gfx_blit1` could return CF = 1 from any of its argument
refusals and Paint would fall back exactly as before, silently and at the same
24x.  So this asks the running machine.

THE ORACLE IS `pt_line`, and it needs no instrumentation in the product: the
fallback expands each canvas row into that buffer and the fast path never
touches it.  A sentinel written there survives one and not the other.  It is
tests/paint1blit.py's technique - that row proves the same thing on kern_big,
and covers the PICTURE as well - and the two differ in how Paint is opened,
which is the whole reason this is a file of its own:

    KERN_SMALL HAS NO FILE ASSOCIATION.  docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md
    wave 0 gated `assoc` out (SPEC.md 54.0), so double-clicking a .BMP - which
    is how paint1blit opens Paint on a picture - launches nothing at all there.
    Paint is opened directly instead, on the canvas it makes for itself, which
    is one-bit on a 1bpp adapter by SPEC.md 42.23 and is asserted below before
    anything else is read.

A stroke and Ctrl+Z is what repaints out of the canvas; a window drag will NOT
do it, because on a 1bpp adapter Paint banks the whole content (SPEC.md
11.96.11) and a drag is served from the raise cache.

BREAK IT ON PURPOSE: put `stc / ret` back over kern_small's `gfx_blit1` thunk
and this goes red, which is the state the kernel shipped in until wave 1.
"""
import os
import tempfile
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import os88build as _B                                   # noqa: E402

# os88sym resolves against build/kernel.bin unless told otherwise, and the two
# kernels differ by far more than the build number - so without this the row
# dies at its first symbol saying the map describes a DIFFERENT kernel, which
# points at the kernel rather than at the row.  tests/vmmouse.py's pattern.
_B.use_build("build/smallk")
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88marty                                         # noqa: E402
import os88mouse                                         # noqa: E402
import os88ui                                            # noqa: E402
import dispapps                                          # noqa: E402

SENT = bytes([0xA5]) * 64


def main(argv):
    machine = "os8088_5150_herc_gla"
    if "--machine" in argv:
        machine = argv[argv.index("--machine") + 1]
    os.chdir(ROOT)
    fails = []

    apps = os88marty.scratch_disk(tempfile.gettempdir() + "/p1small.img", "APPS:build/paint.o88")
    with os88ui.boot("build/small360.img", apps=apps, machine=machine) as ui:
        m = ui.m
        ui.path("B:/APPS/PAINT.O88")
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            print("paint1small: Paint did not open")
            return 1
        seg = got[1]
        img = dispapps.img_size("paint")

        def off(n):
            return (seg << 4) + img + dispapps.bss_off("paint", n)

        def w16(n):
            return int.from_bytes(m.read(off(n), 2), "little")

        if m.read(off("pt_planar"), 1)[0] != 0:
            fails.append("the canvas is PLANAR - this row is about the "
                         "one-bit one (SPEC.md 42.23) and has nothing to say")
        cx0, cy0 = w16("pt_cx0"), w16("pt_cy0")

        mo = os88mouse.Mouse(marty=m)
        m.write(off("pt_thick"), bytes([3]))
        sx, sy = cx0 + 24, cy0 + 24
        mo.to(sx, sy)
        os88marty.settle(m)
        if mo.where()[2] & 1:
            mo._edge(False)
        mo._edge(True)
        mo.to(sx + 100, sy + 30, l=True)
        mo._edge(False)
        os88marty.settle(m)
        mo.to(4, 4)
        os88marty.settle(m)

        LINE = off("pt_line")
        m.write(LINE, SENT)
        m.advance(frames=2)
        m.run()
        m.ctrl("KeyZ")                  # undo: repaints out of the canvas
        m.advance(frames=300)
        m.run()
        os88marty.settle(m)
        if m.read(LINE, 64) != SENT:
            fails.append("pt_line was SCRUBBED, so the repaint expanded a row "
                         "at a time - kern_small's gfx_blit1 is refusing and "
                         "the canvas is still 24x (SPEC.md 5.4.2.5)")

    for f in fails:
        print("paint1small: " + f)
    if fails:
        print("paint1small: FAIL")
        return 1
    print("paint1small: PASS - kern_small's one-bit canvas repaints through "
          "gfx_blit1, not through the row loop")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
