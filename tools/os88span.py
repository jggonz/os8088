#!/usr/bin/env python3
"""Price a span of KERNEL work in guest cycles, between named symbols.

    python3 tools/os88span.py raise    [machine]   # a covered Disk window raised
    python3 tools/os88span.py dragoff  [machine]   # a window dragged off another
    python3 tools/os88span.py paint    [machine]   # Paint repainted (needs pttest)
    python3 tools/os88span.py paintraise [machine] # ...and Paint RAISED (11.96.10)

This is the instrument PERFORMANCE.md Sets 30-34 were taken with, and it exists
because the two obvious alternatives cannot answer the question:

  `flicker` (PERFORMANCE.md Part 3.1) prices what a PERSON sees and cannot
  ATTRIBUTE it - a raise repaints the bar, the dock and the outgoing title bar
  too, so a frame count says a raise is dear and not which part of it is.

  Wall-clock says nothing at all: MartyPC runs the guest at whatever multiple of
  real time the host manages, measured between 3.8x and 4.8x in one session.

So it breakpoints named kernel symbols and reads `cycles` at each. The cycle
counter is exact in guest time on a cycle-accurate 8088, and the deltas are
therefore comparable between builds and quotable in milliseconds at 4.77MHz.

THREE THINGS COST A RUN EACH, and all three were learned the expensive way.

  ATTRIBUTE EVERY HIT TO A WINDOW. wm_su_try takes its window in DI and
  wm_grow_paint in BX, and a pair belonging to two different windows is not a
  decomposition of anything - the first attempt at Set 30 read "fill + palette =
  391 ms" out of two hits that turned out to be two different windows' paints.
  Every row below carries di= and bx= as window slots for that reason.

  DRIVE THE INPUT BEFORE ARMING, AND ONLY THE LAST PACKET AFTER. os88mouse
  converges by reading [mouse_x] and re-sending, so it cannot drive a machine
  that keeps stopping at a breakpoint; and raw packets sent back to back are
  dropped by the 1200-baud UART, which measures as an operation that never
  happened (a repaint with no restores in it at all). So each scenario positions
  and presses with nothing armed, and sends only the RELEASE - the packet that
  triggers the repaint - with the breakpoints up.

  A SYMBOL IS NOT WHERE THE LISTING SAYS. Addresses come from m.sym(), which uses
  nasm's own map and asserts byte-identity with build/kernel.bin; a knob-built
  kernel needs its -D passed (see os88sym.py --define).
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
from os88geom import WIN_SIZE                                # noqa: F401
import sucheck as su
import subcheck as sc

# pttest.img's root row to open: 2 = TEXTURE.BMP (runs of 3-8), 1 = FINE.BMP
# (runs of 1-2, so every run is inside one framebuffer byte - SPEC.md 5.4.1's
# narrowest case). tools/ptcheck.py reads the same variable.
PTROW = int(os.environ.get("PTROW", "2"))

# ...and how far to nudge Paint sideways before the raise. SPEC.md 5.4.1.2's
# aligned body is reachable only at an EVEN destination x, and Paint's template
# happens to put its canvas on one - so `PTNUDGE=21` (any odd number) is what
# prices the other phase. tools/ptcheck.py reads it too, and must, or the gate
# proves the pixels of one half of the routine.
PTNUDGE = int(os.environ.get("PTNUDGE", "0"))

CLK = 4772727.0                 # 4.77 MHz, so cycles -> ms
# WIN_SIZE was 26 here, and wm.inc has said 28 since SPEC.md 13.7 added
# W_ONMOUSEUP - so the `di`/`bx` window slots below were computed at the wrong
# stride and reported nonsense as if it were a window index. os88geom carries
# it now and checks itself against the kernel at import - the same fix this
# branch made by deriving the stride out of the .bss layout, and the better
# half of the two: it covers every mirrored constant rather than that one.


def ms(cycles):
    return 1000.0 * cycles / CLK


def collect(m, names, budget=200.0, limit=80):
    """Run, stopping at each armed breakpoint, until they stop arriving.

    Returns [(name, cycles, di_slot, bx_slot), ...]. The two slots are window
    indices when the register held a window record and nonsense otherwise -
    which is itself informative, being how you tell an unattributed hit.
    """
    marks = {n: m.sym(n) for n in names}
    by_addr = {}
    for n, a in marks.items():
        by_addr.setdefault(a, n)
    base = m.sym("wm_wins")
    out, t0 = [], time.time()
    for _ in range(limit):
        m.run()
        st = None
        while time.time() - t0 < budget:
            st = m.status()
            if st.get("state") == "breakpoint":
                break
            time.sleep(0.02)
        if not st or st.get("state") != "breakpoint":
            break
        flat = ((st["cs"] << 4) + st["ip"]) & 0xFFFFF
        r = m.regs()
        out.append((by_addr.get(flat, "?%05X" % flat), st["cycles"],
                    (r["di"] + 0x600 - base) // WIN_SIZE,
                    (r["bx"] + 0x600 - base) // WIN_SIZE))
    return out


def arm(m, names):
    return m.breakpoints([{"type": "exec", "addr": m.sym(n)} for n in names])


def report(hits):
    print("\n%-16s %14s %12s  %s" % ("breakpoint", "cycles", "delta (ms)",
                                     "window(di/bx)"))
    prev = None
    for name, cyc, di, bx in hits:
        d = "" if prev is None else "%.2f" % ms(cyc - prev)
        print("%-16s %14d %12s  %d / %d" % (name, cyc, d, di, bx))
        prev = cyc


def span(hits, a, b, slot=None):
    """The span from the first `a` to the LAST `b`, optionally for one window."""
    i = next((k for k, h in enumerate(hits)
              if h[0] == a and (slot is None or h[2] == slot)), None)
    j = next((k for k in range(len(hits) - 1, -1, -1)
              if hits[k][0] == b and (slot is None or hits[k][3] == slot)), None)
    if i is None or j is None or j <= i:
        return None
    return hits[j][1] - hits[i][1]


# --- the scenarios ----------------------------------------------------------
#
# Each returns (marks, trigger) where `trigger` is called with the breakpoints
# already armed and sends the one packet that starts the work being priced.

def sc_raise(m, mo):
    """Two Disk windows cascaded; raise the covered one through its dock tile.
    Prices the raise cache: wm_su_ck + wm_su_edge, then the blit (SPEC.md
    11.96/11.96.8). The dock tile, not the title bar - a cascaded window has no
    clickable title strip, and dock_click on a VISIBLE window is wm_front."""
    mo.dblclick(*su.zone(m, 1)); time.sleep(4)
    mo.dblclick(*su.zone(m, 0)); time.sleep(4)
    w = [x for x in su.windows(m) if x.visible]
    z = [i for i in sc.zorder(m) if i in [x.i for x in w]]
    by = {x.i: x for x in w}
    back = by[z[0]]
    print("raising %r" % (back,))
    mo.to(*su.tile(m, back))
    return (("wm_draw_win", "wm_su_try", "gfx_restore", "wm_grow_paint"),
            lambda: m.mouse(0, 0, l=True), back.i)


def sc_dragoff(m, mo):
    """Two Disk windows; drag the front one OFF the other, which is 11.96.6's
    damage pass. Dragged AWAY on purpose: dragged the other way 11.91.2 finds the
    window underneath wholly re-covered and marks nothing at all."""
    mo.dblclick(*su.zone(m, 1)); time.sleep(4)
    mo.dblclick(*su.zone(m, 0)); time.sleep(4)
    w = [x for x in su.windows(m) if x.visible]
    z = [i for i in sc.zorder(m) if i in [x.i for x in w]]
    by = {x.i: x for x in w}
    front, back = by[z[-1]], by[z[0]]
    p = sc.titlebar(m, front)
    sc.pdrag(mo, p[0], p[1], p[0] + 90, p[1] + 60)      # clear of the other
    f = [x for x in su.windows(m) if x.i == front.i][0]
    p = sc.titlebar(m, f)
    mo.to(*p)
    mo._edge(True)
    mo.to(p[0] + 70, p[1], l=True)
    print("dragging %r off %r" % (f, back))
    return (("gfx_restore", "gfx_sub_off", "wm_su_try"),
            lambda: m.mouse(0, 0, l=False), back.i)


def pt_nudge(m, mo, pt):
    """Drag Paint sideways by PTNUDGE and answer its window record again.
    An odd nudge flips the canvas's x PARITY, which is the whole point."""
    if not PTNUDGE:
        return pt
    tb = sc.titlebar(m, pt)
    if not tb:
        raise SystemExit("os88span: no title bar to nudge Paint by")
    sc.pdrag(mo, tb[0], tb[1], tb[0] + PTNUDGE, tb[1])
    os88marty.settle(m)
    pt = [w for w in su.windows(m) if w.visible and w.i == pt.i][0]
    print("nudged paint to %r" % (pt,))
    return pt


