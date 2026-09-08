#!/usr/bin/env python3
"""RELATIVE mouse driving on MartyPC: packets, dead reckoning, frame pacing.

    IF YOU ARE TRYING TO CLICK SOMETHING, THIS IS THE WRONG FILE.
    Use tools/os88mouse.py - it is ABSOLUTE, it READS the kernel's own
    published pointer, and it raises instead of missing:

        from os88mouse import Mouse
        mo = Mouse(marty=m)
        mo.dblclick(150, 90)

There are two mouse drivers here on purpose and picking the wrong one is the
single most expensive mistake in this harness, because it fails SILENTLY: a
dead-reckoned click lands three pixels outside a 16-pixel control, nothing
happens, and the script reports a broken feature twenty steps later.

  * `tools/os88mouse.py` - ABSOLUTE. The default, and what a TEST wants: it
    reads `mouse_x` out of the debug registry (SPEC.md 9.4.3), computes the
    exact remaining delta, and proves arrival. It never writes the cursor, so
    the UART, mou_isr and the packet decoder still do all the work.
  * this file - RELATIVE. What the DEVICE wants, which is a much shorter list.

WHEN RELATIVE IS THE RIGHT ANSWER - three cases, and they are the whole list:

  1. **The mouse itself is under test.** Packet decoding, SPEC.md 9.5's port
     contest, the ISR's own stack (SPEC.md 9.10), a button edge without a
     destination. Asking the kernel where the arrow is would be asking the
     thing under test to mark its own work.
  2. **A bit-exact REPLAY** (docs/plans/completed/SNAPSHOT-PLAN.md 7). Every wait here can be
     `advance(frames=N)` - a bounded amount of GUEST time - so two processes
     driven from reset by the same script land on the same cycle. A closed
     loop cannot promise that: it sends however many packets it needs, and how
     many that is depends on the host.
  3. **Motion with no destination** - a paint stroke, a window drag, a sweep.
     The path is the point; there is no (x, y) to converge on.

Everything else is case 0: you want a thing on the screen clicked, and
os88mouse is how.

WHY THIS ONE CANNOT AIM. A packet carries a SIGNED BYTE per axis, so a long
move is several packets and each is a chance to be rounded or coalesced; the
UART runs at 1200 baud, so a packet sent while the previous is in flight is
simply DROPPED; and the kernel clamps at the screen edge, which is what makes
`home()` work and also silently eats any overshoot you were counting on.
os88mouse.py's header has the long version.

WHAT IT STILL PROVES. Button EDGES, because a dropped press is the failure
that costs the most and costs it furthest away: `press`/`release` re-send and
then wait for the guest's published `mouse_btn` to agree. That is not aiming -
it reads a level the caller already asked for - so it is honest here.

AND IT NAMES ITS OWN DRIFT. `drift()` compares where this driver BELIEVES the
pointer is against where the kernel says it is. A dead-reckoned script that
has lost a packet is wrong from that moment on and looks fine; one line of
`drift()` after a navigation turns twenty steps of debugging into a number.
"""
import argparse
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from os88marty import Marty, MartyError                          # noqa: E402

# Frames, not seconds - `pace="frames"`. 60 frames is ~1 s of guest time on a
# 60 Hz card, and it is what makes a replay exact (docs/plans/completed/SNAPSHOT-PLAN.md 7).
F_SETTLE = 8       # after a move, for the packet to be decoded and drawn
F_CLICK = 8        # between press and release
F_AFTER = 18       # after a click, for the app to react

# ...and the wall-clock equivalents, for `pace="wall"`: a machine that is
# RUNNING while the script talks to it. 1200 baud, 3 bytes is ~25 ms, so GAP
# is the floor below which packets are dropped rather than sent.
GAP = 0.12
W_SETTLE = 0.25
W_AFTER = 0.6

STEP = 100         # the most one packet may carry per axis. A signed byte
                   # goes to 127; the margin is for the rounding the guest
                   # does, which is the thing a big delta most invites.


