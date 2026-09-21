#!/usr/bin/env python3
"""gfxpoints' own claim, across every EXTENDED-DESKTOP situation.

tests/gfxpoints.py asks the one question worth asking - does OSAPI_GFX_POINTS
draw the same pixels as a gfx_pixel loop - and asks it on one display.
SPEC.md 5.6.9.4 adds three paths that row cannot reach, so this asks it again
on a two-card machine, once per path:

    single        the control, and the arm gfxpoints already gates
    primary       [vid_ndisp] = 2, window on display 0  -> the hooked FAST arm
    secondary     window on display 1                   -> the TRANSLATED arm
    straddle      window across the seam                -> the per-point arm

Band A (gfx_points) and band B (one gfx_pixel a point) are laid down by the
package in ONE frame, so each arm is self-comparing: no golden image, no
reference build, and a wrong display or a wrong translation shows up as bands
that differ rather than as a screenshot somebody has to judge.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1) - and it did not have to be
staged, because this row CAUGHT THE DESIGN'S OWN BUG. SPEC.md 39.3.1 has
vid_ctx_act CLEAR [vid_rowmax] on a non-primary display, so every point there
takes gfx_points' "row past the table" path - and 5.6.9.5 made the loop's y
VIRTUAL without telling gfx_pt_row, which then asked gfx_rowbase_calc for a row
20 past the end of the CGA's aperture. The secondary arm went red on all three
cases (0 lit against 24, 12 and 24) with single, primary and straddle green,
which is exactly the shape a targeted row should have. `make test-fast` was
46/46 against that same broken kernel, and so was every other emulator row that
does not extend the desktop - which is this row's whole reason for existing.

The staged version is the same thing by hand: `nop` out GFXPT_LOOP's
`sub ax, [cs:gfx_pt_kx]` and the secondary arm goes red alone.

THE STRADDLING ARM READS A STITCHED DESKTOP and not one card. Each band is cut
by the seam, so neither framebuffer holds a whole one; the first shape of this
row compared the primary half of A against the primary half of B and then
indexed the secondary at a NEGATIVE x, which Python slices silently. `vrows`
is what puts the two cards back into the space the app drew in.
"""
import sys, os, time
# THIS TREE's root, DERIVED - never a hard-coded path, for tests/cycweb.py's
# reason: os88sym re-assembles ROOT/kernel/kernel.asm and compares it against
# ROOT/build/kernel.bin, so a literal answers about a DIFFERENT kernel.
_R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_R, "tools"))
sys.path.insert(0, os.path.join(_R, "tests"))
import os88marty, os88ui, os88mouse, os88sym, os88geom, dispcp
from gfxpoints import band, density, PAPER, PT_DY, PT_W, PT_H, PT_STEP, CASES

SYS = os.path.join(_R, "build/os8088-360.img")
APP = os.path.join(_R, "build/ptstest360.img")


VID_CTX_SZ = 16 * 2 + 5
VID_CTX_VX = 16 * 2
KIND = {0: "herc", 1: "cga"}        # this machine: Hercules primary, CGA second


def vrows(m, S):
    """The two cards STITCHED into the virtual desktop.

    A straddling window has part of each band on each card, so neither card's
    own framebuffer can answer "do band A and band B agree" - the first shape
    of this test read the primary half of A against the primary half of B and
    then indexed the secondary with a NEGATIVE x, which Python slices happily
    and silently. The bands are compared in the space the app drew them in.
    """
    _, _, ph = m.vram(kind=KIND[0])
    _, _, sh = m.vram(kind=KIND[1])
    a = m.read(S("vid_ctx") + VID_CTX_SZ + VID_CTX_VX, 4)
    ox, oy = a[0] | (a[1] << 8), a[2] | (a[3] << 8)
    w = ox + len(sh[0])
    h = max(len(ph), oy + len(sh))
    out = []
    for y in range(h):
        row = [0] * w
        if y < len(ph):
            row[:len(ph[0])] = ph[y]
        if oy <= y < oy + len(sh):
            row[ox:ox + len(sh[0])] = sh[y - oy]
        out.append(row)
    return out


def check(m, ui, label, card=None):
    """The four cases, on whichever card the window is on."""
    ui.settle()
    w = ui.window("PtsTest")
    cx, cy = w.content[0], w.content[1]
    ox, oy = 0, 0
    S = os88sym.linear
    nd = m.read(S("vid_ndisp"), 1)[0]
    if card == "virtual":
        rows = vrows(m, S)                  # the STITCHED desktop
    else:
        if card is not None:
            a = m.read(S("vid_ctx") + card * VID_CTX_SZ + VID_CTX_VX, 4)
            ox = a[0] | (a[1] << 8)
            oy = a[2] | (a[3] << 8)
        _, _, rows = m.vram(kind=KIND[card] if card is not None else None)
    bad = 0
    for n, name in enumerate(CASES):
        x0 = cx + 8 - ox
        ya = cy + 4 + n * PT_STEP - oy
        yb = ya + PT_DY
        try:
            A = band(rows, x0, ya, PT_W, PT_H)
            B = band(rows, x0, yb, PT_W, PT_H)
        except IndexError:
            print("  FAIL %-10s case %d: off the captured framebuffer" % (label, n + 1))
            bad += 1
            continue
        # the MINORITY pixels, which for case 4 are the dark ones: it draws
        # paper on a lit ground (SPEC.md 5.6.9.3.1), so counting lit ones
        # would read 2,136 of 2,160 and trip the "that is not a PATTERN" bound
        lit = density(A, PAPER[n])
        kind = "dark" if PAPER[n] else "lit"
        if A == B and 8 <= lit <= PT_W * PT_H // 4:
            print("  ok   %-10s case %d (%-22s) %4d %s agree"
                  % (label, n + 1, name, lit, kind))
        else:
            bad += 1
            d = sum(1 for ra, rb in zip(A, B) for va, vb in zip(ra, rb) if va != vb)
            print("  FAIL %-10s case %d (%-22s) %4d %s, %d differ"
                  % (label, n + 1, name, lit, kind, d))
    print("       (%s: vid_ndisp=%d card=%s origin=(%d,%d) window x=%d y=%d)"
          % (label, nd, card, ox, oy, w.x, w.y))
    return bad


def main():
    S = os88sym.linear
    bad = 0
    with os88ui.boot(SYS, apps=APP, machine="os8088_5150_both_gla_mono") as ui:
        m = ui.m
        ui.path("B:/PTSTEST.O88")
        ui.settle()
        mo = os88mouse.Mouse(marty=m)
        bad += check(m, ui, "single")

        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        time.sleep(2)
        w = ui.window("PtsTest")
        print("after extend: window at x=%d y=%d w=%d h=%d" % (w.x, w.y, w.w, w.h))
        bad += check(m, ui, "primary", card=0)

        prim_w = m.read(S("vid_cw"), 2)
        seam = prim_w[0] | (prim_w[1] << 8)
        print("seam at x=%d" % seam)

        ui.move_window("PtsTest", seam + 40, 20)
        time.sleep(2)
        bad += check(m, ui, "secondary", card=1)

        ui.move_window("PtsTest", seam - 60, 20)
        time.sleep(2)
        bad += check(m, ui, "straddle", card="virtual")

    if bad:
        print("\nptsext: %d of %d cases FAILED" % (bad, 4 * len(CASES)))
        return 1
    print("\nptsext: %d cases - gfx_points == a gfx_pixel loop on BOTH cards,"
          " hooked, translated and straddling" % (4 * len(CASES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
