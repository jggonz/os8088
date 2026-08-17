#!/usr/bin/env python3
"""Two window-manager artifacts, reproduced with NO package of ours involved.

    make marty && make
    python3 tests/wmartifact.py                       # both parts
    python3 tests/wmartifact.py --part shadow --machine os8088_5150_herc_gla
    python3 tests/wmartifact.py --part seam           # needs the dual machine

Both were found while testing SPEC.md 65's Calculator and both are the window
manager's; docs/WM-ARTIFACTS.md is the report and carries the measurements
this script produces. It is written so that a reader who suspects the package
can throw the package away: part 1 opens `hello`, which draws one greeting and
nothing else, and part 2 drags a DISK WINDOW, which is the kernel's own.

WHAT MAKES EITHER OF THEM VISIBLE IS THE COMPARISON, and it is the thing most
likely to be missing from a failed reproduction. Neither artifact is visible
in a screenshot, because a screenshot has nothing to disagree with: the glass
is stale, not corrupt, and it stays stale until something repaints it. So the
question both parts ask is

    is what is on the glass RIGHT NOW what a full repaint would draw?

and the answer is a pixel count. `full_repaint` below asks the guest to do the
repaint by poking `[cp_dirty]`, which `ui_task` drains with its own
gfx_lock/wm_paint_all/gfx_unlock - do NOT park the CPU on a stub to call those
directly, which is what this harness did first and what docs/MARTYPC-DEBUG.md
now warns about at some length.

THREE MORE WAYS TO MISS THEM, all of which cost time here:

  * the MOUSE POINTER. It is drawn into the framebuffer (SPEC.md 7.1), so a
    forced repaint takes it off and puts it back and its cell legitimately
    differs. Park it somewhere neither artifact can reach, or exclude its
    cell - this script parks it and says where.
  * the CLOCK. The forced repaint takes real wall time, so a run can straddle
    the menu bar's once-a-minute change: two glyph cells at the top right,
    ~42 pixels, nothing to do with any window. Excluded here.
  * SETTLING. Diff a screen that is still being drawn and the count is about
    the drawing, not the artifact. Every diff below is taken after
    os88marty.settle.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty, os88mouse, os88sym, dispcp                      # noqa: E402

S = os88sym.linear
KERNEL_SEG = 0x0060
TITLE_H = 18
MBAR_H = 20
DSK_DE_SIZE = 32
VID_HERC, VID_CGA = 1, 2

fails = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def card_of(machine):
    return "cga" if "cga" in machine and "both" not in machine else "herc"


def frame(m, card):
    """The glass, as one byte per pixel. VGA is four planes behind the
    Graphics Controller and not flat-readable, so there it is the CARD's own
    raster; the 1bpp pair read memory."""
    if card == "vga":
        w, h, px = m.fbuf()
        return w, h, bytes(px[i] for i in range(0, len(px), 3))
    w, h, rows = m.vram(card)
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST repaint, and leave the machine paused."""
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


