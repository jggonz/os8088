#!/usr/bin/env python3
"""Does a PACKAGE stay on its own display? (SPEC.md 39.2.1)

    make && python3 tests/dispapp.py

OSAPI_VIDEO used to answer [vid_w]/[vid_h] - the whole VIRTUAL DESKTOP once a
second display is extended - and a package sizes and centres itself from it.
Arkanoid therefore laid itself out for a 1360px screen, wm_fit clamped its
window to the primary, and the layout it kept drawing ran ~115px past the
seam and onto the OTHER MONITOR. Reported from the field on 86Box.

THE ASSERTION IS THE SECOND CARD BEFORE AND AFTER THE LAUNCH. The window is
wholly on display 0, so a correct system changes NOTHING on display 1 - and
that is a question a screenshot cannot answer, because what a broken run puts
there is desktop dither with a few grey bands in it.

The forced wm_paint_all afterwards is the second half: it says whether what is
on the glass is what a full repaint would put there, which separates "the app
drew outside its window" from "something left stale pixels behind".
"""
import os, sys
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, dispcp
S = os88sym.linear
KERNEL_SEG = 0x0060

def u16(b, i=0): return b[i] | (b[i+1] << 8)

def mono(m):
    w, h, rows = m.vram("herc")
    return w, h, bytes(b for r in rows for b in r)

def vga(m, idx):
    w, h, px = m.fbuf(card=idx)
    return w, h, bytes(px[i] for i in range(0, len(px), 3))

def stub(m, entry):
    where = S("menu_bcell") - KERNEL_SEG * 16
    target = S(entry) - KERNEL_SEG * 16
    rel = (target - (where + 8)) & 0xFFFF
    m.write(S("menu_bcell"),
            bytes([0xFA, 0x8C, 0xC8, 0x8E, 0xD8,
                   0xE8, rel & 0xFF, rel >> 8, 0xEB, 0xFE]))
    return where

with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine="os8088_xt_vga_herc", boot=False) as m:
    cards = {c["idx"]: c for c in m.cards()}
    m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
    mo = os88mouse.Mouse(marty=m)
    pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
    sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
    dispcp.open_panel(m, mo, S, os88marty.settle)
    dispcp.set_mode(m, mo, S, os88marty.settle, "right")
    dispcp.close_panel(m, mo, S, os88marty.settle)
    ctx = m.read(S("vid_ctx"), 84)
    seam = u16(ctx, 42 + 36)
    print("seam at x=%d; vid_w = %d" % (seam, u16(m.read(S("vid_w"), 2))))

    dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 1, card=pri)   # GAMES
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    before_sec = mono(m)                     # the secondary BEFORE Arkanoid
    dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 1, card=pri)   # ARKANOID
    import time; time.sleep(3)
    wins = dispcp.win_list(m, S)
    print("windows now:", [(s,) + dispcp.win_rect(m, S, s) for s in wins])
    ark = wins[-1]
    ax, ay, aw, ah = dispcp.win_rect(m, S, ark)
    ax0, aw0 = ax, aw
    print("ARKANOID window at (%d,%d) %dx%d -> spans x %d..%d, seam %d  %s"
          % (ax, ay, aw, ah, ax, ax + aw - 1, seam,
             "STRADDLES" if ax < seam <= ax + aw - 1 else "one display"))

    after_sec = mono(m)
    d = sum(1 for i in range(len(before_sec[2]))
            if before_sec[2][i] != after_sec[2][i])
    print("SECONDARY changed by %d pixels when Arkanoid launched (window is "
          "wholly on display 0)" % d)
    straddles = ax0 < seam <= ax0 + aw0 - 1
    if d:
        xs = [i % before_sec[0] for i in range(len(before_sec[2]))
              if before_sec[2][i] != after_sec[2][i]]
        ys = [i // before_sec[0] for i in range(len(before_sec[2]))
              if before_sec[2][i] != after_sec[2][i]]
        print("   bbox on the secondary: x %d..%d, y %d..%d"
              % (min(xs), max(xs), min(ys), max(ys)))
    m.cmd(cmd="pause")
    b_sec = mono(m); b_pri = vga(m, pri)
    off = stub(m, "wm_paint_all")
    m.cmd(cmd="park", cs=KERNEL_SEG, ip=off)
    m.cmd(cmd="advance", cycles=30_000_000)
    print("parked at ip=0x%04X (stub 0x%04X)" % (m.status()["ip"], off))
    a_sec = mono(m); a_pri = vga(m, pri)
    for nm, b, a in (("secondary(herc)", b_sec, a_sec), ("primary(vga)", b_pri, a_pri)):
        dd = sum(1 for i in range(min(len(b[2]), len(a[2]))) if b[2][i] != a[2][i])
        print("%-16s %dx%d: %d differ / %d after a forced wm_paint_all"
              % (nm, b[0], b[1], dd, len(b[2])))
    if straddles:
        print("\ndispapp: SKIPPED - the window straddles the seam, so drawing "
              "on display 1 is legitimate and this test cannot judge it")
        sys.exit(0)
    if d:
        print("\ndispapp: FAIL\n  - a package whose window is wholly on "
              "display 0 changed %d pixels on display 1 (SPEC.md 39.2.1)" % d)
        sys.exit(1)
    print("\ndispapp: a package stays on the display its window is on - PASS")
