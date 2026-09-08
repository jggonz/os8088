#!/usr/bin/env python3
"""Does the Calculator re-fold cleanly when its box moves under it?
(SPEC.md 65.3, 11.98, 39.16.3)

    make && python3 tests/dispcalcx.py

TWO WAYS A BOX MOVES WITH NOBODY ASKING, and this drives both on a machine
that has a Hercules AND a CGA in it - which is the machine this project is
calibrated against (docs/FIELD-MACHINES.md).

  1. THE ADAPTER CHANGES. Activate Mode on the Control Panel's Display page
     re-fits every window on the screen at once (SPEC.md 39.11.2). A window
     that fitted 348 rows does not fit 200.

  2. THE WINDOW IS DRAGGED ACROSS THE SEAM. With the desktop extended, a
     720x348 Hercules beside a 640x200 CGA has rows 220..347 on the left and
     nothing at all on the right, so `wm_strad_fit` shortens a frame that
     reaches rows only one display has (SPEC.md 39.16.3).

Both leave [cal_nvis] - the one thing this app derives once and keeps -
describing a box it no longer has, and the gfx primitives clip to the SCREEN
rather than to a window (SPEC.md 11.3), so the surplus history rows would be
drawn straight through the bottom of the frame and onto the desktop. That is
what OSAPI_WM_ONRESIZE is for, and it is what this measures:

  * [cal_nvis] must equal what the LIVE content box holds, after each move;
  * the frame's last row must be one the display it is on actually has;
  * and a forced wm_paint_all must find nothing to correct - on BOTH cards,
    because "it drew past its own frame" shows up on the other monitor.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88sym, dispcp
from os88geom import MBAR_H     # the bar's height, from the one copy
                                # checked against kernel.asm at import

S = os88sym.linear
KERNEL_SEG = 0x0060
CAL_BASE_H, CAL_ROW_H, CAL_HIST_N = 118, 9, 8
TITLE_H = 18
VID_HERC, VID_CGA = 1, 2

fails = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def herc(m):
    w, h, rows = m.vram("herc")
    return w, h, bytes(b for r in rows for b in r)


def cga(m):
    w, h, rows = m.vram("cga")
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST repaint, and leave the machine paused - the routine in
    tests/dispcalc.py, whose header carries the reasoning (and the account of
    what parking the CPU on a stub cost before it)."""
    m.cmd(cmd="run")
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(200):
        time.sleep(0.05)
        if m.read(S("cp_dirty"), 1)[0] == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)
    m.cmd(cmd="pause")