def sc_paintraise(m, mo):
    """Paint with a textured picture in it, COVERED by a Disk window and then
    raised - SPEC.md 11.96.10's case, and the one 11.90.2 could not reach.

    Before 11.96.10 a raise armed no rect at all, so OSAPI_WM_DAMAGE answered
    "whole" and the canvas was blitted entire however little of it had been
    covered. Needs build/pttest.img (tools/ptcheck.py's docstring)."""
    mo.dblclick(*su.zone(m, 1)); time.sleep(4)
    disk = [w for w in su.windows(m) if w.visible][0]
    mo.dblclick(*su.row(disk, PTROW)); time.sleep(45)
    pt = [w for w in su.windows(m) if w.visible
          and w.title.upper().startswith("PAINT")]
    if not pt:
        raise SystemExit("os88span: Paint did not launch - is pttest.img built?")
    pt = pt[0]
    pt = pt_nudge(m, mo, pt)
    sc.pclick(mo, *su.tile(m, disk))            # cover Paint
    os88marty.settle(m)
    print("raising paint %r from under %r" % (pt, disk))
    mo.to(*su.tile(m, pt))
    return (("wm_draw_win", "gfx_blit4", "wm_grow_paint"),
            lambda: m.mouse(0, 0, l=True), pt.i)


