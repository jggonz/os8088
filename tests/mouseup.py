#!/usr/bin/env python3
"""SPEC.md 13.7's release, apps/os88ui.inc's arm, and MOUSEUP-PLAN 4.2's guard.

Five cases. Every signal is read out of the WINDOW TABLE - nothing here is
screen-scraped, so a pass is a memory read and not a picture:

  A  press off both buttons, release on One      -> not fired
  B  press One, release on Two                   -> not fired, release ARRIVED
  C  press One, release outside the window       -> not fired, release ARRIVED
  D  press One, release One                      -> FIRED (the window hides)
  7  press a window that DESTROYS ITSELF, then
     release over where it was                   -> the release is REFUSED

Case 7 is docs/MOUSEUP-PLAN.md 4.2 and it is the one no ordinary gesture can
reach: between the two passes a package can free the record the kernel armed
the release against, and .mup_pkg has to notice before it dispatches through
it. muptest's second window does exactly that with OSAPI_WM_DESTROY.

  python3 tests/mouseup.py [machine]
"""
import sys, struct, time
sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88fixture import need

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
from os88geom import WIN_SIZE, MAX_WIN   # NOT a local copy: this one
                                        # moved 28 -> 30 with SPEC.md
                                        # 13.8.2's W_ONDRAG, and a stale
                                        # stride decodes window 1 as
                                        # garbage and reads it as unused


