#!/usr/bin/env python3
"""SPEC.md 13.11's right button, through the one package that takes it.

    make && python3 tests/minesrc.py

`W_ONRCLICK` is a kernel path (`ui_rdown` -> `wm_onrc` -> `ui_ptcall`) and a
package path (`mn_onrclick` -> `mn_hitcell` -> `mn_flag_toggle`), and only the
two together are the feature. So every case below is driven with a REAL right
press through QEMU's msmouse and read back out of Minesweeper's own bss -
`mn_flags`, `mn_state`, `mn_revealed` - rather than off the screen: a flag is
16 pixels of red pennant and a screenshot cannot tell "the handler ran" from
"the cell happened to redraw".

SEVEN CASES, and three of them are about what must NOT happen:

  A  right press on a covered cell            -> flagged, counter down
  B  right press on it again                  -> unflagged, counter back
  C  right press on the STATUS STRIP          -> nothing (no cell there)
  D  right press then LEFT press on one cell  -> the flag still blocks reveal
  E  left press on a covered cell             -> reveals, exactly as before
  F  right press on an OPEN cell              -> nothing (§23: cannot flag one)
  G  right press on a BACKGROUND Minesweeper  -> RAISES it and does not flag;
                                                the next one flags

G is the case the SDK's second rule exists for and the only one that needs a
second window, so this drives the GAMES folder window it launched from back
to the front and then aims at a board cell that window does not cover.

QEMU AND NOT MARTYPC, unlike tests/dispmine.py next door. Nothing here is a
timing or a pixel question - it is a dispatch question - and QEMU is exact
about which handler ran (CLAUDE.md's "exact about how much work the guest
does"). The cost is that this cannot ask 11.93's question about the dock, and
does not: that is dispmine's row.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402
from ethernet import Qemu, S, SOCK, settle                  # noqa: E402
import os88qemu                                              # noqa: E402

TITLE_H = 18
MN_STRIP_H, MN_CELLPX, MN_COLS = 20, 16, 9      # apps/mines' own constants
MN_S_COVER, MN_S_FLAG, MN_S_OPEN = 0, 1, 2
GAMES_DIR, MINES_PKG = "GAMES", "MINES.O88"
WATCH = ["mn_mode", "mn_revealed", "mn_flags"]

fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


class Mouse:
    """tests/ethernet.py's, plus the right button - one process per action."""

    def run(self, *args):
        subprocess.run(["python3", "tools/mouse.py", SOCK] + list(args),
                       check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", str(x), str(y))
        time.sleep(0.4)

    def rclick(self, x, y):
        self.run("rclick", str(x), str(y))
        time.sleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection.
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       check=True, capture_output=True)
        time.sleep(0.4)


