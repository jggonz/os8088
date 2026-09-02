#!/usr/bin/env python3
"""gfx_xor_rect draws the same pixels it always did (SPEC.md 39.14.10)

    make && python3 tests/xorrect.py --dump /tmp/xor-new.bin
    <reference kernel in build/> && python3 tests/xorrect.py \\
        --image /tmp/ref-360.img --expect /tmp/xor-new.bin

§39.14.10 moved the display hook above `gfx_xor_rect`'s `[vid_mono]` dispatch
and folded `gfx_xor_rect_sw` away. On a machine with ONE display that is a
no-op by construction - `GFXDENTER`/`GFXDORG` are empty macros in kern_small
and a single compare in kern_big - but "by construction" is the claim this
tree checks rather than asserts, because `gfx_xor_rect` is what draws the
DRAG OUTLINE on every machine, and the routine also lost a duplicate
`cur_unlazy` and gained two registers of preservation.

So: an identical scripted session on a CGA - 1bpp, which is the adapter where
the software renderer IS the direct path and therefore the branch that moved -
through both kernels, compared as FRAMEBUFFER BYTES. The outline is XOR and
self-inverting, so a mid-drag capture is where a difference would show; the
run captures both mid-drag and after the button comes up.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402

CGA_VRAM, CGA_LEN = 0xB8000, 16384
TITLE_H = 18


def session(m, mo):
    """Every capture is a full CGA framebuffer. The drag is driven by hand -
    press, move, capture WITH THE OUTLINE UP, move again, release - because
    mouse.drag() would take the outline down before anything could look."""
    shots = []

    def snap(tag):
        shots.append((tag, m.read(CGA_VRAM, CGA_LEN)))

    os88marty.settle(m)
    snap("desktop")

    # Locator's File menu, then Disk - a window to drag.
    mo.menu(20, 8, 20, 45)
    os88marty.settle(m)
    snap("menu-used")

    mo.to(200, 100)
    m.mouse(0, 0, l=True)
    time.sleep(0.4)
    mo.to(240, 120, l=True)
    time.sleep(0.6)
    snap("drag-outline-up")         # vga_xor_rect_vram -> sw_xor_rect
    mo.to(300, 140, l=True)
    time.sleep(0.6)
    snap("drag-outline-moved")
    m.mouse(0, 0, l=False)
    os88marty.settle(m)
    snap("drag-done")
    return shots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--dump")
    ap.add_argument("--expect")
    a = ap.parse_args()

    with os88marty.launch(a.image, apps=a.apps,
                          machine="os8088_5150_cga_gla", boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        shots = session(m, mo)

    blob = b"".join(s for _, s in shots)
    for tag, s in shots:
        print("  %-20s %6d lit" % (tag, sum(bin(c).count("1") for c in s)))
    if a.dump:
        open(a.dump, "wb").write(blob)
        print("wrote %s (%d bytes, %d captures)"
              % (a.dump, len(blob), len(shots)))
    if a.expect:
        ref = open(a.expect, "rb").read()
        if len(ref) != len(blob):
            print("FAIL: %d bytes against the reference's %d"
                  % (len(blob), len(ref)))
            return 1
        bad = sum(1 for i in range(len(ref)) if ref[i] != blob[i])
        for i, (tag, s) in enumerate(shots):
            off = i * CGA_LEN
            n = sum(1 for j in range(CGA_LEN)
                    if ref[off + j] != blob[off + j])
            print("  %-20s %d differing byte(s)" % (tag, n))
        print("%s: %d differing framebuffer byte(s)"
              % ("FAIL" if bad else "PASS", bad))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
