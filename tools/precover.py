#!/usr/bin/env python3
"""The OPENING-WINDOW gate (SPEC.md 11.96.16 / 22.17) - does a program that
opens on top of another program leave that other program with a raise cache?

    python3 tools/precover.py capture DIR [machine [-D...]]
    python3 tools/precover.py diff DIR-A DIR-B

The reported sequence, in the reporter's words: *open the file manager, use it
to open Minesweeper, and from then on any action that affects the file manager
makes it redraw in full - it has no save-under.* SPEC.md 11.96.9.1 had already
measured that exact launch and recorded the row it did NOT fix:

    launch Minesweeper over it | take 1, dropped, fm_repaint 1 | same

Two things go wrong there and this gate covers both.

  22.17 - THE POSTER IS PAID TOO LATE. wm_show banks the Disk window with
    'Loading...' still on its status line, and files_poster then clears that
    word through wm_clip_set, which drops the cache - correctly, the window is
    about to draw. It cannot take a new one, because by then Minesweeper is on
    top of it. The line's text has been known since ui.inc consumed the click,
    so the loader pays it BEFORE wm_show instead.

  11.96.16 - ONLY THE FRONT WINDOW WAS EVER BANKED. wm_raise banks the outgoing
    front and nothing below it, so any other window the newcomer lands on that
    has no cache gets none. wm_su_precover asks every window the incoming rect
    touches, from wm_show, before the visible bit goes on.

THE FIRST CHECK IS wm_su_segs and it is the discriminating one: the poster's
slot must be non-zero after the launch, where before this it was guaranteed to
be 0. But a cache holding the WRONG pixels is invisible until it is used
(SPEC.md 11.96.7), so the session is also driven through `make REDRAWFULL=1`
and compared frame by frame:

    rm -f build/apps360.img && make REDRAWFULL=1
    python3 tools/precover.py capture /tmp/pc-ref <machine> REDRAWFULL
    rm -f build/apps360.img && make
    python3 tools/precover.py capture /tmp/pc-new <machine>
    python3 tools/precover.py diff /tmp/pc-ref /tmp/pc-new

0 differing pixels is the standard, on CGA, Hercules and VGA mode 12h.
REDRAWFULL builds wm_su_precover out entirely, so the reference really is the
tree without this change rather than the same code driven differently.

WHAT THIS DOES NOT MEASURE IS THE SAVING, deliberately. A cache holding the
right pixels and a full fm_repaint land the same picture - that is the point of
the restore - so neither the screen nor wm_su_segs can tell them apart, and the
obvious instrument does not work either: a guest-cycle count across the raise
is dominated by the harness's own wall-clock wait, MartyPC running the guest
headless as fast as it can. Measured, it came out at 68-77 MILLION cycles for
an operation SPEC.md 11.96 prices at 1,026 ms = ~4.9M, and it barely moved
between a kernel with the cache and one built without it. The saving is 11.96's
own figure; this file's job is that the cache EXISTS and that it puts back the
right pixels. PERFORMANCE.md Part 3.1's flicker instrument is what would
measure the visible redraw, and it wants its own session.

Every coordinate is derived from wm_wins / wm_zord / desk_ord_xy, so this runs
on all three adapters; subcheck.py's masks (the menu bar, the mouse cell) and
its proven-edge click and drag are reused rather than re-rolled.
"""
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
import sucheck as su
import subcheck as sc
from os88geom import MAX_WIN

VOL_B = 1                       # the apps floppy
ROW_GAMES = 1                   # B: root, sorted: APPS GAMES MEDIA SYSTEM
ROW_MINES = 2                   # GAMES/, sorted: .. ARKANOID MINES MISSILE ...


def segs(m):
    """wm_su_segs - one word per window slot, 0 = this window has no cache."""
    raw = m.read(m.sym("wm_su_segs"), MAX_WIN * 2)
    return list(struct.unpack("<%dH" % MAX_WIN, raw))


def note(log, text):
    print("    %s" % text)
    log.append(text)


