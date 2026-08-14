#!/usr/bin/env python3
"""Which display does Missile Command ask about Mode X? (SPEC.md 39.18.1)

    make && python3 tests/dispmodex.py

Reported from the field: *"Missile Command would not go into Mode X"* on an
extended VGA + Hercules desktop.

Mode X is gated on the ADAPTER and on nothing else - `fsx_capstab` is indexed
by `VID_*` (VGA 0x01EF has bit 8, HERC 0x0011 and CGA 0x000F do not) and
neither `fsx_caps` nor `fsx_mode` consults the CPU tier. On a two-display
machine `fsx_caps` answers about **the display the asking window is on**
(§39.18.1), which it finds through `wm_top`.

**And `mc_entry` asks BEFORE its window exists.** `mc_adapter` runs at the top
of the entry proc, several dozen instructions before `OSAPI_WM_CREATE`, so
`wm_top` is still the window the game was launched FROM. Launch Missile from a
Disk window sitting on the Hercules and the answer describes the Hercules -
the menu item renames itself `Mode X (Vga)` and `mc_cmd_modex` refuses - even
though the game's own window then opens on the VGA.

This measures `mc_caps` itself, out of the package's own bss, for a launch
from each display.
"""
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, dispcp, dispapps      # noqa: E402

S = os88sym.linear
TITLE_H = 18
FSXM_MODEX = 8
VGA_CAPS = 0x01EF
HERC_CAPS = 0x0011
GAMES_ROW = 1           # B: root, sorted: APPS GAMES MEDIA SYSTEM
MISSILE_ROW = 3         # GAMES, sorted, with '..' at slot 0 (SPEC.md 19.5)


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def pkgs(m):
    """Every visible package window, so reading the WRONG one is visible."""
    out, i = [], 0
    while True:
        p = dispapps.pkg_seg(m, i)
        if p is None:
            return out
        out.append(p)
        i += 1


def facts(m):
    """mc_caps and mc_mono out of the running package, or None if it is up."""
    ps = pkgs(m)
    if not ps:
        return None
    if len(ps) != 1:
        print("   (%d package windows up: %s - reading the last)"
              % (len(ps), [hex(sg) for _, sg in ps]))
    return dispapps.words(m, ps[-1][1], "missile", ["mc_caps", "mc_mono"])


def report(m, where, want_caps, want_mono, bad):
    f = facts(m)
    if f is None:
        print("   %-34s MISSILE IS NOT UP" % where)
        bad.append(where)
        return
    ok = f["mc_caps"] == want_caps and f["mc_mono"] == want_mono
    print("   %-34s mc_caps=0x%04X mono=%d  Mode X %-9s %s"
          % (where, f["mc_caps"], f["mc_mono"],
             "live" if f["mc_caps"] & (1 << FSXM_MODEX) else "greyed",
             "ok" if ok else "EXPECTED 0x%04X/%d" % (want_caps, want_mono)))
    if not ok:
        bad.append(where)


def row(m, mo, disk, card, n):
    bx, by, bw, bh = dispcp.win_rect(m, S, disk)
    dispcp.open_row(m, mo, S, os88marty.settle, bx, by, n, card=card)


def launch(m, mo, disk, card, up=False):
    """Open GAMES and run MISSILE.

    `up` first takes the '..' row (SPEC.md 19.5 synthesizes it at slot 0),
    because a previous launch leaves the window INSIDE GAMES - without it the
    second pass opens row 1 of GAMES (ARKANOID) and row 3 (SOLITAIR), and
    reading missile's bss offsets out of another package's segment answers a
    plausible 0x0000 rather than erroring."""
    if up:
        row(m, mo, disk, card, 0)
    row(m, mo, disk, card, GAMES_ROW)
    row(m, mo, disk, card, MISSILE_ROW)
    time.sleep(3)


def game_win(m, disk):
    w = [x for x in dispcp.win_list(m, S) if x != disk]
    return w[-1] if w else None


def move_to(m, mo, win, x, card):
    """Drag WIN so its CENTRE lands at x - which is what decides the display
    (wm_disp_of: centre, then origin, then the primary)."""
    wx, wy, ww, wh = dispcp.win_rect(m, S, win)
    mo.drag(wx + ww // 2, wy + TITLE_H // 2, x, wy + TITLE_H // 2)
    os88marty.settle(m, card=card)
    time.sleep(1.5)                 # the worker asks once a frame
    return dispcp.win_rect(m, S, win)


def main():
    bad = []
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        ctx = m.read(S("vid_ctx"), 84)
        seam = u16(ctx, 42 + 36)
        print("seam at x=%d; display 0 kind=%d (VGA), display 1 kind=%d (HERC)"
              % (seam, ctx[40], ctx[82]))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        disk = dispcp.win_list(m, S)[-1]
        launch(m, mo, disk, pri)
        report(m, "launched from the VGA", VGA_CAPS, 0, bad)

        # --- the window MOVES, and the facts have to move with it ----------
        g = game_win(m, disk)
        if g is None:
            print("   missile did not launch"); return 1
        r = move_to(m, mo, g, seam + 200, sec)
        print("   dragged to x %d..%d (centre %d, right of the seam)"
              % (r[0], r[0] + r[2] - 1, r[0] + r[2] // 2))
        report(m, "...its centre on the Hercules", HERC_CAPS, 1, bad)

        r = move_to(m, mo, g, 300, pri)
        print("   dragged back to x %d..%d (centre %d)"
              % (r[0], r[0] + r[2] - 1, r[0] + r[2] // 2))
        report(m, "...and back on the VGA", VGA_CAPS, 0, bad)

        # --- launched FROM the Hercules: the entry-time answer is corrected -
        gx, gy, gw, gh = dispcp.win_rect(m, S, g)
        mo.click(gx + 9, gy + TITLE_H // 2)         # close box, LEFT (SPEC 11)
        os88marty.settle(m, card=pri)
        dx, dy, dw, dh = dispcp.win_rect(m, S, disk)
        mo.drag(dx + dw // 2, dy + TITLE_H // 2, seam + 200, dy + TITLE_H // 2)
        os88marty.settle(m, card=sec)
        dx, dy, dw, dh = dispcp.win_rect(m, S, disk)
        print("   disk window now at x %d..%d" % (dx, dx + dw - 1))
        launch(m, mo, disk, sec, up=True)
        report(m, "launched from the Hercules", VGA_CAPS, 0, bad)

    print()
    if bad:
        print("FAIL: %s" % ", ".join(bad))
        return 1
    print("PASS: the caps and the depth follow the window, and a launch from "
          "the other display is corrected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
