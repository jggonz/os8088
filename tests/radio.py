#!/usr/bin/env python3
"""DOES SPEC.md 13.17's RADIO GROUP DRAW A RADIO, AND ANSWER THREE THINGS?

    make && make radtest && python3 tests/radio.py

`os88ui_rad` exists because a radio is not a check box, and BOTH halves of
that are testable rather than assertable by inspection:

  - THE SHAPE. 13.17.1's whole argument is that the mark must not be a square
    (os88ui_chk's already is) and must not be a thin diagonal or a dither
    (SPEC.md 39.4 - grey rounds to black on 1bpp, so a ring drawn either way
    reads as dotted or as noise). So this reads the FRAMEBUFFER and checks the
    12x12 box has ink along all four edges and NO INK IN ANY CORNER, which is
    the difference between this control and a rectangle stated as pixels. It
    runs on HERCULES for that reason: 1bpp is the machine the shape rule is
    about, and a VGA pass would prove nothing about it.

  - THE THREE ANSWERS (13.17.3). CF = 1 not ours; CF = 0 with ZF = 1 the pick
    MOVED; CF = 0 with ZF = 0 swallowed. The third is the one os88ui_chkhit
    does not have and the one a Control Panel needs, because re-applying a
    mode the machine is already in is not free. tests/radtest counts the three
    separately, so a press that lands in the wrong bucket cannot hide in a
    total.

  - AND THAT A PICK REPAINTS TWO ROWS, NOT THE GROUP. [rt_paints] counts
    whole-group paints. It is the reason the control holds its own SEL: a
    five-row group repainted to move one dot is tens of milliseconds on the
    field machine (PERFORMANCE.md Part 2, ~756us a drawing call).

WHAT IT WOULD CATCH, all verified red by breaking the source on purpose:
a ring drawn with UI_FRAME (corners light); a press on the already-picked row
counted as a move; a greyed row accepting a press; radhit repainting the group.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, os88geom, os88build, dispcp   # noqa: E402
from cycweb import Pkg, u16, shot                               # noqa: E402

BOX = 12                        # OS88UI_RDBOX
FAIL = []


def done(m, tr, cond, what):
    """Stay in a trace until the handler a gesture started has RETURNED:
    `cond` says it ran, and a free gfx lock says it has let go - ui_task
    holds the lock for the whole of a package callback."""
    lock = os88sym.linear("gfx_lock_flag")
    tr.until(lambda: cond() and m.read(lock, 1)[0] == 0,
             what, 30, required=False)


def check(ok, what):
    print("   %-52s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def pkg_syms(src, incs=("apps/",)):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in incs:
            cmd += ["-I", i]
        cmd += ["-o", bp, cp]
        subprocess.run(cmd, check=True)
        syms = {}
        for ln in open(mp):
            f = ln.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                syms[f[2]] = int(f[0], 16)
        return syms, open(bp, "rb").read()


def ring(px, W, x, y):
    """The 12x12 box at (x,y) as a set of lit offsets, ink = 1.

    HERCULES DRAWS BLACK ON WHITE, so the package's ink is the UNLIT pixel and
    the window's ground is the lit one. Inverting here rather than at each
    assertion keeps every test below reading as "ink", which is what 13.17.1
    is written in.
    """
    return [[0 if px[(y + r) * W + (x + c)] else 1 for c in range(BOX)]
            for r in range(BOX)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/radtest360.img")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    S = os88sym.linear

    syms, image = pkg_syms("tests/radtest/radtest.asm")
    # THE DISK MUST CARRY WHAT THE SYMBOLS DESCRIBE. `make` does not build
    # radtest - it has its own target - so editing os88ui.inc and re-running
    # this reads a FRESH symbol table against a STALE image, and every bss
    # offset is wrong by however much the library moved. It does not look like
    # that: the pixels are all correct (they come off the screen) and only the
    # counters are nonsense, so it reads as the press half being broken. It
    # cost one wrong diagnosis; os88sym refuses the same way for the kernel.
    try:
        built = open(os88build.at("build/radtest.bin"), "rb").read()
    except OSError:
        sys.exit("radio: no build/radtest.bin - run `make radtest`")
    if built != image:
        sys.exit("radio: build/radtest.bin is %d bytes and the source "
                 "assembles to %d - the disk is BEHIND THE TREE. "
                 "Run `make radtest`." % (len(built), len(image)))
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "RADTEST.O88")))
        mo.dblclick(rx, ry)
        seg = None

        def launched(_):
            nonlocal seg
            for w in os88geom.windows(m, S):
                if w.title.startswith("Radio"):
                    seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                     + os88geom.W_SEG, 2))
            return bool(seg)
        try:
            os88marty.until(m, launched, "RADTEST's window", poll=0.3,
                            limit=120)
        except os88marty.MartyError:
            pass                        # ...and the next line says so
        if not seg:
            sys.exit("radio: RADTEST did not launch")
        p = Pkg(m, seg, syms)
        mo.to(4, 4)             # the arrow off the control: it is somebody
        os88marty.settle(m)     # else's pixels wherever the last click left it

        # --- the geometry, out of the guest's own record ---------------------
        ax = u16(m.read(p.addr("rt_a") + 0, 2))
        ay = u16(m.read(p.addr("rt_a") + 2, 2))
        pitch = u16(m.read(p.addr("rt_a") + 14, 2))
        # THE RING IS CENTRED IN ITS ROW (SPEC.md 13.17.1): (PITCH - 12) >> 1
        # below the row's top, clamped at zero - read it where os88ui_radboxy
        # puts it, and assert the centring itself in step 1 so the ring and
        # the label cannot drift apart again
        ry = ay + max(0, (pitch - BOX) // 2)
        W, H, px = shot(m)
        print("   group A at (%d,%d) pitch %d, screen %dx%d"
              % (ax, ay, pitch, W, H))

        # --- 1. THE SHAPE (13.17.1) ------------------------------------------
        r = ring(px, W, ax, ry)
        corners = [r[0][0], r[0][BOX - 1], r[BOX - 1][0], r[BOX - 1][BOX - 1]]
        check(not any(corners),
              "all four corners CLEAR - it is not a rectangle")
        check(all(r[0][c] for c in range(1, BOX - 1)), "top run is ink")
        check(all(r[BOX - 1][c] for c in range(1, BOX - 1)),
              "bottom run is ink")
        check(all(r[q][0] for q in range(1, BOX - 1)), "left run is ink")
        check(all(r[q][BOX - 1] for q in range(1, BOX - 1)),
              "right run is ink")
        # ...and it is CENTRED: the ring's middle row and the label's agree to
        # the half pixel (the label sits (pitch - 7) >> 1 below the row's top)
        ring_mid = (ry - ay) * 2 + BOX - 1
        label_mid = ((pitch - 7) // 2) * 2 + 7 - 1
        check(abs(ring_mid - label_mid) <= 1,
              "ring centred on its label at pitch %d (ring mid %.1f, label "
              "mid %.1f)" % (pitch, ring_mid / 2, label_mid / 2))
        # ...and the dot, on the row that IS the pick (SEL starts at 0)
        dot = sum(r[q][c] for q in range(3, 9) for c in range(3, 9))
        check(dot == 32, "the picked row's dot is a 6x6 with cut corners "
                         "(%d of 32 px)" % dot)
        # the row BELOW it is unpicked and must have an empty middle
        r1 = ring(px, W, ax, ry + pitch)
        blank = sum(r1[q][c] for q in range(3, 9) for c in range(3, 9))
        check(blank == 0, "the unpicked row's middle is EMPTY (%d px)" % blank)

        # --- 2. A PRESS MOVES THE PICK, AND REPAINTS TWO ROWS ----------------
        paints = p.rw("rt_paints")
        mo.click(ax + 4, ay + pitch + 4)
        os88marty.settle(m)
        check(p.rw("rt_moved") == 1, "press on row 1: ONE move counted")
        check(u16(m.read(p.addr("rt_a") + 12, 2)) == 1, "...and SEL is 1")
        check(p.rw("rt_paints") == paints,
              "...and the GROUP was not repainted (%d paints, was %d)"
              % (p.rw("rt_paints"), paints))
        mo.to(4, 4)             # OFF THE CONTROL BEFORE READING PIXELS. The
        os88marty.settle(m)     # click leaves the kernel's 8x12 arrow sitting
                                # on the dot it just set, and a capture then
                                # reads the pointer's pixels as the control's -
                                # which failed as "row 1 never got the dot",
                                # pointing at the library rather than at the
                                # harness (docs/TESTING.md's standing trap)
        W, H, px = shot(m)
        r0 = ring(px, W, ax, ry)
        r1 = ring(px, W, ax, ry + pitch)
        check(sum(r0[q][c] for q in range(3, 9) for c in range(3, 9)) == 0,
              "...row 0 gave the dot up")
        check(sum(r1[q][c] for q in range(3, 9) for c in range(3, 9)) == 32,
              "...and row 1 has it")

        # --- 3. THE SAME ROW AGAIN IS SWALLOWED (13.17.3) --------------------
        sw = p.rw("rt_swall")
        mo.click(ax + 4, ay + pitch + 4)
        os88marty.settle(m)
        check(p.rw("rt_swall") == sw + 1,
              "press on the row already picked: SWALLOWED")
        check(p.rw("rt_moved") == 1, "...and not counted as a move")

        # --- 4. A GREYED ROW IS SWALLOWED TOO (SPEC.md 47 rule 2) ------------
        by_ = u16(m.read(p.addr("rt_b") + 2, 2))
        pitch2 = u16(m.read(p.addr("rt_b") + 14, 2))
        sw = p.rw("rt_swall")
        mo.click(ax + 4, by_ + pitch2 + 4)
        os88marty.settle(m)
        check(p.rw("rt_swall") == sw + 1, "press on the GREYED row: swallowed")
        check(u16(m.read(p.addr("rt_b") + 12, 2)) == 0,
              "...and group B's pick did not move")

        # --- 5. NOTHING IS BLANKED, AND NOTHING IS DRAWN TWICE (13.17.4) -----
        # THE ONE ASSERTION THAT WOULD HAVE CAUGHT THE FIRST VERSION. It filled
        # the whole row white and then drew the ring, the dot and the label on
        # top - five drawing calls later, which on the field machine is about
        # four milliseconds of blank row, every repaint. A pixel comparison
        # cannot see that: the FINAL frame is identical either way, which is
        # PERFORMANCE.md Part 1's "invisible in an emulator" exactly.
        #
        # So this reads the CALLS instead. Every gfx_fill one full os88ui_rad
        # makes is recorded with its rect; the ring and the dot both live inside
        # the 12x12 box, so a fill wider than the box is a fill that spans the
        # label, and there is no legitimate one.
        #
        # bp_trace AND NOT bp_exec, which is not a style choice: a plain
        # breakpoint STOPS the guest, and a stop landing inside the 1200-baud
        # mouse packet the click is still sending loses the release - the row
        # then dies in os88mouse saying so, pointing at the harness. bp_trace
        # pumps the stops from a daemon and leaves the guest running.
        with os88marty.bp_trace(m, "gfx_fill", regs=True) as tr:
            m.key("KeyA")               # rt_onkey redraws group A in place
            done(m, tr, lambda: tr.n > 0, "group A's redraw")
        wide = [(h["regs"]["ax"], h["regs"]["bx"],
                 h["regs"]["cx"] - h["regs"]["ax"] + 1)
                for h in tr.hits
                if h["regs"]["cx"] - h["regs"]["ax"] + 1 > BOX]
        print("   %d gfx_fill(s) in one full paint (3 radio rows + a check)"
              % tr.n)
        check(tr.n >= 12, "the key reached the controls at all (%d fills)"
              % tr.n)
        check(not wide, "no fill is wider than the 12px box - NEITHER control "
                        "lays a ground (%s)" % (wide[:2] if wide else "none"))

        # --- 6. A PICK DOES NOT RE-LETTER EITHER ROW -------------------------
        # font_run_x is the only way a PACKAGE's label reaches the screen, and
        # the `_x` matters: slot 0x01E5's cell names font_run_x, so a
        # breakpoint on font_run is one a package never reaches. This assertion
        # was a FALSE GREEN on that symbol until the deliberate breakage below
        # refused to go red - docs/WRITING-TESTS.md 1 working as advertised.
        #
        # The menu bar's clock draws through font_run_x too, which is why the
        # hits are filtered by y rather than simply counted.
        ay0 = u16(m.read(p.addr("rt_a") + 2, 2))
        ay1 = ay0 + 3 * pitch
        with os88marty.bp_trace(m, "font_run_x", regs=True) as tr:
            mo.click(ax + 4, ay + 4)    # back to row 0: a real move
            done(m, tr, lambda: u16(m.read(p.addr("rt_a") + 12, 2)) == 0,
                 "the pick back to row 0")
        inside = [(h["regs"]["cx"], h["regs"]["dx"]) for h in tr.hits
                  if ay0 <= h["regs"]["dx"] < ay1]
        check(u16(m.read(p.addr("rt_a") + 12, 2)) == 0,
              "the pick moved back to row 0")
        check(not inside, "...and NO font_run_x landed in the group: the "
                          "labels were not re-drawn (%s)"
                          % (inside[:2] if inside else "none"))

        # --- 7. THE CHECK BOX, on the SAME two rules (13.15.2) ---------------
        # It had NO gate in the tree at all before this, which is how it kept
        # both defects long enough for os88ui_rad to be written by copying it.
        cx0 = u16(m.read(p.addr("rt_c") + 0, 2))
        cy0 = u16(m.read(p.addr("rt_c") + 2, 2))
        cy1 = u16(m.read(p.addr("rt_c") + 6, 2))
        with os88marty.bp_trace(m, "gfx_fill", "font_run_x", regs=True) as tr:
            mo.click(cx0 + 4, (cy0 + cy1) // 2)
            done(m, tr, lambda: p.rw("rt_ctog") == 1, "the check box's toggle")
        mo.to(4, 4)
        os88marty.settle(m)
        check(p.rw("rt_ctog") == 1, "a press on the check box toggled it")
        check(m.read(p.addr("rt_c") + 10, 1)[0] == 1, "...and CK_ON is set")
        # ...the same two questions the radio answers, asked of the toggle
        fills = [(h["regs"]["ax"], h["regs"]["cx"] - h["regs"]["ax"] + 1)
                 for h in tr.hits if h.get("regs")
                 and cy0 <= h["regs"]["dx"] <= cy1]
        widest = max((w for _, w in fills), default=0)
        check(0 < widest <= BOX,
              "the toggle blanked nothing - widest fill %d px, box is %d"
              % (widest, BOX))
        # RULE 2 needs font_run_x and NOT the fill widths: os88ui_chk draws its
        # frame with gfx_frame, so a toggle that redrew the whole control would
        # still show only a 6px fill. The label is the tell.
        clet = [h["regs"]["cx"] for h in tr.hits if h.get("regs")
                and h["regs"]["dx"] >= cy0 and h["regs"]["dx"] <= cy1
                and h["regs"]["cx"] > cx0 + BOX]
        check(not clet, "...and did not re-letter the label (%s)"
                        % (clet[:2] if clet else "none"))

        # --- 8. A PRESS OUTSIDE IS NOT OURS ----------------------------------
        mv, sw = p.rw("rt_moved"), p.rw("rt_swall")
        mo.click(ax + 4, ay - 4)
        os88marty.settle(m)
        check(p.rw("rt_moved") == mv and p.rw("rt_swall") == sw,
              "a press above the group is NOT OURS (CF = 1)")

    print("radio: %s" % ("FAILED - " + "; ".join(FAIL) if FAIL else "ok"))
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
