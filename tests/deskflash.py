#!/usr/bin/env python3
"""A desktop cell is drawn ONCE, and only where it shows (SPEC.md 26.9.9)

    make && python3 tests/deskflash.py              # VGA and CGA
    python3 tests/deskflash.py --machine os8088_xt_vga

The owner's gesture, on a 4.77 MHz 8088 under MartyPC, frame by frame:

  cell   cell A repainted IN PLACE (desk_cdirty posted, as a mount or a
         deselect does), unselected and then selected: NO pixel on the whole
         screen may change and change back. It used to blank to the dither and
         draw the icon over it - 600-1,050 px on VGA and Hercules - and the
         pointer, parked well away, blinked with every desktop repaint.
  over   a Disk window dragged to cover half the drive column: NO cell pixel
         may change at all. It used to repaint a 1-px column of every cell -
         the window's shadow L, which the window pass then drew back.
  back8  ...dragged 8 px back, so the revealed edge is inside the picture
         column and 3 px off the byte grid (a 322-wide window): the sliver
         changes, and NO cell pixel flashes.
  back24 ...and 24 px more: the same.

The drags are whole multiples of 8 so that the drag OUTLINE, drawn at the
pointer's unsnapped x, lands where the window does (SPEC.md 11.96.13) - the
outline is the drag's own feedback and not a cell's. Hercules is not run: the
shadow's row under a window flashes there on the kernel before this too, and
it lands in the drive column.

Each drag is also checked RIGHT and not only quiet: the drive column right of
the window against a forced whole repaint (tools/deskclip.py's verify).

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured on VGA
against the second build of SPEC.md 26.9.9:
  * wm_dmg_gray's `call wm_occl_l` taken out: `over` red on the DRAW count
    alone (2 cells drawn) - a cell redrawn under the shadow's L writes the
    values already on the glass, so no pixel count can see it, which is why
    the row arms `desk_draw_zone`;
  * gfx_blit1's `.offg` head piece taken out (SPEC.md 5.4.2.8): `back8` red
    on the whole-repaint compare, 204 px - the sliver's head byte column
    left STALE, not flashed;
  * desk_zones_r's take-out of each drawn zone from the region (its
    `wm_clip_subr` call) made a no-op: `back8` red, 243 px flashed and 351
    stale, and `over` 782 stale - the dither laid over the cells just
    drawn. The `cell` legs stay green there, and that is the leg's shape
    rather than a hole: they count CHANGE, and with that break the boot's
    own paint has already dithered the icons away, so a repaint changes
    nothing.

(The first build's breaks - its zone ground subtraction, 566 px, and
`desk_bout`'s head, 8/202/16 px - are the same three properties; the
compare also found that build's one bug, a caption head byte drawn under an
empty `gfx_b1hm` mask, one stray column at x 576.)

ONE READING IN ABOUT SIX shows three alternating pixels on one CGA row
change and change back (`ffffff`, `000000`, `ffffff`) - on the kernel before
the second build as well (1 of 6 runs), at a different row each time, and in
an in-place leg as often as a drag one. It is not a cell's draw order.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty            # noqa: E402
import os88ui               # noqa: E402
import os88geom as geom     # noqa: E402
import deskclip as dc       # noqa: E402

BDIR = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)


def frames_after(m, trig, n):
    """Pause, inject, and the glass once per displayed frame for n frames."""
    m.pause()
    _w, _h, a = m.fbuf()
    trig()
    out = []
    for _ in range(n):
        m.advance(frames=1)
        out.append(m.fbuf()[2])
    m.run()
    return a, out


def flashes(a, frames, w, box, skip=None):
    """(changed, flashed) over box: a pixel FLASHES when it takes more than two
    values, or ends where it began after changing."""
    fin = frames[-1]
    changed = flashed = 0
    for y in range(box[1], box[3] + 1):
        for x in range(box[0], box[2] + 1):
            if skip and skip[0] <= x <= skip[2] and skip[1] <= y <= skip[3]:
                continue
            i = (y * w + x) * 3
            vals = {bytes(a[i:i + 3])} | {bytes(f[i:i + 3]) for f in frames}
            if len(vals) > 1:
                changed += 1
                if len(vals) > 2 or bytes(a[i:i + 3]) == bytes(fin[i:i + 3]):
                    flashed += 1
    return changed, flashed


def post_cell(m, cell):
    dirty = m.sym("desk_cdirty") + (cell >> 3)
    m.write(dirty, bytes([m.read(dirty, 1)[0] | (1 << (cell & 7))]))
    m.write(m.sym("desk_zdirty"), b"\x01")
    m.write(m.sym("ui_post"), b"\x01")


def run(machine):
    print("%s" % machine)
    with os88ui.boot(os.path.join(BDIR, "os8088-360.img"),
                     apps=os.path.join(BDIR, "apps360.img"),
                     machine=machine) as ui:
        m = ui.m
        vw, vh = geom.word(m, "vid_w"), geom.word(m, "vid_h")
        whole = (0, 0, vw - 1, vh - 1)
        ui.mo.to(vw // 2, vh // 2)
        ui.settle()
        cell = geom.drive_ordinal(m, "A")
        for sel in (False, True):
            if sel:
                m.write(m.sym("desk_sel"), bytes([cell]))
                post_cell(m, cell)
                ui.settle()
            a, fr = frames_after(m, lambda: post_cell(m, cell), 30)
            ch, fl = flashes(a, fr, vw, whole)
            check(fl == 0 and (sel or ch == 0),
                  "cell%s repainted in place: %d px changed, %d flashed"
                  % (" (selected)" if sel else "", ch, fl))
        m.write(m.sym("desk_sel"), b"\xff")
        post_cell(m, cell)

        w = ui.open_drive("A")
        w = ui.move_window(w, 16, vh // 3)
        ui.mo.to(4, vh - 20)
        ui.settle()
        cr = dc.cell_rect(m, 0)
        rows = geom.word(m, "desk_rows")
        col = (cr[0], cr[1], cr[2], dc.cell_rect(m, rows - 1)[3])
        mid = (cr[0] + cr[2]) // 2
        for name, dx, dy in (("over", ((mid - w.w) - w.x) & ~7, 26 - w.y),
                             ("back8", -8, 0), ("back24", -24, 0)):
            trig = dc.drag_setup(ui, dx, dy, w)
            # every cell DRAW, not only every pixel that moved: a cell redrawn
            # under the shadow's L writes the values already there
            with os88marty.bp_trace(m, "desk_draw_zone") as tr:
                a, fr = frames_after(m, trig, 70)
            w = ui._refresh(w)
            ch, fl = flashes(a, fr, vw, col,
                             skip=(w.x, w.y, w.x + w.w, w.y + w.h))
            if name == "over":
                check(ch == 0 and tr.n == 0,
                      "%s: %d cell px changed, %d cells drawn (none may)"
                      % (name, ch, tr.n))
            else:
                check(ch > 0 and fl == 0,
                      "%s: %d cell px changed, %d flashed" % (name, ch, fl))
            # ...and RIGHT, not only quiet: an under-drawn sliver is stale
            # rather than flashing. Right of the window only - the whole
            # repaint is not a perfect reference for a title (deskclip)
            ui.settle()
            bad = dc.verify(ui, only=(w.x + w.w + 1, col[1], col[2], col[3]))
            check(bad == 0, "%s: the column matches a whole repaint (%d px "
                  "differ)" % (name, bad))
            w = ui._refresh(w)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append")
    args = ap.parse_args()
    for machine in args.machine or ["os8088_xt_vga", "os8088_5150_cga_gla"]:
        run(machine)
    print("deskflash: %s" % ("PASS" if not fails else
                             "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
