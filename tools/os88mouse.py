#!/usr/bin/env python3
"""ABSOLUTE mouse positioning on MartyPC, by closing the loop (SPEC.md 9.4.3).

    python3 tools/os88mouse.py 127.0.0.1:9001 where
    python3 tools/os88mouse.py 127.0.0.1:9001 to 445 153
    python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
    python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90
    python3 tools/os88mouse.py 127.0.0.1:9001 menu 12 8 40 45
    python3 tools/os88mouse.py 127.0.0.1:9001 drag 200 78 200 120

WHY THIS EXISTS. MartyPC's mouse is RELATIVE and deliberately so: the `mouse`
command clocks a real 3-byte Microsoft packet through the emulated UART, so a
scripted click drives mou_isr and the packet decoder exactly as a hand on a
real mouse would. What it cannot do is *aim*. Every script that wanted a
button at (x, y) drove hard into a corner to pin the cursor against the
kernel's own edge clamp and then stepped out by the difference - dead
reckoning - and dead reckoning drifts:

  * a packet carries a SIGNED BYTE per axis, so a long move is several
    packets and each one is a chance to be rounded or coalesced;
  * the UART runs at 1200 baud, so packets sent faster than ~25 ms apart
    queue up, and one sent while the previous is in flight can be lost;
  * the kernel clamps at the screen edge, which is what makes pinning work
    and also silently eats any overshoot you were counting on.

The failure is SILENT and expensive: the click lands three pixels outside a
16-pixel control, nothing happens, and the harness reports a broken feature.
That has cost several sessions real time, which is why this is a tool and not
a snippet.

HOW IT CLOSES THE LOOP. The kernel publishes a pointer to `mouse_x` in the
debug registry's 'MO' block (SPEC.md 9.4.2/9.4.3), so this can READ where the
cursor actually is, compute the exact remaining delta, send it, and read
again. Two packets is the usual cost. When it cannot converge it SAYS SO and
exits non-zero, instead of clicking into empty desktop.

It never WRITES the cursor, and must not learn how. A poke to mouse_x would
skip the UART, mou_isr and the decoder - the three things a scripted click is
there to exercise. The packet still does all the work; the registry only says
where it landed.

DOUBLE-CLICKS ARE A VERB OF THEIR OWN, and cannot be composed out of two
`click`s - that is the trap this closes. Two things defeat the obvious
spelling, and they pull in opposite directions:

  * `click` ends in a 1.5 s settle, so two of them are a second and a half
    apart and the kernel sees two FIRST clicks. Every double-click detector
    in the system (SPEC.md 22/26/38 and ui_tdbl's title bar) compares the two
    presses' BIRTH TICKS against a 9-tick window - about half a second.
  * ...and packets sent faster than the 1200-baud UART can carry them are
    DROPPED, so simply removing the sleep gets one press decoded instead of
    two. The guest then sees a single click, which for a file row is a
    selection instead of a launch: the feature looks broken and the harness
    says nothing.

So `dblclick` PROVES each of the four button edges - it sends the packet and
then polls the published `mouse_btn` until the level agrees, which is `to`'s
own discipline applied to the button instead of the position - and then
measures the span between the two presses in the guest's OWN 18.2 Hz ticks,
read from the BIOS counter at 0040:006C. That is the same clock and the same
units the kernel compares, so the check is the kernel's rather than a guess
about host timing. Too slow, or an edge that never arrived, RAISES.
"""
import argparse
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from os88marty import Marty, MartyError                    # noqa: E402

REG_AT = 0x0600 + 0x0E          # 0060:000E - the debug registry (SPEC.md 57)
KSEG = 0x0060
TAG_MOUSE = 0x4F4D              # 'MO'

# A packet is a signed byte per axis; the kernel's own scripted-input note and
# tools/mouse.py both cap well under that, because a big delta is the thing
# the guest is most likely to round.
STEP = 100
# 1200 baud, 3 bytes: ~25 ms of guest time. Below this the server blocks and
# the client times out, which reads exactly like a hung emulator.
GAP = 0.12
SETTLE = 0.25                   # let the ISR and the UI task see the packet

