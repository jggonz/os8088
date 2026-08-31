#!/usr/bin/env python3
"""DOES A RESIZE STILL HAVE THE PICTURE AFTERWARDS? (SPEC.md 42.19.1)

    make && python3 tests/paintrz.py [--machine os8088_xt_vga]

pt_resize used to carry the picture through the undo image: a full copy out, a
wipe, a full copy back. Nothing about that could get the ORDER wrong, because
the source was a buffer of its own and every order is safe there. §42.19.1
moves the picture where it lies, and the order is now the whole correctness
argument - which walk end leads, which plane in a row leads, and which way each
`movs` runs.

**THE FIRST BUILD HAD IT INVERTED AND EVERY EXISTING TEST PASSED**, because
they all resize in the one direction the wrong answer happens to survive. What
it did was wipe the picture from about the middle of the canvas down.

So this row asks one question after each of five resizes: is every pixel that
should still exist exactly where the arithmetic says? The ink is read out of
the CANVAS, not off the glass, so a repaint cannot flatter it, and the oracle
is the set of inked pixels from before the resize clipped to the new size -
not a bounding box, which a smear inside the box would pass.

  width  DOWN  the stride shrinks: the rows pack together, forward
  width  UP    the stride grows:   the rows spread apart, BACKWARD
  height DOWN  the picture slides down the file, forward
  height UP    the picture slides up the file, BACKWARD
  both, by dragging the grow box diagonally - the case §42.19.1's two passes
        exist for, where one axis grows while the other shrinks and a single
        walk has no safe order

Both storage formats are covered by the machine: os8088_xt_vga gives Paint a
four-plane canvas and a 1bpp machine gives it a packed one, and §42.13.2 is
the record of the planar half going wrong on its own.
"""
import argparse
import os
import subprocess
import sys
import tempfile

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
EQU = ["PT_SZ_Y", "PT_SZ_DY", "PT_SZ_BH", "PT_SZ_BX", "PT_SZ_BW"]
PT_BMPHDR = 118                 # 14 + 40 + 16*4, mirrored in apps/paint


