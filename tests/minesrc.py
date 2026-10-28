#!/usr/bin/env python3
"""SPEC.md 13.11's right button, through the one package that takes it.

    make && python3 tests/minesrc.py

`W_ONRCLICK` is a kernel path (`ui_rdown` -> `wm_onrc` -> `ui_ptcall`) and a
package path (`mn_onrclick` -> `mn_hitcell` -> `mn_flag_toggle`), and only the
two together are the feature. So every case below is driven with a REAL right
press through QEMU's msmouse and read back out of Minesweeper's own bss -
`mn_flags`, `mn_cell`, `mn_revealed` - rather than off the screen: a flag is
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
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402
from ethernet import Qemu, S, settle                        # noqa: E402
import os88geom                                             # noqa: E402
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
    """The right button and the left, CLOSED-LOOP (os88qemu.mouse_click):
    the pointer read back off mouse_x/mouse_y and each edge off mouse_btn,
    where tools/mouse.py pinned, walked and slept a process a gesture. Each
    gesture then ends when the UI has finished with it (ui_done), capped at
    the 0.4 guest s it used to pace; every case waits on its own answer."""

    def __init__(self, q):
        self.q = q

    def click(self, x, y, mask=1):
        os88qemu.mouse_click(self.q, S, self.q.hmp, x, y, mask)
        os88qemu.ui_done(self.q, S, cap=0.4)

    def rclick(self, x, y):
        self.click(x, y, mask=2)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window - four confirmed edges
        os88qemu.mouse_dblclick(self.q, S, self.q.hmp, x, y)
        os88qemu.ui_done(self.q, S, cap=0.4)


def frontmost(m):
    """The slot the WM says is on top - off wm_zord, the way wm_top reads it.

    NOT `dispcp.win_list(m, S)[-1]`, which is the highest USED SLOT and says
    nothing about z-order: it is the newest window only because a fresh slot
    is taken in order, and it does not move when one is raised. That reads
    correctly for `launch_mines`, where the window has just been created, and
    is simply the wrong question for a raise.
    """
    ptr = os88geom.top(m, S)
    if not ptr:
        return None
    return (ptr - S("wm_wins")) // os88geom.WIN_SIZE


def gticks(m):
    """The kernel's own IRQ0 counter - the only clock in this row that is the
    GUEST's. 18.2 Hz (SPEC.md 15), and nothing but IRQ0 advances it."""
    return int.from_bytes(m.read(S("ticks"), 2), "little")


def acted(m, cond, ticks=110):
    """Wait for the guest to have ACTED, budgeted in its own ticks.

    EVERY PRESS IN THIS ROW USED TO BE READ BACK IMMEDIATELY - `mo.rclick(...)`
    and then `w()` on the next line, with nothing between them saying the guest
    had seen it. Most cases survive that because they assert a CHANGE, and a
    change that has not happened yet reads as a plain failure; case G does not,
    and that is what went red in the 2026-09-21 full soak:

        [PASS] G raise does not flag ... mn_flags: 1
        [FAIL] G the NEXT press flags ... mn_flags: 1

    The first of those two is a NEGATIVE assertion - flags unchanged - so it
    passes VACUOUSLY while the press is still in flight, and the second press
    then lands on a window the first one has only just raised. The row reports
    the dispatch it exists to test, for a press the machine had not processed.

    So a press is followed by the guest's own answer to "did you act", and 110
    ticks is ~6 guest seconds - a budget a loaded box cannot shorten, where a
    `time.sleep` is a different amount of the machine at every width.
    """
    t0 = gticks(m)
    while not cond():
        if ((gticks(m) - t0) & 0xFFFF) > ticks:
            return False
        time.sleep(0.05)
    return True


def quiet(m, ticks=20):
    """Burn `ticks` of the GUEST's clock - the wait a NEGATIVE case needs.

    Three of the seven cases assert that nothing happened (C, F, and the first
    half of G), and a press that has not been processed yet satisfies all
    three. There is no positive edge to wait for - that is what "nothing
    happened" means - so the honest wait is a bounded amount of the MACHINE's
    own time, which a loaded box cannot shorten the way it shortens a
    time.sleep. 20 ticks is ~1.1 guest seconds; the positive cases beside them
    take far less than that to land.
    """
    t0 = gticks(m)
    while ((gticks(m) - t0) & 0xFFFF) < ticks:
        time.sleep(0.02)