class Rel:
    """The relative pointer. Position is DEAD RECKONED from `home()`.

    Pacing is chosen once, at construction:

        Rel(m)                  # frames - reproducible, for a replay
        Rel(m, pace="wall")     # wall clock - for a free-running machine

    `advance(frames=)` stops the emulator, which is what makes it exact and
    also what makes it wrong for a script that wants the guest to keep
    working between packets (a paint stroke being profiled, say).
    """

    def __init__(self, m, pace="frames"):
        if pace not in ("frames", "wall"):
            raise ValueError("pace is 'frames' or 'wall', not %r" % (pace,))
        self.m = m
        self.pace = pace
        self.x = self.y = None          # None = origin unknown; home() sets it
        self._mo = None

    # --- pacing -------------------------------------------------------------
    def _wait(self, frames, secs):
        if self.pace == "frames":
            self.m.advance(frames=frames)
        else:
            time.sleep(secs)

    # --- the transport ------------------------------------------------------
    def packet(self, dx=0, dy=0, l=False, r=False):
        """ONE Microsoft 3-byte packet, clocked through the emulated UART.

        Verbatim: no clamping to STEP, no pacing beyond the UART's own floor,
        no proof. This is `Marty.mouse` with the gap that keeps the 8250 from
        dropping it - which is the whole reason to prefer it.
        """
        self.m.mouse(dx, dy, l=l, r=r)
        if self.pace == "wall":
            time.sleep(GAP)
        else:
            self.m.advance(frames=2)

    def move(self, dx, dy, l=False, r=False, step=STEP):
        """A long move as several packets - a packet carries a signed byte."""
        while dx or dy:
            sx = max(-step, min(step, dx))
            sy = max(-step, min(step, dy))
            self.packet(sx, sy, l, r)
            dx -= sx
            dy -= sy
        # ...and the running total is now UNKNOWN. `goto` keeps it because it
        # named a destination; a bare relative move cannot, since nothing here
        # can see whether the kernel's clamp swallowed the tail of it. Saying
        # so is the point: the next `goto` raises instead of aiming from a
        # position this driver only thinks it is at.
        self.x = self.y = None

    # --- an origin ----------------------------------------------------------
    def home(self):
        """Slam into the corner; the kernel's edge clamp pins it at (0,0).

        This is the ONLY way a relative driver gets an origin, and it works
        because the clamp eats the overshoot - which is the same property
        that makes every other overshoot invisible.
        """
        for _ in range(12):
            self.packet(-120, -120)
        self.x = self.y = 0
        self._wait(F_SETTLE, W_SETTLE)

    def goto(self, x, y, held=False, step=60):
        """Dead reckon to (x, y). Call `home()` first, or this raises."""
        if self.x is None:
            raise MartyError(
                "os88mouserel: no origin - call home() first. A relative "
                "driver cannot ask where it is without becoming the absolute "
                "one, which is tools/os88mouse.py.")
        dx, dy = x - self.x, y - self.y
        while dx or dy:
            sx = max(-step, min(step, dx))
            sy = max(-step, min(step, dy))
            self.packet(sx, sy, l=held)
            dx -= sx
            dy -= sy
        self.x, self.y = x, y
        self._wait(F_SETTLE, W_SETTLE)

    # --- buttons, PROVEN ----------------------------------------------------
    def _mouse(self):
        if self._mo is None:
            from os88mouse import Mouse
            self._mo = Mouse(marty=self.m)
        return self._mo

    def _edge(self, down):
        """One button edge, and then WAIT FOR THE GUEST TO AGREE.

        Shared with os88mouse rather than copied: a dropped press is the
        expensive failure on either driver, and re-sending is safe because a
        Microsoft packet carries the button's LEVEL and not an edge.
        """
        self._mouse()._edge(down)

    def press(self, l=True):
        self._edge(bool(l))

    def release(self):
        self._edge(False)

    def click(self, x=None, y=None):
        if x is not None:
            self.goto(x, y)
        self.packet(0, 0, l=True); self._wait(F_CLICK, W_SETTLE)
        self.packet(0, 0);         self._wait(F_AFTER, W_AFTER)

    def dblclick(self, x=None, y=None):
        # SPEC.md 13's double-click window is 9 ticks (~0.5 s), so the two
        # clicks have to be close in GUEST time - which is exactly what frame
        # pacing guarantees and what a wall-clock script cannot.
        if x is not None:
            self.goto(x, y)
        for _ in range(2):
            self.packet(0, 0, l=True); self._wait(3, 0.05)
            self.packet(0, 0);         self._wait(3, 0.05)
        self._wait(F_AFTER, W_AFTER)

    def menu(self, bar_x, bar_y, item_x, item_y):
        """Press on the bar, drag to the item, release - the only way menus
        are driven (SPEC.md 12).

        THE COORDINATES ARE THE PROBLEM, not the gesture: four numbers a
        script worked out by hand go stale the moment a menu gains an item.
        The kernel publishes menu_bar[] and os88ui.py reads it; come here
        only when the menu machinery itself is what is under test.
        """
        self.goto(bar_x, bar_y)
        self.packet(0, 0, l=True); self._wait(F_CLICK, W_SETTLE)
        self.goto(item_x, item_y, held=True)
        self._wait(F_CLICK, W_SETTLE)
        self.packet(0, 0); self._wait(F_AFTER, W_AFTER)

    # --- the diagnostic -----------------------------------------------------
    def drift(self):
        """(believed_x, believed_y, actual_x, actual_y, dx, dy).

        Dead reckoning is wrong from the first dropped packet onward and
        looks exactly like dead reckoning that is right. This asks the kernel
        once. It is a MEASUREMENT and never a correction - correcting is what
        the other driver is for, and a driver that quietly corrected would
        stop being reproducible, which is case 2's whole reason to exist.
        """
        if self.x is None:
            raise MartyError("os88mouserel: no origin - call home() first")
        ax, ay, _ = self._mouse().where()
        return (self.x, self.y, ax, ay, ax - self.x, ay - self.y)

    def check(self, slack=0):
        """`drift()` as an assertion. Raises naming both positions."""
        bx, by, ax, ay, dx, dy = self.drift()
        if abs(dx) > slack or abs(dy) > slack:
            raise MartyError(
                "os88mouserel: dead reckoning has drifted - this driver "
                "believes (%d,%d), the kernel says (%d,%d), off by (%+d,%+d). "
                "A packet was dropped or a clamp ate an overshoot; every "
                "click since the last home() landed somewhere else."
                % (bx, by, ax, ay, dx, dy))
        return (ax, ay)


