#!/usr/bin/env python3
"""The field's freeze: a straddling window over a Disk window, then a click
(docs/DUAL-DISPLAY-VGA.md 8(11))

    make && python3 tests/dispfreeze.py

Reported: extended desktop, VGA primary and Hercules secondary; open a B: Disk
window; open Note Pad; drag Note Pad to straddle; single-click in the middle
of the Disk window; the system freezes.

THE CONDITION THE REPORT DOES NOT STATE is that the click has to land where
the straddling window OVERLAPS the Disk window - a click on the Disk window
clear of the overlap does nothing at all, which is why the first two attempts
at this reproduced nothing. So the script moves the Disk window under the
straddling one deliberately rather than hoping.

The verdict is the BIOS tick at 0040:006C. It is the same test 8(8) used and
it is the only one that distinguishes a freeze from a slow repaint: a frozen
machine's tick does not advance, and every debug read still answers.
"""
import sys, time
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88marty, os88mouse, os88sym, dispcp
S = os88sym.linear
TITLE_H = 18

def u16(b, i=0): return b[i] | (b[i+1] << 8)

def ticks(m):
    """THE KERNEL'S OWN tick, not the BIOS's - and that distinction IS the
    test. sch_isr chains the ROM's int 08h and bumps [ticks], so 0040:006C
    keeps advancing after sched_unhook while [ticks] stops dead. ui_cmd_reboot
    calls sched_unhook on its way to int 19h, so a machine that has dispatched
    CMD_REBOOT reads ALIVE on the BIOS counter and is not alive at all. That
    misreading cost this investigation two rounds: every click below was
    reported healthy while the kernel underneath had already shut down."""
    return u16(m.read(S("ticks"), 2))


def bios_ticks(m):
    return u16(m.readseg(0x0040, 0x006C, 2))

def ui_passes(m, secs=2.0):
    """HOW MANY PASSES THE UI TASK MADE, which is the only honest liveness
    test here. [ticks] is bumped by sch_isr from inside IRQ0, so it keeps
    advancing through a UI task that has stopped dead - a freeze reads ALIVE
    on it, and that misreading cost this investigation two rounds. ui_task's
    step 0 READS [ui_rebootq] every pass, so a memory breakpoint on that byte
    is a pass counter; it is a DATA access, so unlike an exec breakpoint it
    cannot fire on the 8088's prefetch."""
    m.breakpoints([{"type": "mem", "addr": S("ui_rebootq")}])
    n, t0 = 0, time.time()
    while time.time() - t0 < secs:
        if m.status()["state"] == "breakpoint":
            n += 1
            m.run()
        else:
            time.sleep(0.005)
    m.breakpoints([])
    m.run()
    return n


def alive(m, label, secs=1.2, floor=[0]):
    a, ba = ticks(m), bios_ticks(m)
    n = ui_passes(m, max(secs, 2.0))
    b, bb = ticks(m), bios_ticks(m)
    what = "ALIVE"
    if b == a and bb != ba:
        what = "*** REBOOTED: sched_unhook has run (CMD_REBOOT) ***"
    elif n == 0:
        what = "*** FROZEN: no ui_task pass ***"
    elif b == a:
        what = "*** FROZEN ***"
    elif b < floor[0]:
        what = "*** RESET (tick went backwards from %d) ***" % floor[0]
    floor[0] = b
    print("   %-30s ticks %5d -> %5d  ui passes %3d  %s"
          % (label, a, b, n, what))
    return what == "ALIVE"

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
    ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
    seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
    print("seam at x=%d" % seam)
    alive(m, "after extend")

    dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
    w = dispcp.win_list(m, S); bx, by, bw, bh = dispcp.win_rect(m, S, w[-1])
    disk = w[-1]
    dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "APPS", card=pri)
    bx, by, bw, bh = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "NOTEPAD.O88",
                      card=pri)
    time.sleep(2)
    wins = dispcp.win_list(m, S)
    print("windows:", [(s,) + dispcp.win_rect(m, S, s) for s in wins])
    if len(wins) < 2:
        sys.exit("notepad did not launch")
    np = [s for s in wins if s != disk][-1]
    alive(m, "after notepad launched")

    nx, ny, nw, nh = dispcp.win_rect(m, S, np)
    target = (seam - nw // 2) & ~7
    mo.drag(nx + nw // 2, ny + TITLE_H // 2, target + nw // 2, ny + TITLE_H // 2)
    os88marty.settle(m, card=sec)
    nx, ny, nw, nh = dispcp.win_rect(m, S, np)
    print("notepad now at x %d..%d (seam %d) %s"
          % (nx, nx + nw - 1, seam,
             "STRADDLES" if nx < seam <= nx + nw - 1 else "does NOT straddle"))
    alive(m, "after the straddling drag")

    # LEAVE THE DISK WINDOW WHERE IT OPENED. The field reproduces this with
    # the straddling window NOT overlapping it, which is what says the
    # covering window was never the mechanism.
    bx, by, bw, bh = dispcp.win_rect(m, S, disk)
    print("disk window at x %d..%d, y %d (notepad %d..%d) overlap=%s"
          % (bx, bx + bw - 1, by, nx, nx + nw - 1,
             not (bx + bw - 1 < nx or nx + nw - 1 < bx)))
    cx, cy = bx + bw // 2, by + TITLE_H + (bh - TITLE_H) // 2
    seq = []
    for i, row in enumerate((1, 3, 5, 5, 2, 2)):
        seq.append((bx + bw // 2, by + TITLE_H + 8 + row * 16,
                    "disk row %d (click %d)" % (row, i + 1)))
    dead = False
    for (px, py, what) in seq:
        print("clicking %s at (%d,%d)" % (what, px, py))
        try:
            mo.to(px, py)                    # POSITION FIRST, then settle:
            os88marty.settle(m, card=pri)    # a straddling repaint on a
            mo.click(px, py)                 # 4.77MHz 8088 is SECONDS, and a
            os88marty.settle(m, card=pri)    # move sent into it is dropped by
        except Exception as e:               # the 1200-baud UART - which reads
            print("   click raised: %s" % str(e)[:90])   # as a cursor clamp
            dead = True
        if not alive(m, "after %s" % what, 4.0):
            dead = True
        if dead:
            break
    if dead:
        r = m.cmd(cmd="regs")
        print("   regs:", {k: r.get(k) for k in ("cs","ip","ss","sp","ds","flags") if k in r})
        try:
            print("   callstack:", m.cmd(cmd="callstack"))
        except Exception as e:
            print("   callstack unavailable: %s" % str(e)[:60])
