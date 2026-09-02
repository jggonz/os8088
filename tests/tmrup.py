#!/usr/bin/env python3
"""SPEC.md 13.8: the Timer's three buttons fire on the RELEASE.

    python3 tests/tmrup.py [machine]

The Timer is the first conversion where SPEC.md 47's GREYING and 13.8's DOWN
state have to agree, and all four combinations of the two flags are reachable
in one gesture: Start and Stop grey each other, so pressing the live one greys
it the moment it fires, and pressing the greyed one has to say "pressed" about
a control that will refuse.

The signal is TMR_RUN, one byte of the instance's own state, because that is
memory rather than a picture.

  A  press Start, slide OFF it, release  -> still stopped
  B  ...and it came back UP while held
  C  press AND release on Start          -> running
  D  a press draws it DOWN
  E  a press on the GREYED one changes NOTHING and fires nothing - DIS
     OUTRANKS DOWN inside os88ui_btn, so a control that is going to refuse
     must never say "pressed" (SPEC.md 13.8). The first draft of this case
     asserted the opposite and the kernel was right.
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
TITLE_H = 18
# apps.inc's layout, mirrored
APP_TMR_BY, APP_TMR_BH, APP_TMR_BW, APP_TMR_BGAP = 20, 15, 46, 6
APP_TMR_NB = 3
MBAR_H = 20
TMR_RUN = None

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def quiet(m, s=6.0):
    """settle() with a limit, swallowed. THE TIMER ANIMATES - its digits move
    once a second - so the ordinary settle never returns, which is
    tools/subcheck.py's "NO ANIMATING WINDOW" note met from the other side."""
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def timer_win(m):
    want = m.sym("app_ttl_timer") - (M.KERNEL_SEG << 4)
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = b[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, 0) & 2:
            return i, tuple(_u16(r, o) for o in (2, 4, 6, 8))
    return None, None


def running(m):
    """TMR_RUN out of the live instance's state block."""
    p = m.sym("app_tmr_pool") - (M.KERNEL_SEG << 4)
    return m.read((M.KERNEL_SEG << 4) + p + TMR_RUN, 1)[0]


def lit(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    n = 0
    for y in range(rect[1], rect[3] + 1):
        for x in range(rect[0], rect[2] + 1):
            i = (y * w + x) * 3
            if px[i] or px[i + 1] or px[i + 2]:
                n += 1
    return n


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : the Timer fires on the release (SPEC.md 13.8) ==")

    import re
    src = open("kernel/apps.inc").read()
    TMR_RUN = int(re.search(r"^TMR_RUN\s+equ\s+(\d+)", src, re.M).group(1))
    bx0 = int(re.search(r"^APP_TMR_BX\b", src, re.M) is not None)

    # Locator's bar is chip / Locator / File / Builtins, and Timer is
    # Builtins' first item. The title's x is FOUND rather than guessed: the
    # bar is composed at run time (SPEC.md 12.9) and a literal would be a
    # second opinion about a layout that moves with the app's name.
    w0, h0, r0 = m.vram()
    dark = [x for x in range(100, 320)
            if any(not r0[y][x] for y in range(6, 14))]
    runs, cur = [], []
    for x in dark:
        if cur and x - cur[-1] > 8:
            runs.append(cur); cur = []
        cur.append(x)
    if cur:
        runs.append(cur)
    tx = (runs[1][0] + runs[1][-1]) // 2 if len(runs) > 1 else 190
    print(f"  Builtins at x~{tx}")
    mo.menu(tx, 8, tx, MBAR_H + 1 + 8)
    quiet(m)
    slot, w = timer_win(m)
    check("a Timer window opened", w is not None, f"{w}")
    if not w:
        sys.exit("no window")
    cx, cy = w[0] + 1, w[1] + TITLE_H
    # APP_TMR_BX is derived; take it from the window's own width
    bx = ((w[2] - 2) - APP_TMR_NB * APP_TMR_BW
          - (APP_TMR_NB - 1) * APP_TMR_BGAP) // 2

    def brect(i):
        x1 = cx + bx + i * (APP_TMR_BW + APP_TMR_BGAP)
        return (x1, cy + APP_TMR_BY, x1 + APP_TMR_BW - 1,
                cy + APP_TMR_BY + APP_TMR_BH - 1)

    # Opening the Timer STARTS it (SPEC.md 14), so Stop is the live one.
    print(f"  TMR_RUN starts {running(m)}")
    stop = brect(1)
    inner = (stop[0] + 1, stop[1] + 1, stop[2] - 1, stop[3] - 1)
    mid = ((stop[0] + stop[2]) // 2, (stop[1] + stop[3]) // 2)

    mo.to(mid[0], stop[1] - 20)
    time.sleep(0.8)
    up = lit(m, mono, inner)
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.9)
    down = lit(m, mono, inner)
    check("a press draws Stop DOWN", down * 2 < up,
          f"({up} lit upright, {down} held)")
    check("...and it has NOT stopped yet", running(m) == 1,
          f"(TMR_RUN = {running(m)})")

    mo.to(w[0] + 4, mid[1], l=True)          # slide off, STILL HELD
    time.sleep(1.2)
    off = lit(m, mono, inner)
    check("...and back UP when the pointer slides off it", off == up,
          f"({off} lit, upright is {up})")
    mo._edge(False)
    quiet(m)
    check("a slide-off release does NOT stop it", running(m) == 1,
          f"(TMR_RUN = {running(m)})")

    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    quiet(m)
    check("press-and-release ON Stop DOES stop it", running(m) == 0,
          f"(TMR_RUN = {running(m)})")

    # --- E: the GREYED one still draws down, and fires nothing ------------
    # Stop is greyed now. SPEC.md 47's DIS outranks 13.8's DOWN inside
    # os88ui_btn, so what this asserts is that the picture CHANGES (the press
    # is acknowledged) while the state does not.
    quiet(m)
    greyed = lit(m, mono, inner)
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.9)
    held = lit(m, mono, inner)
    # The complement of the DOWN test above rather than equality: the mouse
    # arrow is ~24 pixels of this rect and moves between samples, where a real
    # black fill takes ~490 to ~90. Asking for equality made this the third
    # case this week to be measuring the pointer.
    check("a press on the GREYED Stop does NOT draw it pressed",
          held * 2 > greyed,
          f"({greyed} lit upright, {held} held - DIS outranks DOWN, so a "
          f"black fill must NOT have happened)")
    mo._edge(False)
    quiet(m)
    check("...and fires nothing", running(m) == 0, f"(TMR_RUN = {running(m)})")

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