def _equates(src="apps/paint/paint.asm", incs=("apps/",)):
    """A package's numeric `equ`s, by making nasm EMIT them (paintshrink's)."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        body = open(src).read() + "\nsection .text\n__equ_probe:\n"
        body += "".join("    dw %s\n" % n for n in EQU)
        open(cp, "w").write(body + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], []) + ["-o", bp, cp],
                       check=True, capture_output=True)
        off = None
        for line in open(mp):
            f = line.split()
            if len(f) >= 3 and f[-1] == "__equ_probe":
                off = int(f[0], 16)
        if off is None:
            sys.exit("paintrz: nasm emitted no __equ_probe")
        data = open(bp, "rb").read()
        return {n: int.from_bytes(data[off + i * 2:off + i * 2 + 2], "little")
                for i, n in enumerate(EQU)}


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name, w=2):
    return int.from_bytes(m.read(_boff(seg, name), w), "little")


def _ink(m, seg):
    """The set of inked canvas pixels, read out of the canvas itself.

    NOT white is the test, and white is 0xFF in both formats - a nibble of
    0x0F twice over packed, all four planes set planar - so neither the
    palette nor the plane ORDER has to be known here, only which bit or nibble
    a column falls in. **Whole bytes of 0xFF are skipped before any of that**:
    a pixel at a time over the debug socket is 125,440 of them on a 448x280
    canvas and the row never finishes.
    """
    w, h = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
    planar = _bss(m, seg, "pt_planar", 1)
    bpr, stride = _bss(m, seg, "pt_bpr"), _bss(m, seg, "pt_stride")
    lin = _bss(m, seg, "pt_base") << 4
    total = PT_BMPHDR + h * stride
    buf, got = bytearray(), 0
    while got < total:                          # the canvas can span segments,
        n = min(16384, total - got)             # so it is read as one linear
        buf += m.read(lin + got, n)             # run and sliced here
        got += n
    out = set()
    for row in range(h):
        o = PT_BMPHDR + (h - 1 - row) * stride
        if planar:
            for p in range(4):
                run = buf[o + p * bpr:o + (p + 1) * bpr]
                for i, b in enumerate(run):
                    if b == 0xFF:
                        continue
                    for k in range(8):
                        c = i * 8 + k
                        if c < w and not b & (0x80 >> k):
                            out.add((c, row))
        else:
            run = buf[o:o + (w + 1) // 2]
            for i, b in enumerate(run):
                if b == 0xFF:
                    continue
                if b >> 4 != 0x0F:
                    out.add((i * 2, row))
                if b & 0x0F != 0x0F and i * 2 + 1 < w:
                    out.add((i * 2 + 1, row))
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
    E = _equates()
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
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]

        # --- ink something with a KNOWN top-left corner and some extent -----
        # Two strokes, well apart, so a move that carries one block and drops
        # another is caught: a single blob near the origin survives almost any
        # ordering bug, which is exactly how the inverted walk got through.
        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        for (ox, oy) in ((30, 20), (150, 130)):
            mo.to(cx0 + ox, cy0 + oy)
            os88marty.settle(m)
            mo._edge(True)
            for _ in range(6):
                m.mouse(dx=5, dy=4, l=True)
                m.advance(frames=3)
                m.run()
            mo._edge(False)
            os88marty.settle(m)

        w0, h0 = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        ink = _ink(m, seg)
        print("   canvas %dx%d %s, %d inked pixels, x %d..%d y %d..%d"
              % (w0, h0, "planar" if _bss(m, seg, "pt_planar", 1) else "packed",
                 len(ink), min(p[0] for p in ink), max(p[0] for p in ink),
                 min(p[1] for p in ink), max(p[1] for p in ink)))
        if len(ink) < 40:
            fails.append("SETUP: only %d pixels were inked, so a resize that "
                         "lost the picture would look like one that kept it"
                         % len(ink))

        def apply(box, value):
            ox, oy = _bss(m, seg, "pt_ox"), _bss(m, seg, "pt_oy")
            bxm = ox + E["PT_SZ_BX"] + E["PT_SZ_BW"] // 2
            by = oy + E["PT_SZ_Y"] + E["PT_SZ_BH"] // 2
            if box:
                by += E["PT_SZ_DY"]
            mo.click(bxm, by)
            os88marty.settle(m)
            if _bss(m, seg, "pt_fbox", 1) != box + 1:
                return False
            for ch in str(value):
                m.key("Digit%s" % ch)
                m.advance(frames=2)
                m.run()
            m.key("Enter")
            m.advance(frames=900)
            m.run()
            os88marty.settle(m)
            return True

        def check(what, before):
            w, h = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
            want = set(p for p in before if p[0] < w and p[1] < h)
            got = _ink(m, seg)
            print("   %-22s -> %3dx%-3d  want %4d px, got %4d"
                  % (what, w, h, len(want), len(got)))
            if got != want:
                lost, extra = want - got, got - want
                fails.append(
                    "%s: %d of the %d pixels that should have survived are "
                    "gone and %d appeared that should not have (first lost %r, "
                    "first extra %r) - SPEC.md 42.19.1's walk order"
                    % (what, len(lost), len(want), len(extra),
                       min(lost) if lost else None,
                       min(extra) if extra else None))
            return got

        # --- the four single-axis walks, each through the size box ----------
        # THE WIDTHS ARE MULTIPLES OF EIGHT so that the window's own snap
        # (SPEC.md 11.94.5) does not resize a second time underneath the
        # assertion and turn one walk under test into two.
        for (nm, box, val) in (("width DOWN (forward)",  0, w0 - 144),
                               ("width UP (backward)",   0, w0 - 16),
                               ("height DOWN (forward)", 1, h0 - 60),
                               ("height UP (backward)",  1, h0 - 8)):
            if not apply(box, val):
                fails.append("SETUP: the %s box did not take focus" % nm)
                break
            ink = check(nm, ink)

        # --- and both at once, which is what the two passes are for ---------
        wb, hb = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        pw = dispcp.win_list(m, S)[-1]
        px, py, pww, pwh = dispcp.win_rect(m, S, pw)[:4]
        gx, gy = px + pww - 5, py + pwh - 5     # the grow box, inside the
                                                # bottom-right corner
        mo.to(gx, gy)
        os88marty.settle(m)
        mo._edge(True)
        for _ in range(10):
            m.mouse(dx=6, dy=-4, l=True)     # wider AND shorter, together
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        m.advance(frames=900)
        m.run()
        os88marty.settle(m)
        wa, ha = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        if wa == wb and ha == hb:
            print("   grow box drag moved neither axis (%dx%d) - the MIXED "
                  "case was not reached" % (wa, ha))
        elif (wa > wb) == (ha > hb):
            print("   grow box drag moved both axes the same way "
                  "(%dx%d -> %dx%d) - the MIXED case was not reached"
                  % (wb, hb, wa, ha))
        else:
            check("both axes, opposite ways", ink)

    if fails:
        for f in fails:
            print("paintrz: %s" % f)
        return 1
    print("paintrz: PASS - the picture survives a resize on both axes, in "
          "both directions, and with the two disagreeing")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
