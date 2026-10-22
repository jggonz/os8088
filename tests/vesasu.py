#!/usr/bin/env python3
"""A drop-down's save-under claim on the packed desktop holds what the kernel
writes into it (SPEC.md 120.5, the package rule for DH; 13.14.2; 5.8).
    make vesatest && python3 tests/vesasu.py [--png out.png]

Boots build/vesablitd.img with build/vxpics.img in B: - PiXEL opened on
CITY.PCX on the 1024x768x16 desktop, tests/vesablitd.py's machine - waits
for the decode, presses Ctrl+S for the Save As card, and CLICKS the card's
format drop-down open. os88ui_drpress banks the pixels under the open list
through OSAPI_GFX_SAVE into a claim it sizes itself (apps/os88ui.inc,
os88ui_drbank), and the record keeps the claim's segment and size.

WHAT IT ASSERTS: the list is open with a claim behind it; the claim is at
least DH x rows x byte-columns of the list - DH being 16 here, the bytes a
column a 16bpp save takes, with the list's rows a lower bound from its item
count; the kernel's claim table agrees with the record's segment and size
and has no two claims overlapping; and Escape closes the list and gives the
claim back.

THE RED IT WAS WRITTEN AGAINST: the sizer folded DH to 4-or-1 planes - 16
is not 4, so ONE - and claimed a sixteenth of what the save then wrote,
over whatever followed on the heap. The same fold sat in the pop-up menu's
sizer beside it and in ArtfulType's panel sizer; os88ui.inc is the one
drop-down five packages share, so this is where it is exercised. QEMU by
name, vesa's reason.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
import vesablitd                                            # noqa: E402
from os88geom import VID_VESA                               # noqa: E402
from pxsyms import pkg_syms, instance, u16                  # noqa: E402

# apps/os88ui.inc's drop-down record (OS88UI_DR_*)
DR_RECT, DR_N, DR_OPEN, DR_SEG, DR_KB, DR_TOP = 0, 10, 16, 18, 20, 22
DRIH = 12                           # OS88UI_DRIH: an item's pitch in rows


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    vesa.kill_stale()
    fails = []
    vesablitd.launch()
    try:
        q = heapmap.Qmp(vesa.SOCK)
        for _ in range(200):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)
        S = os88sym.linear
        os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") == VID_VESA, 120.0,
                       "vid_kind = VID_VESA")
        if vesa.byte(q, "vid_kind") != VID_VESA:
            print("vesasu: FAIL\n  the desktop did not come up on VID_VESA")
            return 1
        syms, image = pkg_syms()
        os88qemu.acted(q, lambda: instance(q, S, image), 120.0, "PiXEL's instance")
        seg = instance(q, S, image)
        if not seg:
            print("vesasu: FAIL\n  no PiXEL instance: the autostart link did not "
                  "open CITY.PCX")
            return 1
        base = seg * 16
        B = lambda n, k=0: q.read(base + syms[n] + k, 1)[0]            # noqa: E731
        W = lambda n, k=0: u16(q.read(base + syms[n] + k, 2))          # noqa: E731

        def idle():
            return B("px_busy") == 0 and B("px_job") == 0
        os88qemu.acted(q, lambda: W("px_ndone") and idle(), 300.0, "the decode")
        os88qemu.pace(q, 3.0)
        depth = B("px_bpp")
        if depth < 8:
            fails.append("PiXEL read DH = %d from OSAPI_WM_DISPLAY, want 8 or 16 on "
                         "the packed display" % depth)

        # --- the Save As card, and its format drop-down clicked open -------
        q.hmp("sendkey ctrl-s")
        os88qemu.pace(q, 3.0)
        rect = [W("px_fdrop", DR_RECT + 2 * i) for i in range(4)]
        if not (0 < rect[0] < rect[2] < vesa.WANT_W and 0 < rect[1] < rect[3] < vesa.WANT_H):
            fails.append("the card's format drop-down has no rect (%r): Save As "
                         "did not open" % (rect,))
            raise SystemExit(0)
        mouse = vesa.Mouse()
        mouse.click((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)
        os88qemu.pace(q, 2.0)
        if out_png:
            sw, sh, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            shot.png(out_png, sw, sh, pix)
        if not B("px_fdrop", DR_OPEN):
            fails.append("the drop-down did not open on the click at the centre "
                         "of %r" % (rect,))
            raise SystemExit(0)
        dseg, kb, n = W("px_fdrop", DR_SEG), W("px_fdrop", DR_KB), W("px_fdrop", DR_N)
        cols = (rect[2] >> 3) - (rect[0] >> 3) + 1
        rows = n * DRIH                      # a floor: the list is the items
        need = depth * rows * cols           # ...and a 16bpp save is 16 bytes
        print("vesasu: the list is %d items over %d byte columns; the claim is "
              "%dKB at %04X, a %dbpp save of it at least %d bytes"
              % (n, cols, kb, dseg, depth, need))
        if not dseg:
            fails.append("the open list has no save-under claim (the claim was "
                         "refused, or never asked for)")
        elif kb * 1024 < need:
            fails.append("the save-under claim is %dKB and the %dbpp save of a "
                         "%d-row, %d-column list is at least %d bytes: the sizer "
                         "folded DH to one plane and OSAPI_GFX_SAVE wrote the rest "
                         "past the claim" % (kb, depth, rows, cols, need))
        # ...and the kernel's own table agrees with the record
        m = heapmap.Map(q, heapmap.symbols())
        mine = [c for c in m.claims if c.seg == dseg]
        if dseg and not mine:
            fails.append("no claim in mem_tab starts at %04X, the record's segment" % dseg)
        elif mine and mine[0].para != kb * 64:
            fails.append("mem_tab's claim at %04X is %d paragraphs, the record says "
                         "%dKB" % (dseg, mine[0].para, kb))
        last = None
        for c in m.claims:
            if last is not None and c.seg < last.end:
                fails.append("claims overlap: %r / %r" % (last, c))
            last = c
        # --- Escape: closed, and the claim given back ----------------------
        q.hmp("sendkey esc")
        os88qemu.pace(q, 2.0)
        if B("px_fdrop", DR_OPEN) or W("px_fdrop", DR_SEG):
            fails.append("after Escape the list is still open (%d) or still holds "
                         "its claim (%04X)" % (B("px_fdrop", DR_OPEN), W("px_fdrop", DR_SEG)))
    except SystemExit:
        pass
    finally:
        vesa.kill_stale()
    if fails:
        print("vesasu: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesasu: ok - a drop-down's save-under on the 1024x768x16 desktop is "
          "sized for the depth")
    return 0


if __name__ == "__main__":
    sys.exit(main())