def both_diff(m, what, corner, soft=False):
    """A full repaint, diffed on BOTH cards. The second one is the point:
    a window that draws past its own frame onto the other monitor is the
    exact failure this whole mechanism exists to prevent, and it is invisible
    on the card the window is on."""
    m.cmd(cmd="pause")
    bh, bc = herc(m), cga(m)
    full_repaint(m)
    ah, ac = herc(m), cga(m)
    out = []
    for nm, b, a in (("hercules", bh, ah), ("cga", bc, ac)):
        d = [i for i in range(len(b[2])) if b[2][i] != a[2][i]]
        d = [i for i in d if (i % b[0], i // b[0]) != corner]
        # **AND THE MENU BAR'S CLOCK, which is not this test's to compare.**
        # It changes once a guest MINUTE (SPEC.md 12.1), so a forced full
        # repaint that straddles a tick redraws it and the diff blames
        # whatever was on screen. tests/dispcalc.py has always dropped it and
        # this file never did: measured, 6 and 25 differing pixels at
        # x 704..710 y 6..12 on the Hercules - inside the clock band, which
        # starts at 512 on a 720-wide screen.
        #
        # [vid_clk_hx] is the live left edge of that band, so this follows
        # the adapter instead of assuming 640: it is
        # (vid_pw - MENU_CLK_W - 14) & ~7, recomputed by vid_disp_init.
        #
        # **AND IT IS THE PRIMARY'S, because the menu bar is** (SPEC.md
        # 39.2.1.2, and kernel/viddet.inc says so at the computation). The
        # secondary has no bar and no clock, so masking that band there would
        # hide real pixels in the top 20 rows of the other monitor - which is
        # exactly the failure this file exists to catch. [vid_pw] names the
        # primary's width and the two cards here differ by definition (720
        # against 640), so which card to mask is a fact rather than a guess.
        # No package can draw in the band on the card that HAS it - windows
        # clamp below the bar - so nothing real is hidden there.
        if b[0] == u16(m.read(S("vid_pw"), 2)):
            clk = u16(m.read(S("vid_clk_hx"), 2))
            d = [i for i in d if not (i // b[0] < MBAR_H and i % b[0] >= clk)]
        out.append(len(d))
        print("      %-9s %d differing of %d" % (nm, len(d), len(b[2])))
        if d:
            xs = [i % b[0] for i in d]
            ys = [i // b[0] for i in d]
            print("         bbox x %d..%d y %d..%d"
                  % (min(xs), max(xs), min(ys), max(ys)))
            if not soft:
                fails.append("%s: %d pixels on the %s a full repaint "
                             "disagrees with" % (what, len(d), nm))
    m.cmd(cmd="run")
    os88marty.settle(m)
    return out


def check(m, seg, cal, slot, what):
    """[cal_nvis] against what the live box actually holds."""
    x, y, w, h = dispcp.win_rect(m, S, slot)
    content_h = h - TITLE_H - 1
    nvis = u16(m.read(seg * 16 + cal["nvis"], 2))
    want = max(0, min(CAL_HIST_N, (content_h - CAL_BASE_H) // CAL_ROW_H))
    print("   %-26s frame (%d,%d) %dx%d -> %d rows of content; "
          "[cal_nvis] = %d, the box holds %d"
          % (what, x, y, w, h, content_h, nvis, want))
    if nvis != want:
        fails.append("%s: [cal_nvis] is %d but the box holds %d"
                     % (what, nvis, want))
    return x, y, w, h


PLAIN = {".": "Period", "/": "Slash", "-": "Minus", "=": "Equal",
         "+": "NumpadAdd"}


def typed(m, text):
    for ch in text:
        m.key(PLAIN[ch]) if ch in PLAIN else m.type_text(ch)
        time.sleep(0.15)
    os88marty.settle(m)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine="os8088_5150_both_gla") as m:
    mo = os88mouse.Mouse(marty=m)
    avail = m.read(S("vid_avail"), 1)[0]      # a BYTE: reading a word
    print("adapters available: %#04x (bit1 = Hercules, bit2 = CGA)"
          % avail)                            # picks up the next field

    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CALC.O88")
    time.sleep(2)
    slot = dispcp.win_list(m, S)[-1]
    r = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    seg = u16(r, 22)
    img_end = u16(m.read(seg * 16, 32), 8)
    cal = {"nvis": img_end + 8, "open": img_end + 22, "hn": img_end + 27}

    typed(m, "c8x8=")                       # something for the pane to show
    typed(m, "c7+7=")
    typed(m, "c9/2=")
    typed(m, "h")                           # ...and fold it down
    time.sleep(2)
    os88marty.settle(m)
    cx, cy, cw, ch = check(m, seg, cal, slot, "on the primary, folded down")
    both_diff(m, "folded down", (cx, cy + ch))

    # --- 1. the ADAPTER changes under it (SPEC.md 39.11.2) --------------------
    print("\n-- Activate Mode: the same window on the other adapter --")
    dispcp.open_panel(m, mo, S, os88marty.settle)
    row = dispcp.adapter_row(avail, VID_CGA)
    dispcp.set_primary(m, mo, S, os88marty.settle, row)
    time.sleep(2)
    os88marty.settle(m)
    dispcp.close_panel(m, mo, S, os88marty.settle)
    time.sleep(1)
    os88marty.settle(m)
    dock = u16(m.read(S("vid_dock_y0"), 2))
    cx, cy, cw, ch = check(m, seg, cal, slot, "after the adapter changed")
    print("      the dock now starts at %d; the window ends at %d"
          % (dock, cy + ch))
    if cy + ch > dock:
        fails.append("after the adapter change the window hangs over the dock")
    both_diff(m, "after the adapter change", (cx, cy + ch))

    # ...and back, which SPEC.md 39.11.2.1 says restores the size it asked for
    print("\n-- ...and back again --")
    dispcp.open_panel(m, mo, S, os88marty.settle)
    dispcp.set_primary(m, mo, S, os88marty.settle,
                       dispcp.adapter_row(avail, VID_HERC))
    time.sleep(2)
    os88marty.settle(m)
    dispcp.close_panel(m, mo, S, os88marty.settle)
    time.sleep(1)
    os88marty.settle(m)
    cx, cy, cw, ch = check(m, seg, cal, slot, "back on the tall adapter")
    both_diff(m, "back on the tall adapter", (cx, cy + ch))

    # --- 2. EXTENDED, and dragged across the seam (SPEC.md 39.16.3) ----------
    print("\n-- extended right, then dragged onto the short display --")
    dispcp.open_panel(m, mo, S, os88marty.settle)
    dispcp.set_mode(m, mo, S, os88marty.settle, "right")
    dispcp.close_panel(m, mo, S, os88marty.settle)
    time.sleep(1)
    os88marty.settle(m)
    pw = u16(m.read(S("vid_pw"), 2))
    vw = u16(m.read(S("vid_w"), 2))
    print("   the seam is at x = %d; the virtual desktop is %d wide" % (pw, vw))
    cx, cy, cw, ch = check(m, seg, cal, slot, "extended, still on display 0")

    # ...drag the title bar well past the seam, so the ORIGIN is on the CGA
    tx, ty = cx + cw // 2, cy + TITLE_H // 2
    mo.drag(tx, ty, pw + 200, 40)
    time.sleep(2)
    os88marty.settle(m)
    cx, cy, cw, ch = check(m, seg, cal, slot, "dragged onto the CGA")
    if cx < pw:
        print("      (it did not cross: nothing to judge)")
    else:
        # the second display is 200 rows tall and starts at the virtual origin
        # in y (SPEC.md 39.19.2), so its last row is 199
        print("      it ends at row %d; the CGA's last row is 199"
              % (cy + ch - 1))
        if cy + ch - 1 > 199:
            fails.append("dragged onto the CGA the frame ends at row %d, "
                         "past the display's last (199)" % (cy + ch - 1))
    n = both_diff(m, "dragged onto the CGA", (cx, cy + ch), soft=True)

    # THE CONTROL, and it is the only thing that can say whose those pixels
    # are: drag a DISK WINDOW - the kernel's own, with no package in it -
    # across the same seam and diff the same way. A package cannot produce
    # what that leaves behind.
    print("\n-- the control: a kernel window over the same seam --")
    dw = [s for s in dispcp.win_list(m, S) if s != slot][-1]
    dx0, dy0, dw0, dh0 = dispcp.win_rect(m, S, dw)
    mo.click(dx0 + dw0 // 2, dy0 + TITLE_H // 2)      # raise it first
    os88marty.settle(m)
    dx0, dy0, dw0, dh0 = dispcp.win_rect(m, S, dw)
    mo.drag(dx0 + dw0 // 2, dy0 + TITLE_H // 2, pw + 210, 60)
    time.sleep(2)
    os88marty.settle(m)
    dx1, dy1, dw1, dh1 = dispcp.win_rect(m, S, dw)
    print("   the Disk window is now at (%d,%d) %dx%d" % (dx1, dy1, dw1, dh1))
    ctl = both_diff(m, "CONTROL: a Disk window dragged over the seam",
                    (dx1, dy1 + dh1), soft=True)
    # A DRAG ACROSS THE SEAM LEAVES PIXELS BEHIND, and it is the window
    # manager's, not the package's: measured here, the kernel's OWN Disk
    # window - which contains no package at all - leaves MORE of them than
    # the Calculator does doing the same thing. So the assertion is
    # comparative rather than zero: a package may not be worse at this than a
    # window with nothing in it. Everything else in this file is still an
    # exact 0, on both cards.
    print("\n   the Calculator left %d, a kernel window left %d"
          % (n[1], ctl[1]))
    if n[1] > ctl[1]:
        fails.append("dragged over the seam the Calculator left %d pixels "
                     "where a kernel window leaves %d - the residue is the "
                     "package's, not the window manager's" % (n[1], ctl[1]))

print()
if fails:
    print("dispcalcx: FAIL")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("dispcalcx: the history re-folds to whatever box the kernel hands it, "
      "on an adapter change and across a seam - PASS")
