#!/usr/bin/env python3
"""DOES UNDO PUT THE PICTURE BACK, TO THE PIXEL? (SPEC.md 42.8.6, 41.8)

    make && python3 tests/paintundo.py [--machine os8088_5150_herc_gla]

Paint's undo is copy-on-first-touch: `pt_undo_new` clears a bitmap, `pt_umark`
saves a piece of the canvas the first time an operation touches it, and
`pt_undo_swap` EXCHANGES the saved pieces with the live ones - so Undo and
Redo are the same instruction and alternate forever.  This drives that:

    blank -> draw -> Ctrl+Z -> must be blank again -> Ctrl+Z -> must be drawn

...comparing the canvas as a region hash each time.

**Redo IS the canvas check, and that is why there is no window drag here.**
`pt_undo_swap` exchanges the saved pieces and then repaints the rows it
touched OUT OF THE CANVAS, so the screen after the second Ctrl+Z was drawn
from RAM and not left over from the stroke. If the canvas held anything the
glass did not, `redone` could not match `drawn`. Dragging the window to force
a repaint - which is what tests/paintdraw.py does - is NOT reliable here: on a
1bpp adapter a short move can be served without repainting the content at all,
and then `[pt_cx0]`/`[pt_cy0]` are whatever the last `pt_org` left, which reads
as a canvas mismatch when it is really a stale sampling origin.

**Nothing covered undo before this**, which mattered the moment SPEC.md 42.8.6
changed the granularity of the bitmap from one bit a ROW to one bit a BLOCK:
the copy, the swap and the marking all had to move together, and a swap that
restored the wrong bytes would look like a drawing bug days later.

The stroke is deliberately three chords in three directions - across, down and
diagonal.  Across touches many blocks of a few rows, down touches one block of
many rows, and the diagonal is the chord SPEC.md 42.8.5's banking gate refuses,
so all three of pt_seg's paths mark something.
"""
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
NIB = 3                             # the 8 px nib: several blocks wide


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name):
    return int.from_bytes(m.read(_boff(seg, name), 2), "little")


def _hash(m, x0, y0, w, h):
    fw, fh, fb = m.fbuf(card=0)
    out = bytearray()
    for yy in range(y0, min(y0 + h, fh)):
        for xx in range(x0, min(x0 + w, fw)):
            out.append(1 if fb[(yy * fw + xx) * 3] < 128 else 0)
    return hashlib.sha256(bytes(out)).hexdigest()[:16], sum(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--no-undo", action="store_true",
                    help="draw and repaint only - is the DRAWING "
                         "already out of step with the canvas?")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("paintundo: PAINT.O88 did not open")
        pw, seg = got
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        cw, ch = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        # A sub-rect WELL INSIDE the canvas and comfortably around the stroke.
        # Hashing the full cw x ch reaches past the window on a 720px screen
        # and takes in desktop and chrome, which showed up at once as a "blank"
        # canvas holding 2,754 black pixels.
        RX, RY, RW, RH = 20, 20, 240, 200
        m.write(_boff(seg, "pt_thick"), bytes([NIB]))
        m.advance(frames=4)
        m.run()

        mo.to(4, 4)
        os88marty.settle(m)
        blank = _hash(m, cx0 + RX, cy0 + RY, RW, RH)
        print("   blank canvas      %s  (%d ink)" % blank)

        # across, down, diagonal - one chord each, so pt_seg's banked x path,
        # its banked y path and the per-step path the gate refuses all run
        sx, sy = cx0 + 40, cy0 + 40
        mo.to(sx, sy)
        os88marty.settle(m)
        if mo.where()[2] & 1:
            mo._edge(False)
        mo._edge(True)
        for (tx, ty) in ((sx + 120, sy), (sx + 120, sy + 90),
                         (sx + 190, sy + 160)):
            mo.to(tx, ty, l=True)
        mo._edge(False)
        os88marty.settle(m)
        mo.to(4, 4)
        os88marty.settle(m)
        drawn = _hash(m, cx0 + RX, cy0 + RY, RW, RH)
        print("   after the stroke  %s  (%d ink)" % drawn)

        undone, redone = blank, drawn
        if not a.no_undo:
            m.ctrl("KeyZ")
            os88marty.settle(m)
            m.advance(frames=60)
            m.run()
            mo.to(4, 4)
            os88marty.settle(m)
            undone = _hash(m, cx0 + RX, cy0 + RY, RW, RH)
            print("   after Ctrl+Z      %s  (%d ink)" % undone)

            m.ctrl("KeyZ")
            os88marty.settle(m)
            m.advance(frames=60)
            m.run()
            mo.to(4, 4)
            os88marty.settle(m)
            redone = _hash(m, cx0 + RX, cy0 + RY, RW, RH)
            print("   after Ctrl+Z x2   %s  (%d ink)" % redone)


    ok = True
    if drawn[1] < 500:
        print("\n   the stroke drew %d pixels - it never reached the canvas, "
              "so this run proves nothing" % drawn[1])
        ok = False
    if undone[0] != blank[0]:
        print("\n   UNDO did not restore the blank canvas (%d ink left, was "
              "%d)" % (undone[1], blank[1]))
        ok = False
    if redone[0] != drawn[0]:
        print("\n   REDO did not restore the stroke (%d ink, wanted %d)"
              % (redone[1], drawn[1]))
        ok = False
    print()
    print("paintundo: %s" % ("PASS - undo restores the blank exactly and redo "
                             "repaints the\n  stroke out of the canvas, to the "
                             "pixel" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
