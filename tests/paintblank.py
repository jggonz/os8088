#!/usr/bin/env python3
"""IS A BLANK CANVAS STILL DECODED PIXEL BY PIXEL? (SPEC.md 42.15)

    make && python3 tests/paintblank.py [--machine os8088_xt_vga]

A full-canvas repaint is **980 ms** on a 4.77 MHz 8088 - one `gfx_blit4`, and
SPEC.md 5.4.1's pair decoder costs the same per pixel whatever is in them. The
picture Paint draws most often has nothing in it, and one colour is one
`gfx_fill`. `[pt_blank]` is the flag and this is its gate.

**IT COUNTS THE DECODER, NOT THE CLOCK.** "A blank canvas reaches gfx_blit4
zero times" is the property; a millisecond budget would pass on a machine that
happened to be fast and would drift with every unrelated change.

  BLANK    maximize a Paint that has never been drawn in: zero decoder calls
  MARKED   drawing marks a BAND of SPEC.md 42.18's inked table, which is
           what replaced 42.15.1's all-or-nothing flag
  BANDS    an INKED canvas grown wider fills the new columns instead of
           decoding them - SPEC.md 42.15.0, the drag-resize-larger case
  INK      after drawing and a maximize/restore round trip, the glass still
           SHOWS the stroke - which is what a flag wrongly reading blank would
           take away, since that paints the canvas area flat while the picture
           sits untouched in RAM
  TRUTH    ...and it survives undo and redo, which repaint out of the canvas
           through `pt_uswap_row` - itself one of the six clear-sites

**"An inked canvas must reach the decoder" is NOT asserted, deliberately.** It
is not a property of Paint: SPEC.md 11.96's raise cache can answer a repaint
without the app painting at all, so that count is 1 or 0 depending on what the
window manager had banked, and a gate built on it fails on a correct build.
INK is the assertion that means what that one was reaching for.
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
HZ = 4772727.0


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name):
    return int.from_bytes(m.read(_boff(seg, name), 2), "little")


PT_NBAND = 30                   # SPEC.md 42.18: (PT_CH_MAX >> 4) + 1


def _bands(m, seg):
    """[(band, x1, x2)] for every band the inked table has marked."""
    lo = m.read(_boff(seg, "pt_ibx1"), PT_NBAND * 2)
    hi = m.read(_boff(seg, "pt_ibx2"), PT_NBAND * 2)
    out = []
    for b in range(PT_NBAND):
        x1 = int.from_bytes(lo[b * 2:b * 2 + 2], "little")
        x2 = int.from_bytes(hi[b * 2:b * 2 + 2], "little")
        if x1 <= x2:
            out.append((b, x1, x2))
    return out


def _marked(m, seg):
    return len(_bands(m, seg))


def _table(m, seg):
    b = _bands(m, seg)
    if not b:
        return "table: nothing marked"
    return ("table: %d band(s), rows %d..%d, x %d..%d"
            % (len(b), b[0][0] * 16, b[-1][0] * 16 + 15,
               min(q[1] for q in b), max(q[2] for q in b)))


def _hash(m, x0, y0, w, h):
    fw, fh, fb = m.fbuf(card=0)
    out = bytearray()
    for yy in range(max(0, y0), min(y0 + h, fh)):
        for xx in range(max(0, x0), min(x0 + w, fw)):
            o = (yy * fw + xx) * 3
            out += fb[o:o + 3]
    return hashlib.sha256(bytes(out)).hexdigest()[:16]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
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
            sys.exit("paintblank: PAINT.O88 did not open")
        pw, seg = got
        # PAINT'S OWN label, not the kernel's: `.packed` is the instruction
        # that hands the canvas to OSAPI_GFX_BLIT4, so it names "this repaint
        # was DECODED" without depending on which kernel routine serves it.
        # The window actually changing size is what proves a repaint happened
        # at all - a breakpoint on pt_blit's entry does not fire here, and a
        # setup check that is itself broken is worse than none.
        pm = dispapps._map("paint")
        DEC = [(seg << 4) + pm["pt_blit_1.packed"]]
        BAND = (seg << 4) + pm["pt_fillband"]

        def zoom_counting(label):
            """A title double-click as raw packets, counting the decoder.
            os88mouse proves each edge by polling, which a guest stopped at a
            breakpoint never reaches - so the helper parks the pointer and the
            packets go in by hand."""
            wr = dispcp.win_rect(m, S, pw)
            mo.to(wr[0] + 60, wr[1] + 9)
            os88marty.settle(m)
            m.bp_exec(BAND, *DEC)
            n = nf = 0
            for lvl in (True, False, True, False):
                m.mouse(dx=0, dy=0, l=lvl)
                end = m.status()["cycles"] + int(0.05 * HZ)
                while m.status()["cycles"] < end:
                    if m.status()["state"] != "running":
                        r = m.regs()
                        if (r["cs"] << 4) + r["ip"] == BAND:
                            nf += 1
                        else:
                            n += 1
                        m.run()
                    else:
                        m.advance(frames=3)
                        m.run()
            end = m.status()["cycles"] + int(30 * HZ)
            m.run()
            while m.status()["cycles"] < end:
                if m.status()["state"] != "running":
                    r = m.regs()
                    if (r["cs"] << 4) + r["ip"] == BAND:
                        nf += 1
                    else:
                        n += 1
                    m.run()
                else:
                    m.advance(frames=8)
                    m.run()
            m.breakpoints([])
            m.run()
            os88marty.settle(m)
            moved = dispcp.win_rect(m, S, pw)
            print("   %-16s %3d DECODED, %2d bands   %r -> %r   %s"
                  % (label, n, nf, wr[:2], moved[:2], _table(m, seg)))
            if moved == wr:
                fails.append("SETUP: %s did not change the window, so this "
                             "run proves nothing" % label)
            return n, nf

        print()
        n, _ = zoom_counting("BLANK maximize")
        if n:
            fails.append("a blank canvas reached the decoder %d times - "
                         "SPEC.md 42.15" % n)
        zoom_counting("BLANK restore")

        # --- ink, then the same again -----------------------------------------
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        mo.to(cx0 + 40, cy0 + 40)
        os88marty.settle(m)
        mo._edge(True)
        for (tx, ty) in ((cx0 + 200, cy0 + 60), (cx0 + 90, cy0 + 150)):
            mo.to(tx, ty, l=True)
        mo._edge(False)
        os88marty.settle(m)
        if not _marked(m, seg):
            fails.append("drawing marked no band of the inked table - "
                         "SPEC.md 42.18")
        n, nf = zoom_counting("INKED maximize")
        mo.to(4, 4)
        os88marty.settle(m)
        gx, gy = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        fw, fh, fb = m.fbuf(card=0)
        grown = sum(1 for yy in range(gy + 20, min(gy + 220, fh))
                    for xx in range(gx + 20, min(gx + 260, fw))
                    if min(fb[(yy * fw + xx) * 3:(yy * fw + xx) * 3 + 3]) < 250)
        print("   the GROWN canvas shows %d non-ground pixels" % grown)
        if grown < 200:
            fails.append("the stroke is not on the glass after the canvas GREW "
                         "(%d pixels) - the band split painted over it, "
                         "SPEC.md 42.15.0" % grown)
        if not nf:
            fails.append("an inked canvas GROWN wider filled no bands, so the "
                         "new columns were decoded a pixel at a time - "
                         "SPEC.md 42.15.0")
        zoom_counting("INKED restore")

        # --- TRUTH: the glass against a repaint out of the canvas -------------
        mo.to(4, 4)
        os88marty.settle(m)
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        glass = _hash(m, cx0 + 20, cy0 + 20, 240, 200)
        fw, fh, fb = m.fbuf(card=0)
        ink = sum(1 for yy in range(cy0 + 20, min(cy0 + 220, fh))
                  for xx in range(cx0 + 20, min(cx0 + 260, fw))
                  if min(fb[(yy * fw + xx) * 3:(yy * fw + xx) * 3 + 3]) < 250)
        print("   the canvas shows %d non-ground pixels after the round trip"
              % ink)
        if ink < 200:
            fails.append("the stroke is not on the glass after a maximize and "
                         "a restore (%d pixels) - a blank flag that is not "
                         "true (SPEC.md 42.15.3)" % ink)
        m.ctrl("KeyZ")
        os88marty.settle(m)
        m.advance(frames=90)
        m.run()
        m.ctrl("KeyZ")
        os88marty.settle(m)
        m.advance(frames=90)
        m.run()
        mo.to(4, 4)
        os88marty.settle(m)
        redone = _hash(m, cx0 + 20, cy0 + 20, 240, 200)
        print("   glass %s  after undo+redo %s" % (glass, redone))
        if glass != redone:
            fails.append("the picture does not survive undo and redo, which "
                         "repaint OUT OF the canvas - a flag saying blank "
                         "when it is not (SPEC.md 42.15.3)")

    print()
    if fails:
        for f in fails:
            print("   FAIL: %s" % f)
        return 1
    print("paintblank: PASS - a blank canvas never reaches the decoder, a"
          " grown one\n  fills its new bands instead of decoding them, and the"
          " stroke survives both")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