def capture(out, machine, defines=()):
    os.makedirs(out, exist_ok=True)
    shots, checks = [], []
    ref = "REDRAWFULL" in defines
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        # The reference kernel is a different binary, so every symbol has to be
        # looked up with the knob that built it (subcheck.py's own note).
        if defines:
            plain = m.sym
            m.sym = lambda n, d=tuple(defines): plain(n, d)
        mo = Mouse(marty=m)
        print("machine %s -> %s%s" % (machine, out, "  [reference]" if ref else ""))
        sc.shot(m, "desktop", out, shots, mo)

        # --- the reported sequence -------------------------------------------
        mo.dblclick(*su.zone(m, VOL_B)); time.sleep(4)
        sc.shot(m, "disk-b", out, shots, mo)
        b = sc.wins(m)[-1]
        mo.dblclick(*su.row(b, ROW_GAMES)); time.sleep(5)
        sc.shot(m, "into-games", out, shots, mo)

        b = [w for w in sc.wins(m) if w.i == b.i][0]
        mo.dblclick(*su.row(b, ROW_MINES)); time.sleep(9)
        sc.shot(m, "mines-launched", out, shots, mo)

        # --- CHECK 1: the poster kept a cache through the launch (22.17) -----
        # It is the window the newcomer covers, and until this it was the one
        # window in the system guaranteed NOT to have one.
        cache = segs(m)[b.i]
        note(checks, "poster wm_su_segs after launch = %04X" % cache)
        if not ref and cache == 0:
            raise SystemExit(
                "precover: FAIL - the Disk window that launched Minesweeper has\n"
                "no raise cache (wm_su_segs[%d] = 0). That is the reported bug:\n"
                "every later action on it is a full fm_repaint (SPEC.md 22.17)."
                % b.i)

        mines = [w for w in sc.wins(m) if w.i != b.i]
        if not mines:
            raise SystemExit("precover: Minesweeper did not open - %r"
                             % (sc.wins(m),))
        mines = mines[-1]
        # RECTS, not sampled corners: the first version tested four points of
        # the Disk window and reported "does not overlap" about a Minesweeper
        # sitting squarely in the middle of it.
        if not (mines.x <= b.x + b.w and b.x <= mines.x + mines.w
                and mines.y <= b.y + b.h and b.y <= mines.y + mines.h):
            raise SystemExit("precover: Minesweeper %r does not land on %r, so "
                             "the session proves nothing" % (mines, b))

        # --- the drags 11.96.9.1 measured, FIRST: they need Minesweeper on
        #     top, and the raise below puts the Disk window there. Ordered the
        #     other way round, sc.titlebar had no strip to click and the two
        #     steps silently did not happen.
        pt = sc.titlebar(m, mines)
        if pt is None:
            raise SystemExit("precover: no clickable title strip on %r" % (mines,))
        sc.pdrag(mo, pt[0], pt[1], pt[0] - 60, pt[1] + 30)
        sc.shot(m, "mines-dragged-off", out, shots, mo)
        f = [w for w in sc.wins(m) if w.i == mines.i][0]
        pt = sc.titlebar(m, f)
        sc.pdrag(mo, pt[0], pt[1], pt[0] + 60, pt[1] - 30)
        sc.shot(m, "mines-dragged-back", out, shots, mo)
        note(checks, "poster wm_su_segs after drags = %04X" % segs(m)[b.i])

        # --- CHECK 2/3: raising it back, and what that costs the guest -------
        pt = sc.titlebar(m, b)
        if pt is None:
            raise SystemExit("precover: no clickable title strip on the Disk "
                             "window - %r under %r" % (b, mines))
        sc.pclick(mo, pt[0], pt[1], settle=0)
        os88marty.settle(m)             # two identical rendered frames a second
                                        # apart, rather than a fixed sleep
        sc.shot(m, "disk-raised", out, shots, mo)
        note(checks, "poster wm_su_segs after raise = %04X" % segs(m)[b.i])

        # --- CHECK 3: 11.96.16's own pass, on a window that is NOT the front --
        # Minimizing Minesweeper leaves the Disk window front with its cache
        # spent; restoring it from the dock is a wm_show, and wm_su_precover is
        # what banks whatever that restore is about to land on.
        f = [w for w in sc.wins(m) if w.i == mines.i][0]
        sc.pclick(mo, f.x + f.w - 14, f.y + 9)
        sc.shot(m, "mines-minimized", out, shots, mo)
        sc.pclick(mo, *su.tile(m, mines))
        sc.shot(m, "mines-restored", out, shots, mo)
        note(checks, "poster wm_su_segs after restore = %04X" % segs(m)[b.i])

        with open(os.path.join(out, "checks.txt"), "w") as fh:
            fh.write("\n".join(checks) + "\n")
        m.quit()
    print("captured %d step(s)" % len(shots))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "capture":
        capture(sys.argv[2],
                sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla",
                sys.argv[4:])
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        sc.diff(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