def sc_paint(m, mo):
    """Paint with a textured picture in it, then a window dragged off it.
    Needs build/pttest.img - see tools/ptcheck.py's docstring for how to build
    it. `paintraise` below is the RAISE half, which priced nothing until
    SPEC.md 11.96.10 gave wm_raise a rect to arm."""
    mo.dblclick(*su.zone(m, 1)); time.sleep(4)
    disk = [w for w in su.windows(m) if w.visible][0]
    mo.dblclick(*su.row(disk, PTROW)); time.sleep(45)
    pt = [w for w in su.windows(m) if w.visible
          and w.title.upper().startswith("PAINT")]
    if not pt:
        raise SystemExit("os88span: Paint did not launch - is pttest.img built?")
    pt = pt[0]
    sc.pclick(mo, *su.tile(m, disk))
    os88marty.settle(m)
    d = [w for w in su.windows(m) if w.visible and w.i == disk.i][0]
    p = sc.titlebar(m, d)
    mo.to(*p)
    mo._edge(True)
    mo.to(p[0] + 70, p[1] + 45, l=True)
    print("dragging %r off paint %r" % (d, pt))
    return (("wm_su_try", "gfx_blit4", "wm_grow_paint"),
            lambda: m.mouse(0, 0, l=False), pt.i)


