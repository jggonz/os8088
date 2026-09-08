#!/usr/bin/env python3
"""The SMALL Task Manager still is one, and its click no longer cycles.

    make smallapps && python3 tests/tmsmall.py

SPEC.md 28.12 gates two of this window's three pages out of the `APP_SMALL`
arm, which is 4,445 bytes of a single heap claim - 39.9%, the largest saving
of any package in the tree. `tests/unit/t_appsmall.py` says that host-side,
off the two headers, and it is the wrong instrument for what is actually at
risk here: **this gate removes PAGES, and the two that go share their row
machinery, their check words and their bss chain with the one that stays.**
An `%ifdef` written one line wide takes a routine the performance view still
calls, and every host-side check passes - the package is smaller, the full
build is identical, the disk carries the right file - while the window that
opens on the floor machine is blank.

So this drives it. **AND IT IS AN A/B**, because neither claim can be read
off one arm:

* The small arm draws page 0 and a content click leaves it there. Alone that
  is also what a window whose click handler is broken does, and what one
  drawing nothing at all does.
* The full arm draws the same page 0 - **pixel for pixel**, which is the
  claim that the gates took nothing out of the page that stays - and the
  identical click moves it to the memory view.

Together they say the click still arrives, the handler still runs, and the
only difference is that there is nowhere for it to go (§28.12).

THE LIST HEADER IS WHAT IS COMPARED, and it is chosen rather than convenient:
it is the one band of this window that is STATIC on a page and DIFFERENT on
each - `NAME ST CPU MEM CLM` against the memory view's `NAME ADDR SIZE HEAP`
(§28.5.2). Everything above it moves by construction (a percentage, a graph
column, a RAM figure), so a whole-content diff would answer "changed" on both
arms and mean nothing.

**IT RUNS ON kern_big, DELIBERATELY.** §27.16's central claim is that a small
build is not a second ABI - it calls the same table at the same offsets and
runs on either kernel - so the shipped kernel is where the two arms can be
compared with one variable between them. `tests/smallboot.py` is where
kern_small itself is booted; putting both questions in one row would answer
neither.

`tick` and not `settle`, for tmground.py's reason: the performance view
animates, so this window never stills.
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88geom                                             # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispcorner                                           # noqa: E402
from harness import check, done                             # noqa: E402

MACHINE = "os8088_xt_vga"
KERNEL = "build/os8088-360.img"
S = os88sym.linear

# (arm, the apps floppy it is on, does a content click cycle the page?)
ARMS = [("full", "build/apps360.img", True),
        ("small", "build/smallapps360.img", False)]

# The process list's header line, content-relative (TM_HDR_Y, §28). Eight rows
# is one glyph cell.
HDR_Y, HDR_H = 87, 8


def tick(mm, card=None):
    """A settle substitute: the performance view animates (tmground.py)."""
    mm.advance(frames=110)
    mm.run()


def rows_of(m):
    """The RENDERED framebuffer as rows of 0/1.

    `fbuf` and not `vram`: this runs on a VGA, where mode 12h is four planes
    behind the Graphics Controller and there is no flat framebuffer in guest
    memory to read at all (tools/os88marty.py).
    """
    w, h, rgb = m.fbuf()
    return w, h, [[1 if rgb[(y * w + x) * 3] else 0 for x in range(w)]
                  for y in range(h)]


def band(rows, x1, y1, x2, y2):
    return tuple(tuple(r[x1:x2 + 1]) for r in rows[y1:y2 + 1])


def arm(name, apps, cycles):
    """Open the Task Manager off `apps` and answer (header band, notes)."""
    with os88marty.launch(KERNEL, apps=apps, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        dx, dy, _, _ = dispcp.win_rect(m, S, disk)
        dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
        dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
        tick(m)

        wins = [w for w in os88geom.windows(m) if "Task" in w.title]
        check(bool(wins), "%s: the Task Manager opened" % name,
              "the small arm is a package the loader has never been handed "
              "before; a header the loader refuses never gets as far as a "
              "window", got=[w.title for w in os88geom.windows(m)],
              want="a window titled Task Manager")
        if not wins:
            return None
        w = wins[-1]
        x1, y1, x2, y2 = w.content

        _, _, rows = rows_of(m)
        content = band(rows, x1, y1, x2, y2)
        white = sum(1 for r in content for v in r if v)
        frac = white / float((x2 - x1 + 1) * (y2 - y1 + 1))
        check(0.60 < frac < 0.995, "%s: page 0 is drawn" % name,
              "the ground is white and the ink is black (§5), so an ALL-WHITE "
              "content is a window that filled and never lettered - which is "
              "exactly what a gate one line too wide leaves behind - and an "
              "all-black one never got its ground",
              got="%.4f white" % frac, want="0.60 .. 0.995")

        hdr = band(rows, x1, y1 + HDR_Y, x2, y1 + HDR_Y + HDR_H - 1)

        graph = (x1 + 7, y1 + 15, x1 + 66, y1 + 54)
        g0 = band(rows, *graph)
        time.sleep(3.0)
        _, _, rows2 = rows_of(m)
        check(g0 != band(rows2, *graph), "%s: the window is still live" % name,
              "the worker samples every TM_INT and pushes one history column "
              "(§28.7). A frozen graph is a worker that never got its task "
              "slot, or one that died in a routine the gates took out",
              got="MOVED" if g0 != band(rows2, *graph) else "FROZE",
              want="MOVED")

        mo.click(x1 + 20, y1 + 40)          # the content, left of any bar
        mo.to(*dispcorner.PARK)
        tick(m)
        _, _, rows3 = rows_of(m)
        hdr2 = band(rows3, x1, y1 + HDR_Y, x2, y1 + HDR_Y + HDR_H - 1)
        moved = hdr != hdr2
        check(moved == cycles, "%s: a content click %s the page"
              % (name, "cycles" if cycles else "does NOT cycle"),
              "§28.4's one click target against §28.12's one page. The full "
              "arm is what says this test can see a cycle at all",
              got="the list header %s" % ("CHANGED" if moved else "held"),
              want="CHANGED" if cycles else "held")
        return hdr


def main():
    seen = {}
    for name, apps, cycles in ARMS:
        if not os.path.exists(os.path.join(ROOT, apps)):
            check(True, "%s: %s not built - skipped (`make smallapps`)"
                  % (name, apps))
            continue
        seen[name] = arm(name, apps, cycles)

    if seen.get("full") is not None and seen.get("small") is not None:
        diff = sum(1 for a, b in zip(seen["full"], seen["small"])
                   for u, v in zip(a, b) if u != v)
        check(diff == 0, "the page that STAYS is pixel-for-pixel the full "
                         "build's",
              "the gates are supposed to remove pages, not to change the one "
              "left. Both arms are the same source and the same kernel, and "
              "this band is static on a page - so any difference at all is a "
              "gate that reached into the performance view",
              got="%d differing subpixels" % diff, want=0)
    done("tmsmall")


if __name__ == "__main__":
    main()
