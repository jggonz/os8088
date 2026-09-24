#!/usr/bin/env python3
"""PIXELSTEIN 3D on TWO DISPLAYS: a bracket changes its OWN card only
(SPEC.md 97.14, 53.7.1, 39.18; wave 5).

    python3 tests/pxsmd.py [--machine os8088_xt_vga_herc]

A 4.77 MHz XT with a VGA and a Hercules, the desktop extended RIGHT through
the Control Panel. PIXELSTEIN's bracket SETS A MODE (SPEC.md 53.7), so what
it may touch is decided twice: which modes the Mode row offers and which card
OSAPI_FSX_MODE programs. Both must be THE WINDOW'S DISPLAY - the depth off
OSAPI_WM_DISPLAY on this window, the kind and the modes off OSAPI_FSX_CAPS
with BX = this window (SPEC.md 39.18.2), and never OSAPI_VIDEO, which answers
about the primary (39.2.1) and on this machine is the VGA wherever the window
is. Pixelstein takes no drawing slot past OSAPI_FSX_MODE, so the rect a
same-mode bracket needs from OSAPI_FSX_SURF (53.7.1) is the mode's own screen
here - FSI's segment, on the card fsx_mode picked from [fsx_wdisp], the one
SURF would name - and the gate asserts the consequence rather than the call.

  (s) STATICALLY: the package asks OSAPI_VIDEO ONCE, in px_entry, for the
      window's first position - never in pxrast.inc or pxwin.inc, the
      bracket's and the window's code - and every OSAPI_FSX_CAPS is asked
      with BX = [px_win];
  (a) the window on the VGA: the Mode row is Mode X's (px_vkind VGA, px_bpp
      4, px_mode0 = Mode X); F takes a Mode X bracket, and WHILE IT IS UP
      the VGA's raster is Mode X's and the Hercules' mode and every byte of
      its framebuffer are what they were; Esc, and the Hercules' bytes are
      still the same and the VGA is back in 640 x 480;
  (b) the window dragged wholly onto the Hercules: the Mode row follows the
      display it is on (px_vkind Hercules, px_bpp 1, px_mode0 = Hercules)
      - px_adapter re-asked from the W_ONRESIZE the seam fires; F takes the
      HERCULES bracket (TANK's box), and WHILE IT IS UP the Hercules'
      framebuffer holds the game (its bytes moved) and the VGA's mode is
      the desktop's 640 x 480 still - the kernel darks it (vid_fsx_enter),
      it never programs it; Esc, and the Hercules is the desktop again and
      the VGA's mode unchanged.
  (c) (wave 6) in that Hercules bracket the MOUSE steers from the card's own
      middle: px_fmid is OSAPI_FSX_SURF's 720 / 2 = 360 and not OSAPI_VIDEO's
      640 / 2, a pointer resting there turns nothing for a second, and one
      120 dots right of it turns the view (the control).

QEMU cannot host this: it has one display (docs/TESTING.md's two-displays
row, "QEMU: no"), so the row is MartyPC's - the plan's "QEMU two-card" was
written before that was checked.
"""
import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import dispcp                                                   # noqa: E402
import os88marty                                                # noqa: E402
import os88mouse                                                # noqa: E402
import os88sym                                                  # noqa: E402
import pxslib                                                   # noqa: E402

FAIL = []
S = os88sym.linear
VID_VGA, VID_HERC = 0, 1                # the VID_* kinds
PXB_HERC, PXB_MODEX = 3, 4
SHOTS = os.path.join(ROOT, "build", "pxs-shots")


