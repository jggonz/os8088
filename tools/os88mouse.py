#!/usr/bin/env python3
"""ABSOLUTE mouse positioning on MartyPC, by closing the loop (SPEC.md 9.4.3).

    python3 tools/os88mouse.py 127.0.0.1:9001 where
    python3 tools/os88mouse.py 127.0.0.1:9001 to 445 153
    python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
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


class Mouse:
    def __init__(self, addr, timeout=60.0, verbose=False):
        self.m = Marty(addr, timeout=timeout)
        self.verbose = verbose
        self._cur = None

    # --- finding the cursor ------------------------------------------------
    def _rd(self, off, n):
        return self.m.readseg(KSEG, off, n)

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

    def to(self, x, y, tries=6, l=False):
        """Drive to (x, y) and PROVE it, or raise."""
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
    for name, n in (("to", 2), ("click", 2), ("drag", 4), ("menu", 4)):
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
