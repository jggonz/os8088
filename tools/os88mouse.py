#!/usr/bin/env python3
"""ABSOLUTE mouse positioning on MartyPC, by closing the loop (SPEC.md 9.4.3).

    python3 tools/os88mouse.py 127.0.0.1:9001 where
    python3 tools/os88mouse.py 127.0.0.1:9001 to 445 153
    python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
    python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90
    python3 tools/os88mouse.py 127.0.0.1:9001 menu 12 8 40 45
    python3 tools/os88mouse.py 127.0.0.1:9001 drag 200 78 200 120

THIS IS THE DEFAULT. Reach for it whenever a script wants something on the
screen clicked. The RELATIVE driver is tools/os88mouserel.py and it is for a
much shorter list - the mouse itself under test, a bit-exact replay, or motion
with no destination (a paint stroke, a window drag). Its header has the whole
of that list; if your case is not on it, you are in the right file.

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
import os
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import os88marty                                           # noqa: E402
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
# WHAT ONE PACKET IS ALLOWED, in GUEST seconds. `to` used to sleep a fixed
# 0.25 HOST seconds after each one - about 0.9 guest seconds on an idle box
# here and 0.25 on a full one, so the same click bought the machine four times
# less work under load and then failed somewhere further on. It reads the
# pointer back instead (`_landed`), which on a quiet guest answers in a few
# milliseconds; this is only the deadline, and reaching it means the packet
# was dropped - which `to`'s loop handles by sending another.
PKT_GUEST = 1.0
BUSY = 6.0                      # ...and how long a repaint may hold the guest
                                # before `to` gives up on it - GUEST seconds
                                # now, for the same reason. A window
                                # straddling two displays repainting on a
                                # 4.77MHz 8088 is the worst case measured

# --- GUEST PACING (off by default) ------------------------------------------
#
# `click(settle=1.5)` waits 1.5 HOST seconds for the guest to act on the
# press.  How much guest work that buys is a property of the box.  Measured on
# this container over 118 waits in each arm - the same waits in the same
# scripts - the median HOST cost was 2.2s in both and the GUEST cost 7.3s
# against 5.9s, up to -37% per script.  The row does not get slower (measured: 1.06x
# wall across twelve rows), it gets LESS THOROUGH, and then fails somewhere
# further on looking like the thing under test.  That is the mechanism behind
# docs/plans/HANDOFF-SOAK-FINDINGS.md B5, and it is why "it passed alone" has been
# such an unsatisfying diagnosis: the wall times never showed anything.
#
# `OS88_GUEST_PACE=<ratio>` spends the same wait in GUEST seconds instead -
# `settle * ratio` of the machine's own time - so a click buys the same work
# whatever else the box is doing.
#
# IT IS OFF BY DEFAULT, and that is deliberate rather than timid.  B5 says
# rewriting these waits onto guest time "reaches 194 files, changes how much
# guest work every row gets per settle, and would want a full soak behind it".
# That is right, and a knob is how this project takes a change of that shape:
# the arm exists, it is measurable against the default, and the flip is a
# decision somebody makes with a soak behind it rather than one that happens
# quietly here.  Set it to the box's own idle ratio to reproduce today's
# coverage exactly; below that and rows get less guest time than they do now.
GUEST_PACE = float(os.environ.get("OS88_GUEST_PACE", "0"))


def _wait(m, secs, why="click"):
    """Spend `secs`, in host time or - under OS88_GUEST_PACE - in guest time.

    One function so that every wait in this file moves together: a run where
    the click is guest-paced and the drag is not is a run whose contention
    behaviour nobody can reason about.

    IT LOGS TO OS88_WAITLOG TOO, and that is what makes the log worth
    reading. `settle` and `until` are waits FOR something and end the moment
    it happens; these are UNCONDITIONAL - the wait IS the cost, every time,
    whether or not the guest needed it. A profile recording only the
    conditional half accounts for the part of a row that is already efficient
    and is silent about the part that is pure margin. The `fixed` rows are
    the ones to cut.

    What it records is what the wait BOUGHT: guest seconds across the sleep.
    That is the number a decision needs - "1.5 host seconds bought 5.2 guest
    seconds of a machine that needed 0.4" is an argument; "1.5 seconds" is
    not.
    """
    log = bool(os88marty.WAITLOG) and m is not None
    c0 = None
    if log:
        try:
            c0 = int(m.status().get("cycles", 0))
        except Exception:
            c0 = None
    t0 = time.time()
    if GUEST_PACE > 0 and m is not None:
        os88marty.guest_sleep(m, secs * GUEST_PACE)
    else:
        time.sleep(secs)
    if log:
        try:
            c1 = int(m.status().get("cycles", 0))
            guest = 0.0 if c0 is None else (c1 - c0) / os88marty.GUEST_HZ
            host = time.time() - t0
            with open(os88marty.WAITLOG, "a") as f:
                f.write("fixed\t%s\t%.2f\t%.2f\t%.3f\t%s %.2fs\n"
                        % (os88marty._caller(), guest, host,
                           guest / host if host > 0.01 else 0.0, why, secs))
        except Exception:
            pass


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
    def _pk(self, dx=0, dy=0, l=False, r=False):
        self.m.mouse(dx, dy, l=l, r=r)
        time.sleep(GAP)

    def _landed(self, was, guest=PKT_GUEST):
        """Wait for the published pointer to leave `was`. Did it?

        THIS REPLACES A FIXED `time.sleep(SETTLE)` AFTER EVERY PACKET, and it
        is both faster and safer - the same trade as everywhere else in this
        harness, because the check REPLACES the wait rather than following it.

          * FASTER, because the answer is usually there in a few
            milliseconds. A packet is 3 bytes at 1200 baud - about 25 ms of
            GUEST time - and `mou_isr` publishes `mouse_x` from the interrupt
            gate, so the word moves as soon as the UART delivers. The fixed
            wait was 0.25 HOST seconds, which on this box is ~0.9 guest
            seconds: thirty times what the packet needs. `to` sends two or
            three packets, so that was most of the cost of every click in the
            suite.
          * SAFER, because the budget is the GUEST's clock. The old spelling
            made the whole of `to`'s patience a host quantity - and its own
            docstring says what that is for: a guest busy repainting eats
            packets (the 8250 has no FIFO), and a wait cut short by a busy BOX
            then looks exactly like the kernel refusing to go there. Two
            investigations have been lost to that, one of them concluding a
            drag clamp that does not exist.

        A `False` here is not a verdict - `to`'s loop simply re-reads and
        sends again, which is what it did before.
        """
        c0 = None
        for i in range(4000):
            if self.where()[:2] != was:
                return True
            if i % 4 == 3:                      # a status is a round trip;
                c = self.m.status().get("cycles", 0)     # the position read
                if c0 is None:                  # is the pacing, this is only
                    c0 = c                      # the deadline
                elif (c - c0) / os88marty.GUEST_HZ >= guest:
                    return False
            time.sleep(0.005)
        return False

    def to(self, x, y, tries=None, l=False, retry=True, r=False):
        """Drive to (x, y) and PROVE it, or raise.

        `l` and `r` are the button LEVELS to carry while moving - a Microsoft
        packet states both on every one, so a drag with a button held has to
        keep saying so or the guest sees a release halfway across.
        """
        if tries is None:
            # A packet moves at most STEP per axis, so the budget has to scale
            # with the DISTANCE: a fixed six was enough for a short hop and
            # silently too few to cross the screen, which reported a target as
            # unreachable while walking steadily towards it.
            #
            # AND THE SLACK HAS TO SCALE TOO, which is what a fixed `+ 4` did
            # not. A dropped packet costs one try wherever it happens, so a
            # 900px drag across an extended desktop had nine packets of work
            # and four of margin - and a drag is exactly when drops are likely,
            # because the guest is redrawing an XOR outline between them on a
            # 4.77MHz machine. Doubling the packet count is margin proportional
            # to the exposure; it cannot turn an unreachable target into a
            # reachable one, since the loop still requires exact arrival.
            cx, cy, _ = self.where()
            far = max(abs(x - cx), abs(y - cy))
            tries = ((far + STEP - 1) // STEP) * 2 + 6
        for n in range(tries):
            cx, cy, _ = self.where()
            dx, dy = x - cx, y - cy
            if dx == 0 and dy == 0:
                if self.verbose:
                    print("  at (%d,%d) after %d correction(s)" % (x, y, n))
                return
            # One packet's worth at a time; the read after it is what makes
            # the next one exact rather than hopeful.
            self._pk(max(-STEP, min(STEP, dx)), max(-STEP, min(STEP, dy)),
                     l=l, r=r)
            self._landed((cx, cy))
        cx, cy, _ = self.where()        # ...and CHECK AFTER THE LAST MOVE, or
        if (cx, cy) == (x, y):          # a target needing exactly `tries`
            return                      # packets is reported as unreachable
                                        # while sitting precisely on it

        # A BUSY GUEST LOOKS EXACTLY LIKE A CLAMP, and that is the failure
        # this exists to stop. The 8250 has no FIFO, so a byte arriving before
        # the previous one is read is an overrun and is GONE - and a repaint
        # on a 4.77MHz 8088 is SECONDS, not milliseconds. A window straddling
        # two displays is the worst case in the tree. The whole budget above
        # is ~`tries` x PKT_GUEST of the GUEST's own time, so a guest busy for
        # longer than that eats every packet and the pointer does not move AT
        # ALL, which reads as
        # "the kernel will not let me go there" and has now cost two
        # investigations - one of them concluding a drag clamp that does not
        # exist (docs/plans/completed/DUAL-DISPLAY-VGA.md 8(10)).
        #
        # So exhaustion is not a verdict: wait out the repaint and run the
        # loop again, ONCE. It cannot turn an unreachable target into a
        # reachable one, because arrival is still tested exactly - it can only
        # stop a slow one being called impossible.
        if retry:
            os88marty.guest_sleep(self.m, BUSY)
            self.to(x, y, tries=tries, l=l, retry=False, r=r)
            return
        raise MartyError("could not reach (%d,%d): stuck at (%d,%d) after a "
                         "%.0f GUEST-second wait, so it is not the guest "
                         "being busy and not the box being loaded either. A "
                         "target outside the screen, or off the kernel's "
                         "clamp, cannot be reached." % (x, y, cx, cy, BUSY))

    def click(self, x, y, settle=1.5):
        self.to(x, y)
        if self.where()[2] & 1:         # a button left down by something else
            self._edge(False)           # would make this press no edge at all
        self._edge(True)
        self._edge(False)
        _wait(self.m, settle, "click")

    # --- clicking that PROVES itself ---------------------------------------
    def ticks(self):
        """The guest's own 18.2 Hz tick count (0040:006C)."""
        b = self._raw(BIOS_TICKS, 4)
        return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)

    def _edge(self, down, tries=60, resend=20, btn=1):
        """One button edge, PROVEN: send the packet, then wait until the
        published mouse_btn agrees. A packet clocked into the UART while the
        previous one is still in flight is simply dropped, and the only
        difference a caller can see is that the guest did nothing.

        AND A DROPPED PACKET IS RE-SENT RATHER THAN WAITED FOR, which is what
        the paragraph above asks for and this routine did not do: it sent once
        and then polled 60 times, so a drop could only ever end in the raise
        below. Waiting longer cannot recover a packet the UART never carried.

        Re-sending is safe because a Microsoft packet carries the button's
        LEVEL and not an edge, so a duplicate that arrives after the first one
        was decoded says what the guest already believes and produces no second
        press. That is what keeps this from manufacturing a double-click out of
        a retry - and the loop still requires the guest's own published
        mouse_btn to agree before it returns, so a re-send cannot turn a
        genuinely stuck button into a pass.
        """
        want = btn if down else 0
        for i in range(tries):
            if i % resend == 0:
                self.m.mouse(0, 0, l=down and btn == 1, r=down and btn == 2)
            if (self.where()[2] & btn) == want:
                return
            time.sleep(0.02)
        raise MartyError(
            "the %s %s was never decoded (mouse_btn = %02x). The 1200-baud "
            "UART drops a packet sent while the previous one is in flight."
            % ("right" if btn == 2 else "left",
               "press" if down else "release", self.where()[2]))

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
        _wait(self.m, settle, "dblclick")
        return span

    def menu(self, x0, y0, x1, y1, settle=2.0):
        """Press on the bar, drag to the item, release (SPEC.md 12).

        A menu cannot be opened with a click: menu_track draws the pull-down
        and then polls a level, so a press-and-release in place opens it and
        closes it in the same breath - which is SPEC.md 9.6.1's flashing menu
        seen from the harness side.

        BOTH EDGES ARE PROVEN, for the reason `dblclick`'s are. These used to
        be bare `_pk` packets, and a dropped RELEASE is the worst available
        failure here: the press landed, the pull-down opened, the item
        highlighted, and the command never ran - so a screenshot shows a menu
        that looks exactly like one being used and every later step reads the
        window as unchanged. It cost half a dozen runs of a diagnosis that was
        chasing a kernel bug at the time.
        """
        self._press_drag_release(x0, y0, x1, y1, settle)

    def rmenu(self, x0, y0, x1, y1, settle=2.0, aim=None):
        """The same for the RIGHT button: the context menu (SPEC.md 12.4).

        `fm_rclick` pops the menu under the pointer and `menu_track` then
        polls a LEVEL exactly as the bar's does, so this is `menu` with the
        other button and not a click - a press and release in place would open
        it and close it in one breath.

        `aim(self)` -> (x, y) IS CALLED WITH THE BUTTON DOWN, once the menu is
        on screen, and its answer replaces (x1, y1). A popup is anchored at
        the pointer and then SHIFTED rather than clipped - left off the right
        edge, up off the bottom (menu_popup) - so a caller computing an item's
        y from the press point is right until the press is near an edge, and
        then silently picks a different item. Reading `menu_x1`/`menu_y1` out
        of the guest at this moment is the only spelling that cannot.
        """
        if aim is None:
            self._press_drag_release(x0, y0, x1, y1, settle, btn=2)
            return
        self.to(x0, y0)
        if self.where()[2] & 2:
            self._edge(False, btn=2)
        self._edge(True, btn=2)
        x1, y1 = aim(self)
        self.to(x1, y1, r=True)
        self._edge(False, btn=2)
        _wait(self.m, settle, "drag/menu")

    def drag(self, x0, y0, x1, y1, settle=1.5):
        self._press_drag_release(x0, y0, x1, y1, settle)

    def _press_drag_release(self, x0, y0, x1, y1, settle, btn=1):
        self.to(x0, y0)
        if self.where()[2] & btn:
            self._edge(False, btn=btn)
        self._edge(True, btn=btn)
        self.to(x1, y1, l=btn == 1, r=btn == 2)
        self._edge(False, btn=btn)
        _wait(self.m, settle, "drag/menu")


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
