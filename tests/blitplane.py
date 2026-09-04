#!/usr/bin/env python3
"""DOES SPEC.md 5.4.1.3's PLANAR DECODER DRAW THE SAME PIXELS - and how much
faster?

    make && python3 tests/blitplane.py

Two runs of one script, one kernel each: the shipped one, and `NOPLANE=1`,
which takes the decoder out and sends every run of a VGA blit back to
vga_blit_span. Both must produce a framebuffer that is IDENTICAL to the
pixel, and the shipped one must be several times quicker.

WHY OS8088.GIF AND NOT A BLANK CANVAS. gfx_blit4 is priced per RUN on the
span writer and per PIXEL on the decoder, so a picture that coalesces well
cannot tell the two apart: a blank canvas is one run a row and stays on the
span writer by design (5.4.1.3's row triage), and a test written against one
would pass with the decoder deleted. The logo's ground is SPEC.md 63's 50%
dither - 18,978 runs in 51,260 pixels - which is the case the decoder exists
for and the case the field reported.

BOTH PHASES, because the decoder has two. An odd destination x leaves the
whole row one bit to the left of where it belongs and an `rcr` chain puts it
back; a run does not care. Paint's window cannot be DRAGGED onto the other
phase - every drag this harness can make lands it on x = 7 (mod 8), so the
canvas is always even - so W_X is written directly and the window raised,
which is the only way this side of the routine is entered at all.

AND BOTH PHASES SIT OFF THE BYTE GRID, which is not a detail: since SPEC.md
42.13 Paint's canvas is FOUR PLANES on a colour adapter and repaints through
gfx_blitp, so a window on the grid does not call gfx_blit4 AT ALL. The packed
path is still live and still the field's - pt_blit falls back to it whenever
gfx_blitp refuses, and an origin off the byte grid is one of the refusals -
so the window is put at x-1 (odd, and the refusal that converts the canvas
once) and then at x+2 (even, and still 2 mod 8 so the probe at SPEC.md
5.4.3.2 keeps saying no). Put it back on the grid between the two and
pt_toplanar undoes the whole thing.

WHAT THAT COST, because it is the reason this file was rewritten: for a
release after 42.13 landed, the even phase measured NOTHING. It did not fail
either. `_bracket` waited 300 wall seconds - about twenty guest minutes at
this host's rate - for a gfx_blit4 that was never coming, the idle screen
saver came up at five, the breakpoint caught one of ITS shapes (16x8 at
(162,377)), and the settle after that could never return because the saver
animates. The reported error was "the machine never finished booting". The
bound in `_bracket` is under the saver's idle period now, so a scene that
stops reaching the primitive says so in 45 seconds instead.

The apps disk is built here for the same reason tests/paintgif.py builds its
own: the Open dialog then comes up on B:\\MEDIA with the GIF the only thing in
it, and the association (SPEC.md 54) launches Paint with the picture already
decoded, so not one navigation click is inside the measurement.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
import dispapps                                              # noqa: E402


# SPEC.md 42.23.6's COLOUR fixture, at module scope because the helpers below
# read it and they are not nested inside main(). dispapps.colour_gif is itself
# cached on mtime, so naming it here costs one stat and not a rebuild.
def gifpath():
    return dispapps.colour_gif()


def gifname():
    return os.path.basename(gifpath())
from os88geom import TITLE_H                                  # noqa: E402

MIN_GAIN = 3.0                  # measured 6.2x even / 4.9x odd (Set 107).
                                # Three is the floor a REGRESSION has to break
                                # through, not the figure to quote


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def shots(image, apps, machine, defines):
    """(even-phase frame, odd-phase frame, even cycles, odd cycles)."""
    os88sym.default_defines(*defines)
    S = os88sym.linear
    kbase = os88sym.KERNEL_SEG << 4
    out = []
    with os88marty.launch(image, apps=apps, machine=machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        # SPEC.md 79's SAVER IS LEFT ON, DELIBERATELY. It came up in the
        # middle of this gate once and broke it twice over - it ANIMATES, so
        # the settles waited out their whole limit, and it BLITS, so the
        # gfx_blit4 breakpoint caught a wandering saver shape (16x8 at
        # (162,377)) instead of the 466x110 canvas. os88marty.no_saver(m)
        # would have hidden both.
        #
        # But the saver was the SYMPTOM. Five guest minutes of no input is not
        # something a scripted session should be able to reach, and this one
        # reached it in `_bracket`, which waited 300 WALL seconds - about
        # twenty guest minutes here - for a blit that could never come once
        # SPEC.md 42.13 made Paint's canvas planar. The bound there is the fix
        # and it is under the idle period on purpose; leaving the saver armed
        # is what keeps this gate able to notice if that ever stops being true.
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
                                                              gifname())))
        mo.to(rx, ry)
        os88marty.settle(m)

        # --- open it, and DO NOT measure this repaint (SPEC.md 42.13.1) ------
        # Paint's canvas is FOUR PLANES now, and a planar canvas repaints
        # through OSAPI_GFX_BLITP - so on a VGA machine with the window on the
        # byte grid, opening a picture does not call gfx_blit4 at all and this
        # gate measured nothing. It did not fail, either: it stopped at
        # whatever gfx_blit4 ran next, which for a while was the SCREEN SAVER's
        # shapes.
        #
        # The packed path is still live and still the field's - pt_blit falls
        # back to it whenever gfx_blitp refuses (a 1bpp adapter, an origin off
        # the byte grid, a straddled seam), and pt_topacked then converts the
        # canvas ONCE and every repaint after it is an OSAPI_GFX_BLIT4. So the
        # ODD phase is measured FIRST: nudging W_X off the byte grid is what
        # provokes the refusal, so it both selects the rcr pass and is what
        # puts Paint into the mode the rest of this gate needs.
        mo.dblclick(rx, ry)
        time.sleep(6)
        os88marty.settle(m)
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        px, py, pwid, _ = dispcp.win_rect(m, S, pw)
        dx, dy = dispcp.win_rect(m, S, disk)[:2]

        def repaint_at(x):
            """Put the window at x and force a full canvas repaint there."""
            m.pause()
            m.write(S("wm_wins") + pw * dispcp.WIN_SIZE + 2,
                    x.to_bytes(2, "little"))
            m.run()
            mo.click(dx + 60, dy + 9)                # behind the disk window
            time.sleep(4)
            m.bp_exec("gfx_blit4")
            mo.click(x + pwid // 2, py + TITLE_H // 2)      # ...and in front
            cyc, g = _bracket(m, kbase)
            m.bp_exec()
            m.run()
            os88marty.settle(m)
            return cyc, g, m.fbuf(card=0)[2]

        # --- one pixel over, which is the rcr pass AND the refusal ----------
        cyc_odd, geom2, f_odd = repaint_at(px - 1)
        # --- ...and an EVEN x that is still OFF THE BYTE GRID.
        # Not `px`, which is where this first went: pt_blit re-ASKS on every
        # repaint whether the planes would be taken (SPEC.md 5.4.3.2's probe),
        # so putting the window back on the grid calls pt_toplanar and the
        # canvas is four planes again - gfx_blit4 is then not called at all and
        # the phase measures nothing. px+2 is even, so the destination x is
        # even and this is the decoder's straight pass, and it is 2 (mod 8), so
        # the probe keeps saying no and the canvas stays packed.
        cyc_even, geom, f_even = repaint_at(px + 2)
    return f_even, f_odd, cyc_even, cyc_odd, geom, geom2


def _bracket(m, kbase):
    """Time the next gfx_blit4 from its entry to its RETURN.

    The return address is read off the stack rather than assumed: gfx_blit4
    has no exit symbol of its own, and a settle-shaped measurement would be
    reporting the harness's quiet window rather than the primitive.
    """
    # THE LIMIT IS UNDER THE SCREEN SAVER'S IDLE PERIOD, and that is the
    # point of the number. The blit being waited for happens within a couple
    # of guest seconds of the click; a limit long enough to sit through five
    # GUEST minutes with no input is not patience, it is a way of turning "the
    # caller no longer calls this primitive" into a twenty-minute free run.
    # That is exactly what happened when SPEC.md 42.13 made Paint's canvas
    # planar: 300s here is ~20 guest minutes at this host's rate, the saver
    # came up at five, and the breakpoint finally caught one of ITS shapes -
    # so the gate reported a 16x8 blit at (162,377) and then waited out a
    # settle that could never return, and none of it named the cause.
    if not m.wait_stop(limit=45.0):
        sys.exit("blitplane: gfx_blit4 never ran within 45s of the click.\n"
                 "  The primitive is not being reached at all - check that "
                 "Paint's canvas is still PACKED here (SPEC.md 42.13.1: a "
                 "planar canvas repaints through gfx_blitp instead), which is "
                 "what putting the window off the byte grid is for.")
    r = m.regs()
    ret = u16(m.read((r["ss"] << 4) + r["sp"], 2))
    print("      blit x=%d y=%d w=%d h=%d" % (r["ax"], r["bx"], r["cx"],
                                              r["dx"]))
    m.bp_exec(kbase + ret)
    c0 = m.status()["cycles"]
    m.run()
    if not m.wait_stop(limit=120.0):
        sys.exit("blitplane: gfx_blit4 never returned")
    return (m.status()["cycles"] - c0,
            (r["ax"], r["bx"], r["cx"], r["dx"]))


def canvas_diff(a, b, geom, w=640):
    """Differing pixels INSIDE the blitted rect, and their bounding box.

    The rect and not the screen: the menu bar carries a running clock and the
    pointer is drawn by the mouse ISR, so a whole-screen compare of two boots
    is asking two machines to agree about the time. What is under test is the
    block gfx_blit4 was handed, and the bracket above says exactly which one
    that was.
    """
    x, y, bw, bh = geom
    n = 0
    box = None
    for row in range(y, y + bh):
        base = (row * w + x) * 3
        if a[base:base + bw * 3] == b[base:base + bw * 3]:
            continue
        for col in range(x, x + bw):
            i = (row * w + col) * 3
            if a[i:i+3] != b[i:i+3]:
                n += 1
                box = ((min(box[0], col), min(box[1], row),
                        max(box[2], col), max(box[3], row))
                       if box else (col, row, col, row))
    return n, box


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/blitplane.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()

    # A COLOUR PICTURE, and it has to be said now: SPEC.md 42.23.6 opens a GIF
    # whose colour table has two entries ONE BIT DEEP on any adapter, and
    # build/OS8088.GIF has exactly two - so the fixture every picture row here
    # uses stopped being able to give this one a four-plane canvas.
    # dispapps.colour_gif appends two unused entries and changes not one
    # pixel, so every oracle below is the one it always was.
    gif = gifpath()

    if a.apps == "/tmp/blitplane.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + gif)

    print("   shipped kernel:")
    fe, fo, ce, co, ge, go = shots(a.image, a.apps, a.machine, ())

    # THIS ROW REBUILDS THE TREE, which no other one does, and the `finally`
    # is not decoration: `make NOPLANE=1` writes build/kernel.bin, so a run
    # that dies in the middle leaves the tree holding a kernel `make` did not
    # put there - and every emulator row after it would then be driving a
    # kernel its symbol map describes perfectly and nobody asked for. The
    # knob image is COPIED out before the tree is put back, so the two
    # captures are of two files rather than of one file twice.
    ref = "/tmp/blitplane-noplane.img"
    print("   NOPLANE=1 kernel: building")
    try:
        subprocess.check_call(["make", "NOPLANE=1", a.image],
                              stdout=subprocess.DEVNULL)
        subprocess.check_call(["cp", a.image, ref])
        print("   NOPLANE=1 kernel:")
        ne, no, rce, rco, nge, ngo = shots(ref, a.apps, a.machine,
                                           ("NOPLANE",))
    finally:
        subprocess.check_call(["make"], stdout=subprocess.DEVNULL)

    bad = 0
    for name, x, y, g, ng in (("even", fe, ne, ge, nge),
                              ("odd", fo, no, go, ngo)):
        if g != ng:
            print("   %-4s phase: the two kernels blitted DIFFERENT rects, "
                  "%r against %r" % (name, g, ng))
            bad += 1
            continue
        d, box = canvas_diff(x, y, g)
        print("   %-4s phase: %d differing pixels in the %dx%d canvas%s"
              % (name, d, g[2], g[3], "" if not box else " - box %r" % (box,)))
        bad += d
    for name, mine, theirs in (("even", ce, rce), ("odd", co, rco)):
        gain = theirs / float(mine)
        print("   %-4s phase: %d cycles against %d - %.2fx"
              % (name, mine, theirs, gain))
        if gain < MIN_GAIN:
            print("blitplane: %s phase is only %.2fx (floor %.1f)"
                  % (name, gain, MIN_GAIN))
            bad += 1
    if bad:
        sys.exit("blitplane: FAILED")
    print("blitplane: identical pixels, and %.2fx / %.2fx"
          % (rce / float(ce), rco / float(co)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