def granted(w):
    """The frame width the kernel GRANTS for a requested frame width `w`.

    SPEC.md 11.94.5's `wm_snap_w` rounds a window's CONTENT width - `W_W - 2`,
    the frame less its two borders - UP to a multiple of 8 wherever it fits, so
    that the LAST cell in a row owns its framebuffer byte exactly as 11.94's
    origin snap makes the first one own its. muptest asks for a 240 and a 160
    wide frame, whose content widths are 238 and 158; neither is a multiple of
    8, so what the window manager hands back is 242 and 162.

    Derived rather than written down as 242, so the rule is stated once and a
    fixture that changes its own size does not need this line changed with it.
    The heights are untouched - 11.94.5 snaps the width alone.
    """
    return ((w - 2 + 7) // 8) * 8 + 2


MUP = (granted(240), 100)        # the main window's frame, AS GRANTED, and
VAN = (granted(160), 60)         # ...the vanish window's. MU_W/MU_H in
                                 # tests/muptest/muptest.asm are what the
                                 # package ASKS for, which stopped being what
                                 # it is given when 11.94.5 landed.
fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def drive_y(m, n=1):
    """The middle of drive zone n, read from the KERNEL rather than assumed.

    The zone pitch is [desk_zstep] and SPEC.md 26.4 made it adapter-dependent
    - 60 rows with the 32-row icon, 34 with the CGA's short one - so a
    hard-coded y that worked on every adapter for months silently started
    landing one zone out.
    """
    step = int.from_bytes(m.read(m.sym("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(m.sym("desk_zh1"), 2), "little")
    return 32 + n * step + h1 // 2


def wins(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    out = []
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if struct.unpack_from("<H", r, 0)[0] & 2:
            out.append(tuple(struct.unpack_from("<H", r, o)[0]
                             for o in (2, 4, 6, 8, 10)))   # x y w h W_TITLE
    return out


def find(m, size):
    return next((w for w in wins(m) if (w[2], w[3]) == size), None)


def disk_win(m):
    return next((w for w in wins(m) if (w[2], w[3]) not in (MUP, VAN)), None)


def armw(m):
    return int.from_bytes(m.read(m.sym("ui_armw"), 2), "little")


need("build/muptest.img")          # `all` builds nothing under tests/

with M.launch("build/os8088-360.img", apps="build/muptest.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    print(f"== {MACHINE} : SPEC 13.7 + os88ui.inc + MOUSEUP-PLAN 4.2 ==")

    if not disk_win(m):
        mo.dblclick(vw - 40, drive_y(m))
        M.settle(m)
    d = disk_win(m)
    if d is None:
        sys.exit("no Disk window")
    # RAISE it first: after a muptest window hides, the Disk window is not
    # frontmost, and a double-click on a background window spends its first
    # click on wm_front - so the pair reads as raise-then-select and the row
    # never opens. Cost one CGA-only pass to notice.
    mo.click(d[0] + d[2] // 2, d[1] + 9)
    M.settle(m)
    mo.dblclick(d[0] + 40, d[1] + 18 + 30)
    M.settle(m)

    w = find(m, MUP)
    check("launched", w is not None)
    if not w:
        sys.exit("nothing to test")
    cx, cy = w[0] + 1, w[1] + 18
    one = (cx + 16 + 31, cy + 40 + 9)
    two = (cx + 100 + 31, cy + 40 + 9)
    print(f"   win={w[:4]}  One={one}  Two={two}")

    # --- case 7 FIRST, because it is the only one that needs the vanish
    # window, and D hides the main one -------------------------------------
    v = find(m, VAN)
    check("the vanish window is up", v is not None)
    if v:
        # RAISE it first. The main window is launched last and so is
        # frontmost, and W_ONCLICK fires on the frontmost window ALONE
        # (SPEC.md 13) - so a press on a background window is spent on
        # wm_front and arms nothing. Without this the release test passes
        # VACUOUSLY, which is what the "did it arm" check below exists to
        # catch and did.
        mo.click(v[0] + v[2] // 2, v[1] + 9)
        M.settle(m)
        v = find(m, VAN)
        t0 = find(m, MUP)[4]
        vx, vy = v[0] + v[2] // 2, v[1] + 18 + 20
        # PRESS and RELEASE are driven apart on purpose: the whole case lives
        # BETWEEN them, and `menu` would close the gesture before anything
        # could be read.
        mo.to(vx, vy)
        mo._pk(l=True)
        time.sleep(1.2)
        armed = armw(m)                    # mu_vclick has run and destroyed
        gone = find(m, VAN) is None        # the window; the arm points at it
        mid = find(m, MUP)
        check("the press was DISPATCHED (the caption moved to Pressed)",
              mid is not None and mid[4] != t0)
        check("...and it armed a release", armed != 0,
              f"(ui_armw={armed:#06x})")
        check("the window really went away (the record is FREED)", gone)

        mo._pk()                           # ...and now the release
        time.sleep(1.5)
        M.settle(m)
        w2 = find(m, MUP)
        # The caption is the signal. mu_vup would move it to 'GuardFailed',
        # so the POINTER not having changed since the press is the pass - the
        # two strings live in the package's own segment and differ, which is
        # all this needs to know about them.
        check("the release did NOT dispatch through the freed record",
              w2 is not None and mid is not None and w2[4] == mid[4],
              f"(title {mid[4]:#06x} -> {w2[4]:#06x})" if w2 and mid else "")
        check("...and the arm was SPENT either way", armw(m) == 0,
              f"(ui_armw={armw(m):#06x})")

    print("-- A: press off both buttons, release on One --")
    mo.menu(cx + 8, cy + 8, one[0], one[1])
    M.settle(m)
    check("NOT fired (the press armed nothing)", find(m, MUP) is not None)

    print("-- B: press One, release on Two --")
    t0 = find(m, MUP)[4]
    mo.menu(one[0], one[1], two[0], two[1])
    M.settle(m)
    w2 = find(m, MUP)
    check("NOT fired (still up)", w2 is not None)
    check("...but the release DID arrive (retitled)",
          w2 is not None and w2[4] != t0)

    print("-- C: press One, release outside the WINDOW --")
    t0 = find(m, MUP)[4]
    mo.menu(one[0], one[1], w[0] + w[2] + 30, w[1] + w[3] + 20)
    M.settle(m)
    w2 = find(m, MUP)
    check("NOT fired", w2 is not None)
    check("...and the release still arrived (13.7's second rule)",
          w2 is not None and w2[4] != t0)

    print("-- D: press One, release One --")
    mo.menu(one[0], one[1], one[0] + 2, one[1])
    M.settle(m)
    check("the arm FIRED (window hid)", find(m, MUP) is None)

print()
if fails:
    print("FAILURES:"); [print("  " + f) for f in fails]
    sys.exit(1)
print("all pass")