# The BIOS tick count, 18.2 Hz. A fact about the PC rather than about os8088 -
# sch_isr chains the ROM's handler (SPEC.md 7), so this advances on every
# machine and cannot drift with the kernel the way a .bss offset would.
BIOS_TICKS = 0x0046C            # 0040:006C
# ...and the window every double-click detector in the system uses: UI_TDBLT
# in kernel/ui.inc, DESK_DBLT, FM_DBLCLK, FD_DBLCLK - deliberately all the
# same half second (SPEC.md 22/26/38).
DBL_TICKS = 9


class Mouse:
    def __init__(self, addr=None, timeout=60.0, verbose=False, marty=None):
        # SHARE a connection when one is offered. The debug server takes a
        # single client, so a script that wants the mouse driver AND the
        # framebuffer must not build two Martys - the second does not error,
        # it hangs until the read times out, which reads as a wedged guest.
        #
        #     with os88marty.launch(img, apps=apps) as m:
        #         mo = Mouse(marty=m)
        #         mo.dblclick(150, 90)
        #         m.vram("cga")            # ...the same connection
        if marty is None:
            if addr is None:
                raise MartyError("Mouse needs an addr or an open marty=")
            marty = Marty(addr, timeout=timeout)
        self.m = marty
        self.verbose = verbose
        self._cur = None

    # --- finding the cursor ------------------------------------------------
    def _rd(self, off, n):
        return self.m.readseg(KSEG, off, n)

    def _raw(self, addr, n):
        return self.m.read(addr, n)         # linear, for the BIOS data area

    def _word(self, off):
        b = self._rd(off, 2)
        return b[0] | (b[1] << 8)

    def cursor_ptr(self):
        """Walk the debug registry for tag 'MO' and take its fourth word."""
        if self._cur is not None:
            return self._cur
        tab = self._word(0x000E)
        if not tab:
            raise MartyError("no debug registry at 0060:000E - is os8088 up?")
        off = tab
        while True:
            tag = self._word(off)
            if tag == 0:
                raise MartyError("no 'MO' block in the debug registry")
            blk = self._word(off + 2)
            if tag == TAG_MOUSE:
                if self._word(blk) != TAG_MOUSE:
                    raise MartyError("the 'MO' block does not name itself - "
                                     "the registry offset is stale")
                ptr = self._word(blk + 6)      # +0 magic, +2 bases, +4 state
                if not ptr:
                    raise MartyError(
                        "this kernel's 'MO' block has no cursor word: it "
                        "predates SPEC.md 9.4.3. Rebuild it.")
                self._cur = ptr
                return ptr
            off += 4

    def where(self):
        p = self.cursor_ptr()
        b = self._rd(p, 5)
        return (b[0] | (b[1] << 8), b[2] | (b[3] << 8), b[4])

    # --- moving ------------------------------------------------------------
    def _pk(self, dx=0, dy=0, l=False):
        self.m.mouse(dx, dy, l=l)
        time.sleep(GAP)

    def to(self, x, y, tries=None, l=False):
        """Drive to (x, y) and PROVE it, or raise."""
        if tries is None:
            # A packet moves at most STEP per axis, so the budget has to scale
            # with the DISTANCE: a fixed six was enough for a short hop and
            # silently too few to cross the screen, which reported a target as
            # unreachable while walking steadily towards it.
            cx, cy, _ = self.where()
            far = max(abs(x - cx), abs(y - cy))
            tries = (far + STEP - 1) // STEP + 4
        for n in range(tries):
            cx, cy, _ = self.where()
            dx, dy = x - cx, y - cy
            if dx == 0 and dy == 0:
                if self.verbose:
                    print("  at (%d,%d) after %d correction(s)" % (x, y, n))
                return
            # One packet's worth at a time; the read after it is what makes
            # the next one exact rather than hopeful.
            self._pk(max(-STEP, min(STEP, dx)), max(-STEP, min(STEP, dy)), l=l)
            time.sleep(SETTLE)
        cx, cy, _ = self.where()        # ...and CHECK AFTER THE LAST MOVE, or
        if (cx, cy) == (x, y):          # a target needing exactly `tries`
            return                      # packets is reported as unreachable
                                        # while sitting precisely on it
        raise MartyError("could not reach (%d,%d): stuck at (%d,%d). A target "
                         "outside the screen, or off the kernel's clamp, "
                         "cannot be reached." % (x, y, cx, cy))

    def click(self, x, y, settle=1.5):
        self.to(x, y)
        self._pk(l=True)
        self._pk()
        time.sleep(settle)

    # --- clicking that PROVES itself ---------------------------------------
    def ticks(self):
        """The guest's own 18.2 Hz tick count (0040:006C)."""
        b = self._raw(BIOS_TICKS, 4)
        return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)

    def _edge(self, down, tries=60):
        """One button edge, PROVEN: send the packet, then wait until the
        published mouse_btn agrees. A packet clocked into the UART while the
        previous one is still in flight is simply dropped, and the only
        difference a caller can see is that the guest did nothing."""
        self.m.mouse(0, 0, l=down)
        want = 1 if down else 0
        for _ in range(tries):
            if (self.where()[2] & 1) == want:
                return
            time.sleep(0.02)
        raise MartyError(
            "the %s was never decoded (mouse_btn = %02x). The 1200-baud UART "
            "drops a packet sent while the previous one is in flight."
            % ("press" if down else "release", self.where()[2]))

    def dblclick(self, x, y, settle=2.0):
        """Two presses inside the kernel's own double-click window.

        NOT two `click`s: that spelling is a second and a half apart and reads
        as two first clicks. See the module docstring.
        """
        self.to(x, y)
        if self.where()[2] & 1:         # a button left down by something else
            self._edge(False)           # would make the first press no edge
        self._edge(True)
        t1 = self.ticks()
        self._edge(False)
        self._edge(True)
        t2 = self.ticks()
        self._edge(False)
        span = (t2 - t1) & 0xFFFFFFFF
        if span >= DBL_TICKS:
            raise MartyError(
                "the two presses were %d ticks apart and the window is %d: "
                "the guest saw two FIRST clicks, not a double-click. Something "
                "between them was slow - a mount, a package load, or a host "
                "that cannot keep up." % (span, DBL_TICKS))
        if self.verbose:
            print("  double-click at (%d,%d): %d tick(s) apart" % (x, y, span))
        time.sleep(settle)
        return span

    def menu(self, x0, y0, x1, y1, settle=2.0):
        """Press on the bar, drag to the item, release (SPEC.md 12).

        A menu cannot be opened with a click: menu_track draws the pull-down
        and then polls a level, so a press-and-release in place opens it and
        closes it in the same breath - which is SPEC.md 9.6.1's flashing menu
        seen from the harness side.
        """
        self.to(x0, y0)
        self._pk(l=True)
        self.to(x1, y1, l=True)
        self._pk(l=True)
        self._pk()
        time.sleep(settle)

    def drag(self, x0, y0, x1, y1, settle=1.5):
        self.to(x0, y0)
        self._pk(l=True)
        self.to(x1, y1, l=True)
        self._pk(l=True)
        self._pk()
        time.sleep(settle)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("-q", "--quiet", action="store_true")
    sub = ap.add_subparsers(dest="op", required=True)
    sub.add_parser("where")
    for name, n in (("to", 2), ("click", 2), ("dblclick", 2),
                    ("drag", 4), ("menu", 4)):
        p = sub.add_parser(name)
        p.add_argument("coords", type=int, nargs=n)
    a = ap.parse_args()

    try:
        mo = Mouse(a.addr, verbose=not a.quiet)
        if a.op == "where":
            x, y, b = mo.where()
            print("cursor (%d,%d) buttons %02x" % (x, y, b))
        elif a.op == "to":
            mo.to(*a.coords)
        elif a.op == "click":
            mo.click(*a.coords)
        elif a.op == "dblclick":
            mo.dblclick(*a.coords)
        elif a.op == "drag":
            mo.drag(*a.coords)
        elif a.op == "menu":
            mo.menu(*a.coords)
        if a.op != "where" and not a.quiet:
            x, y, _ = mo.where()
            print("%s -> cursor (%d,%d)" % (a.op, x, y))
    except MartyError as e:
        sys.stderr.write("os88mouse: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