def cell_xy(rect, col, row):
    """The screen centre of board cell (col, row) - apps/mines' own layout."""
    x, y = rect[0] + 1, rect[1] + TITLE_H       # wm_content
    return (x + col * MN_CELLPX + MN_CELLPX // 2,
            y + MN_STRIP_H + row * MN_CELLPX + MN_CELLPX // 2)


def state(m, seg, cell):
    """One cell's MN_S_* state, decoded out of the package's bss: `mn_cell`
    is one byte a cell (SPEC.md 23) - MN_C_OPEN 0x40, MN_C_FLAG 0x20."""
    base = dispapps.img_size("mines") + dispapps.bss_off("mines", "mn_cell")
    b = m.read((seg << 4) + base + cell, 1)[0]
    return MN_S_OPEN if b & 0x40 else MN_S_FLAG if b & 0x20 else MN_S_COVER


def boot():
    """`make test`'s machine - the default images, VGA, the apps disk in B:,
    the OPL2 its DEVCARD adds - as an os88qemu.Private in a directory of its
    own: the make would rebuild a build/ that is behind its sources, and its
    fixed socket kept two QEMU rows from running at once."""
    import tempfile
    import os88build
    tmp = tempfile.mkdtemp(prefix="mnsrc-")     # short: a socket path
    p = os88qemu.Private(tmp, os88build.at("build/os8088.img"),
                         os88build.at("build/apps.img"),
                         extra=["-audiodev", "none,id=devsnd", "-device",
                                "adlib,audiodev=devsnd"])
    q = Qemu(p.sock)
    # dispcp scrolls a Disk window with the ARROWS and calls `m.key` with
    # MartyPC's W3C names, which is the only thing it needs that the QEMU
    # harness does not already have. Two names reach it, and QEMU's sendkey
    # spells them differently - so the mapping lives here rather than in
    # dispcp, whose caller is otherwise MartyPC's.
    q.key = lambda name: q.hmp(
        "sendkey " + {"ArrowUp": "up", "ArrowDown": "down"}[name])
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
    # The package's WINDOW is the launch having landed - in the guest's own
    # seconds (tests/os88qemu.py) - and one more for the board's first paint.
    if os88qemu.acted(m, lambda: dispapps.pkg_seg(m, 0) is not None, secs=30,
                      what="the Minesweeper window", poll=0.05):
        os88qemu.ui_done(m, S, cap=1)   # (the board's first paint)
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        sys.exit("minesrc: MINES.O88 did not open")
    return got[0], got[1], games


def main():
    """Boot, run, and ALWAYS quit the machine.

    `sys.exit` inside the body used to skip `m.quit()` - and `make test`
    DAEMONISES qemu, so the emulator outlived the script, kept the write lock
    on build/os8088.img, and every later QEMU row died in 0.4s with
    `Failed to get "write" lock`. tools/os88bisect.py then read three of those
    as the ROW failing and answered `minesrc is PRE-EXISTING - not a
    regression, nothing to bisect` about a row that passes every time it is
    asked by itself. One leaked process, three wrong verdicts, and nothing in
    any of them named qemu.
    """
    m = boot()
    try:
        return _run(m)
    finally:
        try:
            m.quit()
        except Exception:                                   # noqa: BLE001
            pass                    # already gone, or never came up


def _run(m):
    # The desktop is up when drive B: has its zone - the thing the first
    # double-click aims at - and that is asked of the GUEST, not a 6s guess.
    def zone():
        try:
            return dispcp.drive_ordinal(m, S, "B") is not None
        except Exception:                                   # noqa: BLE001
            return False
    if not os88qemu.acted(m, zone, secs=90, what="drive B's zone", poll=0.05):
        sys.exit("minesrc: drive B: never got a desktop zone - the guest did "
                 "not reach a desktop")
    os88qemu.ui_done(m, S, cap=8.0, what="the first desktop")
    mo = Mouse(m)
    slot, seg, games = launch_mines(m, mo)
    rect = dispcp.win_rect(m, S, slot)
    grect = dispcp.win_rect(m, S, games)
    print("minesrc: mines window %d, segment %04x, rect %r" % (slot, seg, rect))

    def w():
        return dispapps.words(m, seg, "mines", WATCH)

    # --- A: the press that is the whole feature ------------------------------
    x, y = cell_xy(rect, 0, 0)
    mo.rclick(x, y)
    acted(m, lambda: w()["mn_flags"] != 0)
    got = w()
    check("A flagged", got["mn_flags"] == 1 and state(m, seg, 0) == MN_S_FLAG,
          "-> %r state=%d" % (got, state(m, seg, 0)))
    check("A revealed nothing", got["mn_revealed"] == 0,
          "(a right press must not open a cell)")

    # --- B: and it is a TOGGLE, not a set ------------------------------------
    mo.rclick(x, y)
    acted(m, lambda: w()["mn_flags"] == 0)
    got = w()
    check("B unflagged", got["mn_flags"] == 0 and state(m, seg, 0) == MN_S_COVER,
          "-> %r state=%d" % (got, state(m, seg, 0)))

    # --- C: the strip is not a cell ------------------------------------------
    mo.rclick(rect[0] + 40, rect[1] + TITLE_H + MN_STRIP_H // 2)
    quiet(m)
    check("C strip ignored", w()["mn_flags"] == 0, "-> %r" % (w(),))

    # --- D: a flag still blocks the left button ------------------------------
    fx, fy = cell_xy(rect, 2, 2)
    mo.rclick(fx, fy)
    acted(m, lambda: w()["mn_flags"] != 0)
    mo.click(fx, fy)
    quiet(m)
    got = w()
    check("D flag blocks reveal",
          got["mn_flags"] == 1 and got["mn_revealed"] == 0
          and state(m, seg, 2 * MN_COLS + 2) == MN_S_FLAG, "-> %r" % (got,))

    # --- E: ...and the left button is otherwise untouched --------------------
    mo.click(*cell_xy(rect, 5, 5))
    acted(m, lambda: w()["mn_revealed"] > 0)
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
        quiet(m)
        got = w()
        check("F open cell refuses a flag",
              got["mn_flags"] == before
              and state(m, seg, open_cell) == MN_S_OPEN, "-> %r" % (got,))

    # --- G: a BACKGROUND window is raised, not played ------------------------
    mo.click(grect[0] + 40, grect[1] + TITLE_H // 2)        # GAMES to the front
    if not acted(m, lambda: frontmost(m) == games):
        sys.exit("minesrc: GAMES did not come to the front, so case G cannot "
                 "pose its question - a BACKGROUND Minesweeper is the whole "
                 "of what it tests")
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
        # THE RAISE IS THE GUEST'S ANSWER, and it is what makes the next line
        # an assertion rather than a coin toss: "flags unchanged" is true for
        # free until the press has been processed, so it is only worth reading
        # once the press HAS been - and the press's whole visible effect is
        # that Minesweeper comes to the front.
        raised = acted(m, lambda: frontmost(m) == slot)
        quiet(m)
        got = w()
        check("G raise does not flag",
              raised and got["mn_flags"] == before,
              "cell (0,%d) at (%d,%d) raised=%s -> %r"
              % (row, cx, cy, raised, got))
        # ...and the RAISE FINISHED before the next press: frontmost is set
        # at the top of the raise and its repaint runs after it, and on QEMU
        # a starved guest's 20 quiet ticks were not the whole of that - the
        # next press arrived mid-raise and flagged nothing
        # (docs/plans/SOAK-PARALLEL.md 17.5). The UI going idle is the end
        os88qemu.ui_done(m, S, cap=10.0, what="the raise")
        mo.rclick(cx, cy)                                   # now frontmost
        acted(m, lambda: w()["mn_flags"] != before)
        got = w()
        check("G the NEXT press flags",
              got["mn_flags"] == before + 1
              and state(m, seg, row * MN_COLS) == MN_S_FLAG, "-> %r" % (got,))

    print()
    for f in fails:
        print("minesrc: FAIL: %s" % f)
    if fails:
        return 1
    print("minesrc: OK - SPEC.md 13.11 dispatches, and only where it should")
    return 0


if __name__ == "__main__":
    sys.exit(main())