def diff(m, card, tag, show=8):
    """Pixels on which the glass and a forced repaint disagree."""
    os88marty.settle(m)
    m.cmd(cmd="pause")
    w, h, before = frame(m, card)
    full_repaint(m)
    _, _, after = frame(m, card)
    d = [i for i in range(len(before)) if before[i] != after[i]]
    clk = u16(m.read(S("vid_clk_hx"), 2))
    d = [i for i in d if not (i // w < MBAR_H and i % w >= clk)]
    pts = [(i % w, i // w, before[i], after[i]) for i in d]
    if pts:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        print("   %-38s %4d px   x %d..%d y %d..%d"
              % (tag, len(pts), min(xs), max(xs), min(ys), max(ys)))
        print("      %s%s" % (
            "  ".join("(%d,%d) %d->%d" % p for p in pts[:show]),
            " ..." if len(pts) > show else ""))
    else:
        print("   %-38s    0 px" % tag)
    m.cmd(cmd="run")
    os88marty.settle(m)
    return pts


def row_of(m, name):
    """Which display row a file is on. The listing is SORTED by name
    (SPEC.md 19.4), so a row index is a runtime fact and never the order the
    Makefile happened to build things in."""
    n = u16(m.read(S("disk_nfiles"), 2))
    base = S("disk_dir")
    for i in range(n):
        e = m.read(base + i * DSK_DE_SIZE, DSK_DE_SIZE)
        if e.split(b"\0")[0].decode("latin-1").strip() == name:
            return i
    raise RuntimeError("%s is not in this listing (%d entries: %s)"
                       % (name, n, ", ".join(
                           m.read(base + i * DSK_DE_SIZE, DSK_DE_SIZE)
                           .split(b"\0")[0].decode("latin-1")
                           for i in range(n))))


def name_at(m, row):
    e = m.read(S("disk_dir") + row * DSK_DE_SIZE, DSK_DE_SIZE)
    return e.split(b"\0")[0].decode("latin-1").strip()


def last_clickable_row(m, wx, wy, skip=None):
    """The highest-numbered listing row whose centre is still on the screen."""
    n = u16(m.read(S("disk_nfiles"), 2))
    h = u16(m.read(S("vid_h"), 2))
    for r in range(n - 1, -1, -1):
        if r == skip:
            continue
        if dispcp.row_xy(wx, wy, r)[1] < h - 2:
            return r
    raise RuntimeError("no clickable row in this window")


def under(m, x, y, skip):
    """Which VISIBLE window, if any, holds screen (x, y) - `skip` excluded.

    The question is whether the artifact needs something under the corner or
    whether bare desktop will do, and the window table answers it without
    another capture."""
    best = None
    for s in dispcp.win_list(m, S):
        if s == skip:
            continue
        wx, wy, ww, wh = dispcp.win_rect(m, S, s)
        if wx <= x < wx + ww and wy <= y < wy + wh:
            best = (s, wx, wy, ww, wh)
    return best


def open_apps(m, mo):
    """B:, then its APPS folder. Answers that window's rect."""
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, row_of(m, "APPS"))
    w = dispcp.win_list(m, S)
    return dispcp.win_rect(m, S, w[-1])


# =============================================================================
# PART 1 - one pixel at a window's (W_X, W_Y + W_H)
# =============================================================================
def part_shadow(machine):
    card = "vga" if "vga" in machine else card_of(machine)
    print("\n=== part 1: the drop shadow's bottom-left corner (%s) ===\n"
          % machine)
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=machine) as m:
        mo = os88mouse.Mouse(marty=m)
        wx, wy, ww, wh = open_apps(m, mo)

        # THE POINTER IS PARKED FIRST, and on the far side of the screen from
        # everything this part opens. Left where a click put it, its own cell
        # differs across a forced repaint and reads as an artifact.
        px, py = 8, MBAR_H + 8
        mo.to(px, py)
        print("   pointer parked at (%d,%d)\n" % (px, py))

        # the control: the kernel's own windows, nothing else open
        base = diff(m, card, "CONTROL two Disk windows, no package")
        if base:
            fails.append("the control is not clean: %d px with no package "
                         "open at all" % len(base))

        row = row_of(m, "HELLO.O88")
        dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, row)
        time.sleep(2)
        os88marty.settle(m)
        slot = dispcp.win_list(m, S)[-1]
        hx, hy, hw, hh = dispcp.win_rect(m, S, slot)
        print("\n   HELLO.O88 (row %d) opened at (%d,%d) %dx%d"
              % (row, hx, hy, hw, hh))
        u = under(m, hx, hy + hh, slot)
        print("   its frame's bottom-left shadow pixel is (%d,%d), over %s"
              % (hx, hy + hh,
                 "window slot %d at (%d,%d) %dx%d" % u if u else "the desktop"))
        # EVERY window's rect AND shadow span, because the pixel in question
        # is on a shadow row and a shadow lies OUTSIDE the rect `under` tests:
        # the bottom edge is row y+h, columns x+1..x+w (SPEC.md 11.95.2). Two
        # windows clamped by `wm_fit` share that row, so whose shadow the
        # pixel belongs to is the first thing a reader needs.
        for s in dispcp.win_list(m, S):
            sx, sy, sw, sh = dispcp.win_rect(m, S, s)
            print("      slot %d rect (%d,%d) %dx%d   shadow row %d, "
                  "columns %d..%d" % (s, sx, sy, sw, sh, sy + sh,
                                      sx + 1, sx + sw))
        got = diff(m, card, "a package window is open")
        want = (hx, hy + hh)
        if [p for p in got if (p[0], p[1]) != want]:
            fails.append("more than the shadow corner differs: %s"
                         % [p[:2] for p in got])
        print("   -> %s"
              % ("the ONLY differing pixel is the frame's own (x, y+h)"
                 if [p[:2] for p in got] == [want]
                 else "NOT reproduced in this configuration"))
        for x, y, b, a in got:
            owners = []
            for s in dispcp.win_list(m, S):
                sx, sy, sw, sh = dispcp.win_rect(m, S, s)
                if sy + sh == y and sx < x <= sx + sw:
                    owners.append(s)
            print("      (%d,%d) is on the shadow of window(s) %s: the glass "
                  "has %s there and the repaint %s"
                  % (x, y, owners or "nobody",
                     "black" if b == 0 else "white",
                     "black" if a == 0 else "white"))

        # does it FOLLOW the window? If it does, it is about the frame and not
        # about whatever happens to be under that spot.
        mo.drag(hx + hw // 2, hy + TITLE_H // 2, hx + hw // 2 + 96,
                hy + TITLE_H // 2 + 40)
        time.sleep(1)
        os88marty.settle(m)
        hx2, hy2, hw2, hh2 = dispcp.win_rect(m, S, slot)
        print("\n   dragged to (%d,%d): its corner is now (%d,%d)"
              % (hx2, hy2, hx2, hy2 + hh2))
        got2 = diff(m, card, "...after the window was dragged")
        if [p[:2] for p in got2] == [(hx2, hy2 + hh2)]:
            print("   -> it FOLLOWED the window: the pixel is the frame's")
        elif not got2:
            print("   -> it did NOT come back after a drag")
        else:
            print("   -> it moved somewhere else: %s" % [p[:2] for p in got2])

        # ...and is it still there once the window has gone?
        dispcp.win_rect(m, S, slot)
        mo.click(hx2 + 8, hy2 + TITLE_H // 2)          # the close box
        time.sleep(1)
        os88marty.settle(m)
        got3 = diff(m, card, "...after the package was closed")
        if got3:
            fails.append("closing the package left %d px behind" % len(got3))

        # A SECOND PACKAGE, so the answer is not about `hello`. Which one is
        # whatever the WINDOW can reach: a CGA Disk window shows ~7 rows of a
        # ten-package folder, and a row below the fold cannot be clicked -
        # os88mouse says so rather than clicking into the desktop, which is
        # the one good reason to derive this at run time instead of naming it.
        row = last_clickable_row(m, wx, wy, skip=row)
        nm2 = name_at(m, row)
        dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, row)
        time.sleep(2)
        os88marty.settle(m)
        slot = dispcp.win_list(m, S)[-1]
        rx, ry, rw, rh = dispcp.win_rect(m, S, slot)
        u = under(m, rx, ry + rh, slot)
        print("\n   %s (row %d) opened at (%d,%d) %dx%d; its corner (%d,%d) "
              "is over %s"
              % (nm2, row, rx, ry, rw, rh, rx, ry + rh,
                 "window slot %d" % u[0] if u else "the desktop"))
        got4 = diff(m, card, "a second package window is open")
        print("   -> %s"
              % ("the same one pixel, at ITS (x, y+h)"
                 if [p[:2] for p in got4] == [(rx, ry + rh)]
                 else "%d px: %s" % (len(got4), [p[:2] for p in got4][:6])))


# =============================================================================
# PART 2 - what a drag across an extended desktop's seam leaves behind
# =============================================================================
def part_seam():
    print("\n=== part 2: a drag across the seam (os8088_5150_both_gla) ===\n")
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_both_gla") as m:
        mo = os88mouse.Mouse(marty=m)
        avail = m.read(S("vid_avail"), 1)[0]
        print("   adapters available: %#04x (bit1 Hercules, bit2 CGA)" % avail)
        wx, wy, ww, wh = open_apps(m, mo)

        # EXTEND THE DESKTOP. Without this step there is no seam and nothing
        # to reproduce - which is the likeliest reason a reproduction fails.
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        time.sleep(1)
        os88marty.settle(m)
        pw = u16(m.read(S("vid_pw"), 2))
        vw = u16(m.read(S("vid_w"), 2))
        kind = m.read(S("vid_kind"), 1)[0]
        prim = {VID_HERC: "herc", VID_CGA: "cga"}.get(kind, "?%d" % kind)
        second = "cga" if prim == "herc" else "herc"
        # WHICH CARD IS PRIMARY DECIDES WHICH ONE TO LOOK AT, and it is not a
        # detail: measured, the residue lands on the SECONDARY display both
        # ways round, so a run that only diffs the card it expects sees a
        # clean screen and reports no reproduction.
        print("   the seam is at x = %d; the virtual desktop is %d wide; "
              "the PRIMARY is %s and the secondary is %s\n"
              % (pw, vw, prim, second))

        mo.to(8, MBAR_H + 8)
        for card in ("herc", "cga"):
            base = diff(m, card, "CONTROL extended, nothing dragged (%s)"
                        % card)
            if base:
                fails.append("the extended desktop is not clean on %s: %d px"
                             % (card, len(base)))

        slot = dispcp.win_list(m, S)[-1]
        dx, dy, dw, dh = dispcp.win_rect(m, S, slot)
        print("\n   dragging the Disk window at (%d,%d) %dx%d across the seam"
              % (dx, dy, dw, dh))
        mo.drag(dx + dw // 2, dy + TITLE_H // 2, pw + 210, 60)
        time.sleep(2)
        os88marty.settle(m)
        dx1, dy1, dw1, dh1 = dispcp.win_rect(m, S, slot)
        print("   it is now at (%d,%d) %dx%d - origin %s the seam"
              % (dx1, dy1, dw1, dh1, "past" if dx1 >= pw else "still before"))
        mo.to(8, MBAR_H + 8)                       # the pointer went with it
        left = {}
        for card in ("herc", "cga"):
            left[card] = diff(m, card, "after the drag (%s)" % card)
        print("\n   -> hercules %d px, cga %d px"
              % (len(left["herc"]), len(left["cga"])))
        if not left["cga"] and not left["herc"]:
            print("   -> NOT reproduced: the drag left nothing behind")

        # ...and does a repaint fix it for good, or does the next drag do it
        # again? The second answer is what says it is the drag rather than a
        # one-off.
        dx2, dy2, dw2, dh2 = dispcp.win_rect(m, S, slot)
        mo.drag(dx2 + dw2 // 2, dy2 + TITLE_H // 2, pw - 120, 60)
        time.sleep(2)
        os88marty.settle(m)
        dx3, dy3, _, _ = dispcp.win_rect(m, S, slot)
        print("\n   dragged back to (%d,%d)" % (dx3, dy3))
        mo.to(8, MBAR_H + 8)
        back = {}
        for card in ("herc", "cga"):
            back[card] = diff(m, card, "after dragging back (%s)" % card)
        print("\n   -> hercules %d px, cga %d px"
              % (len(back["herc"]), len(back["cga"])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", choices=("shadow", "seam", "both"),
                    default="both")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()

    if a.part in ("shadow", "both"):
        part_shadow(a.machine)
    if a.part in ("seam", "both"):
        part_seam()

    print()
    if fails:
        print("wmartifact: something OTHER than the two known artifacts moved")
        for f in fails:
            print("  - " + f)
        sys.exit(1)
    print("wmartifact: both artifacts measured; docs/WM-ARTIFACTS.md is the "
          "report")


# The guard is not decoration: `diff`, `full_repaint`, `row_of` and
# `open_apps` are the pieces a one-off probe wants to import, and without it
# importing this file runs the whole 20-minute session first.
if __name__ == "__main__":
    main()