def check(ok, what):
    print("   %-72s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def static_leg():
    src = {}
    d = os.path.join(ROOT, "apps", "pixelstein")
    for f in sorted(os.listdir(d)):
        if f.endswith((".asm", ".inc")):
            src[f] = open(os.path.join(d, f)).read().splitlines()
    calls = [(f, i) for f, ls in src.items() for i, l in enumerate(ls)
             if re.match(r"\s*call\s+OSAPI_VIDEO\b", l)]
    where = []
    for f, i in calls:                  # the proc it is in: the last label
        for j in range(i, -1, -1):      # at column 0 above it
            m = re.match(r"([A-Za-z_][\w]*):", src[f][j])
            if m:
                where.append((f, m.group(1)))
                break
    print("   OSAPI_VIDEO is called at %s" % where)
    check(where == [("pxgame.asm", "px_entry")],
          "(s) OSAPI_VIDEO once, in px_entry, never in the bracket or the window")
    caps = [(f, i) for f, ls in src.items() for i, l in enumerate(ls)
            if re.match(r"\s*call\s+OSAPI_FSX_CAPS\b", l)]
    ok = all(any("mov bx, [px_win]" in src[f][k] for k in range(max(0, i - 3), i))
             for f, i in caps)
    check(caps and ok, "(s) every OSAPI_FSX_CAPS is asked with BX = [px_win] (%d call(s))"
          % len(caps))


def herc_bytes(m):
    return m.read(0xB0000, 4 * 0x2000)


def mode_of(m, card):
    v = m.video(card=card)
    return (str(v.get("mode")), v.get("field_w"), v.get("field_h"))


def shot(m, card, name):
    os.makedirs(SHOTS, exist_ok=True)
    m.pause()
    w, h, px = m.fbuf(card=card)
    os88marty.write_png_rgb(os.path.join(SHOTS, name), w, h, px)
    m.run()
    print("   shot", os.path.join("build/pxs-shots", name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    static_leg()
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine, boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("pxsmd: %s is not a two-card machine" % a.machine)
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("pxsmd: the Control Panel did not turn Extend on")
        g = pxslib.open_game(m)
        g.sim(False)
        g.god(True)
        g.pin(rung="flat", lowres=True, size=64)
        g.wait_frames(1)
        os88marty.settle(m)

        # --- (a) the window on the VGA: a Mode X bracket, the Hercules still ---
        print("   on the VGA: px_vkind %d, px_bpp %d, px_mode0 %d"
              % (g.byte("px_vkind"), g.byte("px_bpp"), g.byte("px_mode0")))
        check(g.byte("px_vkind") == VID_VGA and g.byte("px_bpp") == 4 and
              g.byte("px_mode0") == PXB_MODEX,
              "(a) on the VGA the Mode row is Mode X's, asked of THIS window")
        h0, hm0, vm0 = herc_bytes(m), mode_of(m, 1), mode_of(m, 0)
        g.enter_fsx()
        g.force()                           # a whole frame drawn in it (the
        g.wait_frames(1, limit=300.0)       # entry's own may be done already)
        os88marty.settle(m)
        h1, hm1, vm1 = herc_bytes(m), mode_of(m, 1), mode_of(m, 0)
        print("   in the bracket: backend %s; VGA %s -> %s; Hercules %s -> %s, %d byte(s) moved"
              % (pxslib.PXB.get(g.byte("px_back")), vm0, vm1, hm0, hm1,
                 sum(1 for x, y in zip(h0, h1) if x != y)))
        check(g.byte("px_back") == PXB_MODEX and vm1 != vm0,
              "(a) the bracket is Mode X, on the VGA (its raster changed)")
        check(hm1 == hm0 and h1 == h0,
              "(a) ...and the Hercules is untouched: its mode and every byte")
        shot(m, 0, "wave5-pxsmd-modex-on-vga.png")
        g.leave_fsx()
        os88marty.settle(m)
        h2, vm2 = herc_bytes(m), mode_of(m, 0)
        check(h2 == h0 and vm2 == vm0,
              "(a) back: the Hercules' bytes unchanged, the VGA in its desktop mode")

        # --- (b) the window on the Hercules: the Hercules bracket, the VGA still -
        x, y, ww, wh = dispcp.win_rect(m, S, g.win)
        gx, gy = x + ww // 2, y + 9
        mo.drag(gx, gy, gx + (640 + 64 - x), gy + (40 - y))
        mo.to(630, 30)
        os88marty.settle(m, card=1)
        g.force()
        g.wait_frames(1)
        print("   on the Hercules at %s: px_vkind %d, px_bpp %d, px_mode0 %d"
              % (dispcp.win_rect(m, S, g.win)[:2], g.byte("px_vkind"), g.byte("px_bpp"),
                 g.byte("px_mode0")))
        check(g.byte("px_vkind") == VID_HERC and g.byte("px_bpp") == 1 and
              g.byte("px_mode0") == PXB_HERC,
              "(b) on the Hercules the Mode row FOLLOWS: Hercules, 1 bpp")
        h0, hm0, vm0 = herc_bytes(m), mode_of(m, 1), mode_of(m, 0)
        g.enter_fsx()
        g.force()
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m, card=1)
        h1, hm1, vm1 = herc_bytes(m), mode_of(m, 1), mode_of(m, 0)
        moved = sum(1 for p, q in zip(h0, h1) if p != q)
        print("   in the bracket: backend %s; Hercules %s -> %s, %d byte(s) moved; VGA %s -> %s"
              % (pxslib.PXB.get(g.byte("px_back")), hm0, hm1, moved, vm0, vm1))
        check(g.byte("px_back") == PXB_HERC and moved > 1000,
              "(b) the bracket is the Hercules', on the Hercules (its bytes are the game)")
        check(vm1 == vm0, "(b) ...and the VGA is not programmed: its mode is the desktop's")
        shot(m, 1, "wave5-pxsmd-herc-bracket.png")
        # --- (c) THE MOUSE STEERS FROM THIS BRACKET'S MIDDLE (wave 6) --------
        # px_mouse_turn took the middle from OSAPI_VIDEO's width, which names
        # the PRIMARY: 640 / 2 = 320 here, 40 dots left of the Hercules' own
        # middle, so a pointer resting at the middle of the card the game is
        # on turned the view. The rect is OSAPI_FSX_SURF's (53.7.1), asked
        # once after the mode set (px_fmid)
        fm, sw = g.word("px_fmid"), g.word("px_scrw")
        print("   (c) px_fmid %d (the bracket's middle), OSAPI_VIDEO's width %d" % (fm, sw))
        check(fm == 360 and fm != sw // 2,
              "(c) the Hercules bracket's mouse middle is ITS card's: 720 / 2 = 360, "
              "not the primary's %d / 2" % sw)
        g.poke_byte("px_mouse", 1)
        mo.to(fm, 150)
        h0, t0 = g.word("px_head"), g.kticks()
        os88marty.until(m, lambda mm: (g.kticks() - t0) & 0xFFFF >= 18, "a second",
                        poll=0.2, limit=60.0)
        h1 = g.word("px_head")
        print("   (c) the pointer at x = %d for a second: heading %d -> %d" % (fm, h0, h1))
        check(h1 == h0, "(c) ...and a pointer resting there turns nothing (the "
              "unfixed middle read it 40 dots right: 10 units a tick)")
        mo.to(fm + 120, 150)
        h0, t0 = g.word("px_head"), g.kticks()
        os88marty.until(m, lambda mm: (g.kticks() - t0) & 0xFFFF >= 9, "half a second",
                        poll=0.2, limit=60.0)
        h1 = g.word("px_head")
        print("   (c) the pointer 120 dots right: heading %d -> %d" % (h0, h1))
        check(h1 != h0, "(c) ...while one 120 dots right of it turns the view (the "
              "negative control: the mouse IS steering)")
        mo.to(fm, 150)
        g.poke_byte("px_mouse", 0)
        g.leave_fsx()
        os88marty.settle(m, card=1)
        vm2 = mode_of(m, 0)
        check(vm2 == vm0 and g.byte("px_back") == 5,
              "(b) back: the window's WIN1 again, the VGA's mode unchanged")
    if FAIL:
        print("pxsmd: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsmd: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
