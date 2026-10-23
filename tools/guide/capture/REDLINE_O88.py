"""REDLINE's screenshots, for tools/guide/manuals/REDLINE_O88.md.

    python3 tools/guide/capture/run.py REDLINE_O88 --apps build/redline360.img

REDLINE.O88 is at the root of its own disk. One full benchmark on the
4.77 MHz VGA XT is a few minutes of guest time; the waits are on REDLINE's
own state (tools/redline_profile.py's Probe), never on a guess.
"""

import os88marty
import redline_profile as R

T = "REDLINE"


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    sym = R.symbols()
    ui.path("B:/REDLINE.O88")
    ui.settle()
    p = R.Probe(ui, sym)
    ui.mo.to(636, 470)

    _menu_shot(ui, shot, "Bench", "redline-menu")

    # Run: the Graphics Lab opens over the report while it works
    R.key(ui, "KeyR")
    R.wait(ui, lambda: any(w.title == "REDLINE Graphics Lab" for w in ui.windows()),
           "the Graphics Lab to open")
    R.wait(ui, lambda: p.word("rl_active_row") >= 18, "the run to reach the 3D rows")
    shot("redline-running", settle=False)
    ui.m.run()

    R.wait(ui, lambda: p.word("rl_runs") == 1 and p.data("rl_busy") == b"\0",
           "the benchmark to finish", seconds=1800)
    R.wait(ui, lambda: p.word("ru_anim") == 12, "the bars to finish")
    ui.m.run()
    shot("redline-summary", window=T, pad=4)

    R.key(ui, "KeyC")
    ui.m.run()
    shot("redline-compare", window=T, pad=4)

    R.key(ui, "KeyD")
    ui.m.run()
    shot("redline-detailed", window=T, pad=4)

    R.key(ui, "KeyH")
    ui.m.run()
    os88marty.guest_sleep(ui.m, 10.0)
    shot("redline-tune", window=T, pad=4)