def cell_xy(rect, col, row):
    """The screen centre of board cell (col, row) - apps/mines' own layout."""
    x, y = rect[0] + 1, rect[1] + TITLE_H       # wm_content
    return (x + col * MN_CELLPX + MN_CELLPX // 2,
            y + MN_STRIP_H + row * MN_CELLPX + MN_CELLPX // 2)


def state(m, seg, cell):
    """One cell's MN_S_* byte, straight out of the package's bss."""
    base = dispapps.img_size("mines") + dispapps.bss_off("mines", "mn_state")
    return m.read((seg << 4) + base + cell, 1)[0]


def boot():
    """A plain `make test` - the default images, VGA, the apps disk in B:."""
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    r = subprocess.run(["make", "test"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("minesrc: make test failed:\n" + r.stdout + r.stderr)
    q = Qemu()
    # dispcp scrolls a Disk window with the ARROWS and calls `m.key` with
    # MartyPC's W3C names, which is the only thing it needs that the QEMU
    # harness does not already have. Two names reach it, and QEMU's sendkey
    # spells them differently - so the mapping lives here rather than in
    # dispcp, whose caller is otherwise MartyPC's.
    q.key = lambda name: subprocess.run(
        ["python3", "tools/qmp.py", SOCK,
         "sendkey " + {"ArrowUp": "up", "ArrowDown": "down"}[name]],
        check=True, capture_output=True)
    return q


def launch_mines(m, mo):
    """Drive B: -> GAMES -> MINES.O88, and answer (window slot, segment)."""
    dispcp.open_drive(m, mo, S, settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, settle, wx, wy, GAMES_DIR)
    games = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, games)[:2]
    dispcp.open_named(m, mo, S, settle, wx, wy, MINES_PKG)
    time.sleep(4)
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        sys.exit("minesrc: MINES.O88 did not open")
    return got[0], got[1], games


def main():
    m = boot()
    time.sleep(6)
    mo = Mouse()
    slot, seg, games = launch_mines(m, mo)
    rect = dispcp.win_rect(m, S, slot)
    grect = dispcp.win_rect(m, S, games)
    print("minesrc: mines window %d, segment %04x, rect %r" % (slot, seg, rect))

    def w():
        return dispapps.words(m, seg, "mines", WATCH)

    # --- A: the press that is the whole feature ------------------------------
    x, y = cell_xy(rect, 0, 0)
    mo.rclick(x, y)
    got = w()
    check("A flagged", got["mn_flags"] == 1 and state(m, seg, 0) == MN_S_FLAG,
          "-> %r state=%d" % (got, state(m, seg, 0)))
    check("A revealed nothing", got["mn_revealed"] == 0,
          "(a right press must not open a cell)")

    # --- B: and it is a TOGGLE, not a set ------------------------------------
    mo.rclick(x, y)
    got = w()
    check("B unflagged", got["mn_flags"] == 0 and state(m, seg, 0) == MN_S_COVER,
          "-> %r state=%d" % (got, state(m, seg, 0)))

    # --- C: the strip is not a cell ------------------------------------------
    mo.rclick(rect[0] + 40, rect[1] + TITLE_H + MN_STRIP_H // 2)
    check("C strip ignored", w()["mn_flags"] == 0, "-> %r" % (w(),))

    # --- D: a flag still blocks the left button ------------------------------
    fx, fy = cell_xy(rect, 2, 2)
    mo.rclick(fx, fy)
    mo.click(fx, fy)
    got = w()
    check("D flag blocks reveal",
          got["mn_flags"] == 1 and got["mn_revealed"] == 0
          and state(m, seg, 2 * MN_COLS + 2) == MN_S_FLAG, "-> %r" % (got,))

    # --- E: ...and the left button is otherwise untouched --------------------
    mo.click(*cell_xy(rect, 5, 5))
    got = w()
    check("E left still reveals", got["mn_revealed"] > 0, "-> %r" % (got,))
    if not got["mn_revealed"]:
        sys.exit("minesrc: the LEFT button opened nothing either - the click "
                 "path is broken, not the right button")

    # --- F: an open cell cannot be flagged (SPEC.md 23) ----------------------
    open_cell = next((c for c in range(MN_COLS * MN_COLS)
                      if state(m, seg, c) == MN_S_OPEN), None)
    if open_cell is None:
        fails.append("F no open cell to aim at")
    else:
        before = w()["mn_flags"]
        mo.rclick(*cell_xy(rect, open_cell % MN_COLS, open_cell // MN_COLS))
        got = w()
        check("F open cell refuses a flag",
              got["mn_flags"] == before
              and state(m, seg, open_cell) == MN_S_OPEN, "-> %r" % (got,))

    # --- G: a BACKGROUND window is raised, not played ------------------------
    mo.click(grect[0] + 40, grect[1] + TITLE_H // 2)        # GAMES to the front
    time.sleep(1.0)
    aim = None
    for row in range(MN_COLS - 1, -1, -1):                  # the bottom rows
        cx, cy = cell_xy(rect, 0, row)                      # clear it first
        if not (grect[0] <= cx < grect[0] + grect[2]
                and grect[1] <= cy < grect[1] + grect[3]):
            aim = (cx, cy, row)
            break
    if aim is None:
        fails.append("G no board cell outside the GAMES window - this run "
                     "could not pose the question")
    else:
        cx, cy, row = aim
        before = w()["mn_flags"]
        mo.rclick(cx, cy)
        got = w()
        check("G raise does not flag", got["mn_flags"] == before,
              "cell (0,%d) at (%d,%d) -> %r" % (row, cx, cy, got))
        mo.rclick(cx, cy)                                   # now frontmost
        got = w()
        check("G the NEXT press flags",
              got["mn_flags"] == before + 1
              and state(m, seg, row * MN_COLS) == MN_S_FLAG, "-> %r" % (got,))

    m.quit()
    print()
    for f in fails:
        print("minesrc: FAIL: %s" % f)
    if fails:
        return 1
    print("minesrc: OK - SPEC.md 13.11 dispatches, and only where it should")
    return 0


if __name__ == "__main__":
    sys.exit(main())
