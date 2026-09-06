#!/usr/bin/env python3
"""WHAT A CANVAS BIGGER THAN ITS FIRST CLAIM DOES (SPEC.md 42.13.2)

    make && python3 tests/paintbig.py

Growing the canvas is the operation that changes [pt_bpr], and [pt_bpr] is
the only number the two storage formats do not share. A packed row is a run
of nibbles, so carrying its first N bytes across carries its leftmost
columns; a planar row is four plane-runs end to end, and a width change moves
every boundary inside it. pt_resize moved the row as one block, so old plane
1 landed inside new plane 0 and the picture came back as vertical bands of
the wrong colour. It is a spectacular failure and nothing before this row
could see it: every other Paint test opens a picture at its natural size and
never resizes.

Growing is also what makes a Copy ask for a BIGGER CLIPBOARD, which is the
second thing here. The claim starts at a 4KB floor and pt_clip_need swaps it
for a larger one the first time a selection needs more - and it did that by
calling pt_free_clip, which cleared [pt_cbw]. The Copy then stored a width of
zero and copied no columns at all, so the first selection worth copying
silently copied nothing and every Paste after it pasted nothing. The
selection here is 200x100 - 10,000 packed bytes, comfortably over the floor -
for exactly that reason.

It ends by reading the three Edit labels out of pt_it_edit, because a
capability the keyboard has and the menu denies is the same defect twice.

The paste lands at (0,0) because pt_paste uses the selection's corner and
there is none: picking a tool drops the selection. So the block pasted over
the logo's top-left must be the block the marquee took from the middle of it,
which is a compare against the FILE and not against a golden image. The
marquee's own edge can land a pixel either way - a drag is reports, not
coordinates - so the compare is allowed to shift by one and has to be EXACT
at one of them.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
import dispapps                                              # noqa: E402
from blitpair import gif_pixels                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

if os.environ.get("NOPLANE"):
    os88sym.default_defines("NOPLANE")      # the knob kernel is a DIFFERENT
                                            # kernel and every address here
                                            # has to be asked for with it

S = os88sym.linear
PT_CV_X = 48
PT_BW = 20
PT_PAL_DY = 21
PT_T_PENCIL = 0
PT_T_SEL = 5
SEL_W, SEL_H = 200, 100
SRC_X, SRC_Y = 203, 5           # ...both off the byte grid, at different
DST_X, DST_Y = 5, 130           # phases, and the destination clear of the logo
SELV = ("pt_selx1", "pt_sely1", "pt_selx2", "pt_sely2")


def tool_xy(ox, oy, i):
    x = 1 if not (i & 1) else 22
    y = 1 + PT_PAL_DY * (i >> 1)
    return ox - PT_CV_X + x + PT_BW // 2, oy + y + PT_BW // 2


def widest(m, blit, iw, limit=40):
    """The regs at the next `blit` call at least as wide as the picture."""
    for _ in range(limit):
        if not m.wait_stop(limit=300.0):
            return None
        r = m.regs()
        if r["cx"] >= iw:
            return r
        m.bp_exec(blit)
        m.run()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintbig.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default=None)
    # WHICH PRIMITIVE THE CANVAS ARRIVES ON is what says which format it is
    # in, so it is also how this row is pointed at the packed clipboard: on a
    # NOPLANE kernel every gfx_blitp refuses, Paint falls back to nibbles
    # (SPEC.md 42.13.1) and draws through gfx_blit4 instead.
    ap.add_argument("--blit", default="gfx_blitp")
    a = ap.parse_args()

    # A COLOUR PICTURE, and it has to be said now: SPEC.md 42.23.6 opens a GIF
    # whose colour table has two entries ONE BIT DEEP on any adapter, and
    # build/OS8088.GIF has exactly two - so the fixture every picture row here
    # uses stopped being able to give this one a four-plane canvas.
    # dispapps.colour_gif appends two unused entries and changes not one
    # pixel, so every oracle below is the one it always was.
    gif = dispapps.colour_gif()
    gifname = os.path.basename(gif)

    if a.apps == "/tmp/paintbig.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + gif)

    iw, ih, px = gif_pixels(gif)
    sym = pkg_syms("apps/paint/paint.asm")
    print("   %s: %dx%d" % (gif, iw, ih))

    def want(c, rw):
        return px[rw * iw + c] == 1

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)           # ...or the first long settle below
                                        # meets SPEC.md 79's blanker, which
                                        # DRAWS - so nothing ever settles and
                                        # the row dies on the harness rather
                                        # than on the picture
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "MEDIA")
        os88marty.settle(m)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              gifname)))
        mo.to(rx, ry)
        os88marty.settle(m)
        m.bp_exec(a.blit)
        mo.dblclick(rx, ry)
        r = widest(m, a.blit, iw)
        if r is None:
            sys.exit("paintbig: no %s as wide as the picture" % a.blit)
        # Paint's segment, off the API stub's saved DS - the menu check at
        # the end reads variables of Paint's, and this is the one place the
        # harness is stopped inside a call Paint made.
        pkgbase = int.from_bytes(
            m.read((r["ss"] << 4) + r["sp"] + 2, 2), "little") << 4
        m.bp_exec()
        m.run()
        time.sleep(6)
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)

        # --- GROW, by the grow box, as far as the desktop allows
        m.bp_exec(a.blit)
        mo.drag(wx + ww - 6, wy + wh - 6, 636, 476)
        r = widest(m, a.blit, iw)
        m.bp_exec()
        m.run()
        os88marty.settle(m)
        time.sleep(8)
        if r is None:
            sys.exit("paintbig: the picture never blitted after the grow, so "
                     "there is nowhere to read it back from")
        ox, oy = r["ax"], r["bx"]

        # THE CANVAS SIZE COMES FROM PAINT, NOT FROM THE BLIT. It used to be
        # the blit's own CX/DX, on the assumption that growing the canvas
        # repaints the whole of it - and that stopped being true: the repaint
        # after a grow lays the white ground and then blits the PICTURE'S
        # rect, 466x110, so CX/DX report the picture and the row concluded
        # "the canvas did not grow" about a canvas that had just gone
        # 472x110 -> 512x390. The blit still gives the picture's ORIGIN,
        # which is what the compare below actually needs.
        cw = m.read(pkgbase + sym["pt_cw"], 2)
        ch = m.read(pkgbase + sym["pt_ch"], 2)
        cw = cw[0] | (cw[1] << 8)
        ch = ch[0] | (ch[1] << 8)
        print("   picture back at %d,%d; canvas now %dx%d (picture %dx%d)"
              % (ox, oy, cw, ch, iw, ih))
        if cw <= iw and ch <= ih:
            sys.exit("paintbig: the canvas did not grow, so nothing here is "
                     "under test")
        mo.to(4, 4)
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)
        kept = [(c, rw) for rw in range(ih) for c in range(iw)
                if (fb[((oy + rw) * fw + ox + c) * 3] < 128) != want(c, rw)]
        print("   the picture after the grow: %d pixels differ from the file"
              % len(kept))
        if kept:
            xs = [b[0] for b in kept]
            ys = [b[1] for b in kept]
            print("      box x %d..%d y %d..%d, first 8: %r"
                  % (min(xs), max(xs), min(ys), max(ys), kept[:8]))

        # --- COPY a block bigger than the clipboard's floor, and PASTE it
        # BOTH x's ARE OFF THE BYTE GRID, and at different phases. A copy
        # shifts the selection's left edge down to bit 0 and a paste shifts it
        # back up to wherever it lands, so a block taken from x=203 and put
        # down at x=5 exercises both shifts and both edge masks; taken from a
        # multiple of 8 and pasted at one, neither runs at all and this test
        # would pass with the shifting deleted.
        def selrect():
            raw = m.read(pkgbase + min(sym[n] for n in SELV), 8)
            v = dict(zip(sorted(SELV, key=lambda n: sym[n]),
                         [raw[i] | (raw[i + 1] << 8) for i in range(0, 8, 2)]))
            return v["pt_selx1"], v["pt_sely1"], v["pt_selx2"], v["pt_sely2"]

        tx, ty = tool_xy(ox, oy, PT_T_SEL)
        mo.click(tx, ty)
        os88marty.settle(m)
        time.sleep(2)
        mo.drag(ox + SRC_X, oy + SRC_Y,
                ox + SRC_X + SEL_W - 1, oy + SRC_Y + SEL_H - 1)
        os88marty.settle(m)
        time.sleep(4)
        sx1, sy1, sx2, sy2 = selrect()
        print("   source selection (%d,%d)-(%d,%d), %dx%d, x phase %d"
              % (sx1, sy1, sx2, sy2, sx2 - sx1 + 1, sy2 - sy1 + 1, sx1 & 7))
        m.key("ControlLeft", down=True, up=False)
        m.key("KeyC")
        m.key("ControlLeft", down=False, up=True)
        os88marty.settle(m)
        time.sleep(8)

        # --- the destination: a marquee in the blank ground BELOW the
        # picture, so nothing here is comparing the block against itself.
        mo.drag(ox + DST_X, oy + DST_Y, ox + DST_X + 8, oy + DST_Y + 8)
        os88marty.settle(m)
        time.sleep(4)
        dx1, dy1 = selrect()[:2]
        print("   destination corner (%d,%d), x phase %d" % (dx1, dy1, dx1 & 7))
        m.key("ControlLeft", down=True, up=False)
        m.key("KeyV")
        m.key("ControlLeft", down=False, up=True)
        os88marty.settle(m)
        time.sleep(10)
        # The marquee is a LATCHED XOR (SPEC.md 11.90.2) and pt_paste re-shows
        # it over the block it just laid down, so the outline is in the
        # framebuffer and 4n-4 pixels of the compare below are it. Picking a
        # tool drops the selection, which XORs it back off.
        tx, ty = tool_xy(ox, oy, PT_T_PENCIL)
        mo.click(tx, ty)
        os88marty.settle(m)
        time.sleep(3)
        mo.to(4, 4)
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)

        # --- AND THE MENU HAS TO AGREE WITH THE KEYBOARD.
        # The kernel reads a menu set LIVE out of the owning segment (SPEC.md
        # 12.2), so the three Edit labels are three words in pt_it_edit and
        # this can read them rather than the screen. pt_clip_need swaps the
        # claim by calling pt_free_clip, which sets them to the "(NoRam)"
        # strings on its way past - true for those few instructions - and for
        # one release nothing put them back, so the first Copy over the floor
        # left Cut, Copy and Paste reading "(NoRam)" while all three worked
        # from Ctrl.
        tab = sym["pt_it_edit"]
        end = min(v for v in sym.values() if v > tab)
        raw = m.read(pkgbase + tab, min(end - tab, 64))
        words = [raw[i] | (raw[i + 1] << 8) for i in range(0, len(raw) - 1, 2)]
        greyed = [n for n in ("pt_i_cut2", "pt_i_copy2", "pt_i_paste2")
                  if sym[n] in words]
        print("   Edit menu after the Copy: %s"
              % ("all three funded" if not greyed
                 else "STILL SAYING (NoRam): %s" % ", ".join(greyed)))

    # The rects are READ rather than assumed - a drag is mouse reports, not
    # coordinates, and its edge can land a pixel either way - so this compare
    # is exact and needs no search.
    w, h = sx2 - sx1 + 1, sy2 - sy1 + 1
    bad = [(c, rw) for rw in range(h) for c in range(w)
           if (fb[((oy + dy1 + rw) * fw + ox + dx1 + c) * 3] < 128)
           != (px[(sy1 + rw) * iw + sx1 + c] == 1)]
    print("   pasted %dx%d block: %d pixels differ from the source region"
          % (w, h, len(bad)))
    if bad:
        xs = [b[0] for b in bad]
        ys = [b[1] for b in bad]
        print("      box x %d..%d y %d..%d, first 8: %r"
              % (min(xs), max(xs), min(ys), max(ys), bad[:8]))
    if kept or bad or greyed:
        sys.exit("paintbig: FAILED")
    print("paintbig: the picture survived the grow and the block came back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
