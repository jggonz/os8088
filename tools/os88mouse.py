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

  * `click` ends in a settle (up to 1.5 s), so two of them are a second and a half
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

# --- GUEST PACING ------------------------------------------------------------
#
# `click(settle=1.5)` waits 1.5 idle-box seconds' worth of the GUEST's time
# for it to act on the press: os88marty.GUEST_PACE guest seconds a second.
# It was 1.5 HOST seconds, and how much guest work that bought was a property
# of the box - measured over 118 waits in each arm, the median host cost was
# 2.2s in both and the GUEST cost 7.3s against 5.9s, up to -37% per script.
# The row did not get slower, it got LESS THOROUGH, and failed further on
# looking like the thing under test. OS88_GUEST_PACE=0 puts the host sleeps
# back for an A/B.
#
# THAT IS AN EXPLICIT `settle=`. A verb called WITHOUT one waits for the UI to
# finish with the gesture instead (`_ui_wait`), capped at the same pause - so
# the fixed figure is now a ceiling rather than a price.
GUEST_PACE = os88marty.GUEST_PACE


def _wait(m, secs, why="click", dflt=None):
    """Spend `secs` of idle-box time, as GUEST time (os88marty.pace).

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
    auto = secs is None                 # the caller took the verb's default
    if auto:
        secs = dflt if dflt is not None else 1.5
    log = bool(os88marty.WAITLOG) and m is not None
    c0 = None
    if log:
        try:
            c0 = int(m.status().get("cycles", 0))
        except Exception:
            c0 = None
    t0 = time.time()
    if auto:
        _ui_wait(m, secs, why)
    else:
        os88marty.pace(m, secs)
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


def _ui_wait(m, dflt, why):
    """A verb's DEFAULT settle: until the UI has finished with the gesture
    (os88marty.ui_done), and never longer than the `dflt` pause it replaces.

    Every verb here ended in a fixed pause - 1.5 idle-box seconds after a
    click, 2.0 after a double-click or a menu, which at GUEST_PACE is 6.75
    and 9 guest seconds - whether the handler took a millisecond or a folder
    load. Most of any row that clicks its way through the UI was the machine
    sitting idle in those. It now ends when ui_task has gone back to sleep
    with nothing queued, nothing locked and the drive still, and the old
    pause is the CAP: a machine whose worker keeps the lock busy (a game)
    gets exactly what it got before. A caller that passes a number keeps the
    fixed pause it asked for.

    A guest that is not executing ends it at once, as `pace` does - the
    caller armed a breakpoint or paused the machine, and nothing more will
    happen until it acts. A kernel whose map cannot be read here (a DOS box,
    a private build this process cannot resolve) takes the old pause, and so
    does every call under OS88_SETTLE_UI=0, the A/B for both halves."""
    if os88marty.GUEST_PACE <= 0 or m is None or not os88marty.SETTLE_UI:
        os88marty.pace(m, dflt)
        return
    cap = dflt * os88marty.GUEST_PACE
    try:
        os88marty.ui_idle(m)
    except MartyError:
        raise
    except Exception:
        os88marty.pace(m, dflt)
        return
    try:
        os88marty.ui_done(m, "the %s to be handled" % why, cap=cap)
    except MartyError:
        st = m.status()
        if st.get("state") != "running" and not getattr(m, "_pumping", 0):
            return
        raise


# A GUEST THAT IS NOT EXECUTING, named here rather than polled out.
#
# Every wait in this file is a poll on something the GUEST publishes - the
# pointer in `_landed`, the button level in `_edge` - so a guest that has
# stopped can never satisfy one. Without this the two of them fail in the two
# worst possible ways: `_landed`'s deadline is in guest CYCLES and a stopped
# machine spends none, so it burns its whole 4000-round loop (~20 host
# seconds) per packet and `to` then re-sends and does it again; and `_edge`
# ends in a raise BLAMING THE 1200-BAUD UART for dropping a packet the UART
# was never asked to carry. An armed breakpoint is what puts a machine in that
# state, and MEASURED on this container that took 332.1 seconds to say so -
# from `guest_sleep`'s stall arm inside `to`'s retry, which is the only thing
# in the path that was watching the clock at all. It is 2.2s now.
#
# So the clock is watched the way os88marty's `_Progress` watches it - the
# same GUEST_STALL, and the same reasoning that a two-second answer naming the
# machine beats a sixty-second one blaming the condition.
#
# UNDER A `bp_trace` BLOCK THE ALLOWANCE IS WIDER AND STILL FINITE. A pumped
# stop is meant to end within a poll interval, so the clock keeps moving
# between hits and this never fires; a clock frozen for four times the stall
# under a pump means the pump is not resuming, which is worth saying in those
# words rather than sitting out the caller's deadline.
def _alive(m, seen, why):
    """Raise if the guest's clock has stopped. `seen` is the caller's [c, t]."""
    c = int(m.status().get("cycles", 0))
    now = time.time()
    if not seen or c != seen[0]:
        seen[:] = [c, now]
        return
    pumped = bool(getattr(m, "_pumping", 0))
    if now - seen[1] <= os88marty.GUEST_STALL * (4 if pumped else 1):
        return
    st = {}
    try:
        st = m.status()
    except Exception:
        pass
    raise MartyError(
        "the GUEST CLOCK HAS NOT MOVED for %.1fs while waiting for %s - it is "
        "%r at %04X:%04X. %s"
        % (now - seen[1], why, st.get("state", "?"), st.get("cs", 0),
           st.get("ip", 0),
           "A bp_trace block is open and its pump is not resuming the guest - "
           "the pump thread has died, or a breakpoint is being re-entered "
           "before any cycle passes."
           if pumped else
           "A stopped machine cannot move the pointer or decode a button, so "
           "this is the machine and not the mouse. A breakpoint armed outside "
           "a bp_trace block is the usual cause: arm it inside one, or clear "
           "it before driving the UI."))


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
        self._last_edge = None          # guest tick of the last button edge,
                                        # for _sep's double-click separation

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
        os88marty.pace(self.m, GAP)     # a packet in flight drops the next

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
        c0, seen = None, []
        for i in range(4000):
            if self.where()[:2] != was:
                return True
            if i % 4 == 3:                      # a status is a round trip;
                _alive(self.m, seen, "the pointer to move")  # the position
                c = self.m.status().get("cycles", 0)         # read is the
                if c0 is None:                  # pacing, this is only the
                    c0 = c                      # deadline
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

    def click(self, x, y, settle=None):
        self.to(x, y)
        if self.where()[2] & 1:         # a button left down by something else
            self._edge(False)           # would make this press no edge at all
        self._sep()                     # ...and not the second half of a
        self._edge(True)                # double-click either (see _sep)
        self._edge(False)
        _wait(self.m, settle, "click", 1.5)

    # --- clicking that PROVES itself ---------------------------------------
    def ticks(self):
        """The guest's own 18.2 Hz tick count (0040:006C)."""
        b = self._raw(BIOS_TICKS, 4)
        return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)

    def _sep(self):
        """Separate this press from the LAST one on the GUEST's clock.

        **A GAP MEASURED IN HOST WORK IS WRONG AT SOME GUEST SPEED**, and that
        is the whole reason this exists rather than a sleep at a call site.
        Two of this module's verbs in a row put two button edges a fixed
        amount of HOST work apart - a `settle`, a screen read, a couple of
        round trips - and the guest sees that interval in GUEST time, which
        shrinks as the box gets busier and grows as it gets quieter. The
        kernel's double-click detectors measure the same interval in TICKS
        (`DBL_TICKS`, shared by ui_tdbl, DESK_DBLT, FM_DBLCLK and FD_DBLCLK),
        so on a loaded box two ordinary clicks slide inside the window and
        become a DOUBLE-CLICK: a title bar zooms instead of dragging
        (SPEC.md 11.95), a file opens instead of being selected.

        Nothing about that looks like what it is. The verb that follows acts
        on the wrong window or never runs, and the row reports the feature.
        It failed dockpos once in a four-lane soak and lzcomp three times in
        four with four copies at once, and both passed alone - which is how
        it kept being written off as contention.

        So the separation is taken on 0040:006C, the guest's own tick, and is
        the same amount of the MACHINE's time whatever the host is doing.
        `dblclick` is the one verb that WANTS to be inside the window and
        skips it between its own two presses - never before the first.
        """
        if self._last_edge is None:
            return
        while self.ticks() - self._last_edge <= DBL_TICKS:
            time.sleep(0.01)

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
        seen = []
        for i in range(tries):
            if i % resend == 0:
                self.m.mouse(0, 0, l=down and btn == 1, r=down and btn == 2)
            if (self.where()[2] & btn) == want:
                self._last_edge = self.ticks()   # ...for _sep above
                return
            # ...and the raise below blames the UART, which is the wrong
            # answer for a guest that is not executing at all.
            if i % 4 == 3:
                _alive(self.m, seen, "the button edge to be decoded")
            time.sleep(0.02)
        raise MartyError(
            "the %s %s was never decoded (mouse_btn = %02x). The 1200-baud "
            "UART drops a packet sent while the previous one is in flight."
            % ("right" if btn == 2 else "left",
               "press" if down else "release", self.where()[2]))

    # A DOUBLE-CLICK'S TWO PRESSES ARE SPACED IN GUEST CYCLES, NOT HOST ROUND
    # TRIPS. `_edge` proves an edge by polling the guest while it runs freely,
    # so the gap between the first press and the second was however much
    # guest time elapsed while the host sent a packet, polled, slept and sent
    # again - a quantity of the BOX. On a loaded box that crossed the kernel's
    # 9-tick window and the guest saw two first clicks; the Weave family grew
    # a navigation retry for it and documented it as the thing that flakes.
    # Stepped instead: the guest is paused, each packet is sent and the
    # machine advanced DBL_STEP cycles at a time until the published button
    # level agrees, so the span is the UART's and the ISR's and nothing else -
    # a packet is 3 bytes at 1200 baud, 25 ms, and the whole gesture is about
    # one tick of the window's nine whatever the host is doing.
    DBL_STEP = 20000                # cycles an advance: ~4 ms of a 4.77 MHz 8088
    DBL_RESEND = 12                 # ...and a re-send every 240k (~50 ms), two
                                    # packet times, so a re-send never lands on
                                    # a packet still in flight (see `_edge`)
    DBL_TRIES = 240                 # ~1 guest second an edge before it is lost

    def _gedge(self, down, btn=1):
        """`_edge`, with the guest PAUSED and advanced by cycles between polls.

        The same proof - the published mouse_btn agrees - and the same
        idempotent re-send, but no guest time passes that the loop did not
        hand out itself.
        """
        want = btn if down else 0
        for i in range(self.DBL_TRIES):
            if i % self.DBL_RESEND == 0:
                self.m.mouse(0, 0, l=down and btn == 1, r=down and btn == 2)
            st = self.m.advance(cycles=self.DBL_STEP)
            if st.get("state") == "breakpoint":
                raise MartyError(
                    "a breakpoint stopped the guest inside a double-click at "
                    "%04X:%04X - arm it inside a bp_trace block, whose pump "
                    "services it, or after the gesture"
                    % (st.get("cs", 0), st.get("ip", 0)))
            if (self.where()[2] & btn) == want:
                self._last_edge = self.ticks()
                return
        raise MartyError(
            "the %s %s was never decoded across %d guest cycles (mouse_btn = "
            "%02x) with the packet re-sent every %d - the guest is running, "
            "so this is the serial path and not the host"
            % ("right" if btn == 2 else "left", "press" if down else "release",
               self.DBL_TRIES * self.DBL_STEP, self.where()[2],
               self.DBL_RESEND * self.DBL_STEP))

    def dblclick(self, x, y, settle=None):
        """Two presses inside the kernel's own double-click window.

        NOT two `click`s: that spelling is a second and a half apart and reads
        as two first clicks. See the module docstring, and DBL_STEP above for
        why the presses are stepped in guest cycles.
        """
        self.to(x, y)
        if self.where()[2] & 1:         # a button left down by something else
            self._edge(False)           # would make the first press no edge
        self._sep()                     # ...the FIRST press is separated from
                                        # whatever came before; the second is
                                        # deliberately not (see _sep)
        if getattr(self.m, "_pumping", 0):
            # A bp_trace pump resumes breakpoint stops from its own thread,
            # and a paused guest would take its stops away from it. Inside a
            # pump the edges run free, as they always did.
            self._edge(True)
            t1 = self.ticks()
            self._edge(False)
            self._edge(True)
            t2 = self.ticks()
            self._edge(False)
        else:
            was = self.m.status().get("state")
            self.m.pause()
            try:
                self._gedge(True)
                t1 = self.ticks()
                self._gedge(False)
                self._gedge(True)
                t2 = self.ticks()
                self._gedge(False)
            finally:
                if was == "running":
                    self.m.go()
        span = (t2 - t1) & 0xFFFFFFFF
        if span >= DBL_TICKS:
            raise MartyError(
                "the two presses were %d ticks apart and the window is %d: "
                "the guest saw two FIRST clicks, not a double-click. Something "
                "between them was slow - a mount or a package load holding the "
                "mouse ISR off for half a second." % (span, DBL_TICKS))
        if self.verbose:
            print("  double-click at (%d,%d): %d tick(s) apart" % (x, y, span))
        _wait(self.m, settle, "dblclick", 2.0)
        return span

    def menu(self, x0, y0, x1, y1, settle=None):
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

    def rmenu(self, x0, y0, x1, y1, settle=None, aim=None):
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
        _wait(self.m, settle, "drag/menu", 2.0)

    def drag(self, x0, y0, x1, y1, settle=None):
        self._press_drag_release(x0, y0, x1, y1, settle, dflt=1.5)

    def _press_drag_release(self, x0, y0, x1, y1, settle, btn=1, dflt=2.0):
        self.to(x0, y0)
        if self.where()[2] & btn:
            self._edge(False, btn=btn)
        self._sep()                     # a drag OPENS with a press, and two
        self._edge(True, btn=btn)       # of them in a row are a double-click
        self.to(x1, y1, l=btn == 1, r=btn == 2)
        self._edge(False, btn=btn)
        _wait(self.m, settle, "drag/menu", dflt)


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