def shot(m, path):
    from os88marty import write_png
    w, h, rows = m.vram()
    write_png(path, w, h, rows)
    return path


def main():
    ap = argparse.ArgumentParser(
        description="RELATIVE mouse packets. To click something, use "
                    "tools/os88mouse.py instead.")
    ap.add_argument("addr")
    ap.add_argument("--pace", choices=("frames", "wall"), default="wall")
    sub = ap.add_subparsers(dest="op", required=True)
    p = sub.add_parser("packet", help="one raw packet")
    p.add_argument("dx", type=int); p.add_argument("dy", type=int)
    p.add_argument("--left", action="store_true")
    p = sub.add_parser("move", help="a long relative move, several packets")
    p.add_argument("dx", type=int); p.add_argument("dy", type=int)
    p.add_argument("--left", action="store_true")
    sub.add_parser("home", help="pin at (0,0) against the kernel's clamp")
    sub.add_parser("press"); sub.add_parser("release")
    sub.add_parser("drift", help="believed position vs the kernel's")

    a = ap.parse_args()
    try:
        m = Marty(a.addr)
        r = Rel(m, pace=a.pace)
        if a.op == "packet":
            r.packet(a.dx, a.dy, l=a.left)
        elif a.op == "move":
            r.move(a.dx, a.dy, l=a.left)
        elif a.op == "home":
            r.home()
        elif a.op == "press":
            r.press()
        elif a.op == "release":
            r.release()
        elif a.op == "drift":
            r.home()
            print("believed (%d,%d), kernel says (%d,%d), off by (%+d,%+d)"
                  % r.drift())
            return 0
        x, y, b = r._mouse().where()
        print("%s -> cursor (%d,%d) buttons %02x" % (a.op, x, y, b))
    except MartyError as e:
        sys.stderr.write("os88mouserel: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