def _sol_up(m, mo):
    """Solitaire off soltest.img (root row 0), with the Disk window it came
    from still open - which is the second window everything below needs."""
    mo.dblclick(*su.zone(m, 1))
    time.sleep(4)
    disk = [w for w in su.windows(m) if w.visible][0]
    mo.dblclick(*su.row(disk, 0))
    time.sleep(25)
    sol = [w for w in su.windows(m) if w.visible
           and w.title.upper().startswith("SOL")]
    if not sol:
        raise SystemExit("os88span: Solitaire did not launch - is "
                         "build/soltest.img built? (see tools/solcheck.py)")
    return sol[0], [w for w in su.windows(m) if w.visible
                    and w.i != sol[0].i][0]


def sc_sol(m, mo):
    """A Disk window dragged OFF Solitaire — reported from the field as
    "a drag and drop did not seem to use this buffer", which is what
    SPEC.md 11.96.4 was written for and is worth re-asking per window.

    `gfx_blit4` is the tell and it is Solitaire's alone: card backs are the
    only thing in this window that blits (SPEC.md 43.3/43.9), so a pass with a
    blit in it is a pass in which sol_drawall ran and the cache did not."""
    sol, disk = _sol_up(m, mo)
    p = sc.titlebar(m, disk)
    sc.pdrag(mo, p[0], p[1], sol.x + 40, sol.y + 60)     # ...onto Solitaire
    os88marty.settle(m)
    d = [w for w in su.windows(m) if w.visible and w.i == disk.i][0]
    p = sc.titlebar(m, d)
    mo.to(*p)
    mo._edge(True)
    mo.to(p[0] - 80, p[1] + 40, l=True)
    print("dragging %r off solitaire %r" % (d, sol))
    return (("wm_su_try", "gfx_restore", "gfx_blit4"),
            lambda: m.mouse(0, 0, l=False), sol.i)


def sc_solraise(m, mo):
    """...and the other half: Solitaire COVERED and then called to the front,
    through its dock tile (sc_raise's reasoning - a covered window's title
    strip may not be clickable at all)."""
    sol, disk = _sol_up(m, mo)
    sc.pclick(mo, *su.tile(m, disk))            # cover Solitaire
    os88marty.settle(m)
    print("raising solitaire %r from under %r" % (sol, disk))
    mo.to(*su.tile(m, sol))
    return (("wm_draw_win", "wm_su_try", "gfx_restore", "gfx_blit4"),
            lambda: m.mouse(0, 0, l=True), sol.i)


SCENARIOS = {"raise": (sc_raise, "os8088-360.img", "apps360.img"),
             "dragoff": (sc_dragoff, "os8088-360.img", "apps360.img"),
             "paint": (sc_paint, "os8088-360.img", "pttest.img"),
             "paintraise": (sc_paintraise, "os8088-360.img", "pttest.img"),
             "sol": (sc_sol, "os8088-360.img", "soltest.img"),
             "solraise": (sc_solraise, "os8088-360.img", "soltest.img")}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in SCENARIOS:
        raise SystemExit(__doc__)
    fn, boot, apps = SCENARIOS[sys.argv[1]]
    machine = sys.argv[2] if len(sys.argv) > 2 else "os8088_5150_cga_gla"
    with os88marty.launch(os.path.join(ROOT, "build", boot),
                          apps=os.path.join(ROOT, "build", apps),
                          machine=machine) as m:
        mo = Mouse(marty=m)
        print("machine %s / %s" % (machine, sys.argv[1]))
        names, trigger, slot = fn(m, mo)
        arm(m, names)
        trigger()
        hits = collect(m, names)
        m.breakpoints([])
        m.run()
        report(hits)
        print()
        for a, b in zip(names, names[1:]):
            c = span(hits, a, b)
            if c is not None:
                print("%-18s -> %-18s %9.2f ms" % (a, b, ms(c)))
        c = span(hits, names[0], names[-1], slot)
        if c is not None:
            print("%-18s -> %-18s %9.2f ms   (window %d only)"
                  % (names[0], names[-1], ms(c), slot))
        m.quit()


if __name__ == "__main__":
    main()
