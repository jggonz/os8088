#!/usr/bin/env python3
"""Scrolling a note whose height is still being counted, and the scroll bar
neither the blit nor the repaint behind it may touch (SPEC.md 27.7.6.1,
27.7.2, 27.7.2.1).

    make && python3 tests/npscroll.py

THREE FIELD REPORTS, ONE ROW, and they share a machine because they share a
gesture: open a long document and click the scroll bar before the background
count (SPEC.md 27.7.3) has finished.

  1. *"any attempt to scroll freezes Note Pad until the walk completes."*
     Every arm of np_sbclick is RELATIVE and the test at `.set` was UNSIGNED,
     so the up arrow at [np_top] = 0 asked for row -4 and was read as row
     65,532 - past any counted extent there will ever be. Measured before the
     fix on this exact machine: **1,053 ms of frozen 8088, [np_hdirty] 1 -> 0,
     [np_drows] 551 -> 718, and [np_top] 0 -> 0.** It scrolled nothing and
     bought the whole note. (That 1,053 ms was the count RESUMING where the
     worker had got to; from the top, which is what leg A arranges, the same
     click is 4.6 seconds.)

  2. *"scrolling erases half the scroll bar."* np_vshift cut its blit from
     [np_rgt], the last TEXT column, rounded up to a byte column - which
     reached into the bar itself (six of its fourteen columns on this
     machine). The strip was then blanked and the whole bar drawn again, so
     the bar's left half was WHITE for the band's entire relettering:
     PERFORMANCE.md Part 1's double-draw flash.

  3. *"clicking below the thumb, but not on the arrow, is blanking the entire
     inner window - causing the scrollbar to completely disappear."* A track
     click moves by [np_vrows] on the nose, which retains nothing, so the blit
     is refused and np_redraw repaints - and .fullpaint's white fill covered
     the WHOLE content, bar and grow box included, with np_paint drawing the
     bar back only after every visible row had been lettered.

WHAT MAKES EACH LEG GO RED, which is the only question that decides whether a
row is worth having (docs/WRITING-TESTS.md 1):

  A  the GFX LOCK must be free while an up-arrow click at the top is being
     served. ui_task holds it around the whole event handler
     (docs/plans/completed/UI-FREEZE-PLAN.md 1), so it IS the freeze rather than a proxy for
     one. Against the build before the fix: **held at 30 of 30 samples across
     629 ms**, and `wait_idle` then measured another 19 M cycles before the
     hold ended - a 4.6-second stop. After: 0 of 30.
  B  a request PAST the counted extent must carry the count, not finish it.
     Before, np_height walked to row 0x7FFF whoever asked: [np_drows] came
     back 718 of a 718-row note with [np_hdirty] cleared. After: 543, still
     owed.
  C  the bar's UP-ARROW CELL must be untouched at every instant of a scroll,
     and it is watched across TWO clicks because they are two different paths.
     Nothing in a scroll draws there - the thumb is clamped between the
     arrow-cell rules and the pointer is parked on whatever is being clicked -
     so the reference is the cell as it stood before the press and the number
     owed is zero, at every sample.
       C1, the DOWN ARROW, blits: the strip fill spanned [np_ty]..[np_bot] and
       whitened that cell's left columns. Before: **21 of 64 samples altered,
       11 bytes at worst**. After: 0 of 64.
       C2, the TRACK below the thumb, cannot blit and repaints: the whole
       content went white and the bar with it. Before: **60 of 400 samples,
       22 bytes - the whole cell - so the bar was off the screen for ~315 ms**.
       After: 0 of 400. `down=True` refuses a click that paged UP, so "below
       the thumb" is checked rather than assumed.

Every one of those "before" numbers was taken by building this tree with the
fix backed out and running this file against it, which is the only thing that
says a green row means anything.

THE SAMPLING IN LEG C IS STEPPED, not timed. The press is injected on a
PAUSED machine and the guest is then advanced in fixed cycle steps with the
framebuffer read at each one, so what it sees does not depend on the host's
speed. The gash lasted ~130 ms of guest time (a band of glyphs) against the
25,000-cycle step, which is ~5 ms - so a build that has it cannot slip
between two samples.

CGA by name: leg C reads the framebuffer out of guest memory at 0xB8000, and
mode 12h is four planes behind Read Map Select rather than flat memory
(os88marty.vram says so). Nothing here is adapter-specific except that read.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tools", "notepad"))
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
from state import offsets                                   # noqa: E402

IMG = os.path.join(ROOT, "build", "os8088-360.img")
NPBIN = os.path.join(ROOT, "build", "notepad.bin")
RAWDOC = os.path.join(ROOT, "build", "readme-plain.txt")

NP_SB_ARR = 11                  # apps/notepad/notepad.asm: the arrow cells
NP_SB_W = 14                    # ...and the bar's width
NP_SB_STEP = 4                  # ...and what one arrow click travels

STEP_CYCLES = 25_000            # leg C's sampling step, ~5 ms of guest
STEP_COUNT = 64                 # ...over ~335 ms, which contains the scroll
PAGE_STEPS = 400                # ...and a PAGE is a full repaint: ~19 rows of
                                # glyphs at ~71 ms apiece (PERFORMANCE.md Part
                                # 2), so the window that has to be watched is
                                # seconds rather than a third of one

# What a bar click is allowed to cost, in guest cycles at 4.77 MHz. 8,000,000
# is 1.68 s - eight times the ~200 ms a scroll actually takes here, and well
# past the 1,053 ms unbounded walk the legs below forbid, so a build that
# takes it FINISHES it inside the budget and is caught by the state it leaves
# rather than by a timeout. The worker gets ~10 of its NP_WTICKS passes in
# that window, which is ~50 rows: nowhere near the ~700 that finishing the
# count would take, so the two cannot be confused.
CLICK_CYCLES = 8_000_000

# Leg A watches the gfx lock rather than a budget: 30 samples 100,000 cycles
# apart is ~630 ms of guest time, which spans three of the count worker's
# NP_WTICKS passes - so a healthy machine can show the lock held a few times
# and still be nowhere near half.
LOCK_STEP = 100_000
LOCK_SAMPLES = 30

# ...and what a click that legitimately WALKS is allowed, which is a different
# number: leg B arranges a count owed from the top and then asks about a row
# ~536 down, so the bounded walk it is testing for is seconds of honest work.
# 60,000,000 is 12.6 s - past the ~4.3 s an UNBOUNDED walk of this note takes,
# so a build that has one finishes it and is caught by the state it leaves.
WALK_CYCLES = 60_000_000


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


class Note(object):
    """Note Pad's own bss, read out of the running guest.

    The offsets hang off the IMAGE SIZE (docs/plans/completed/NOTEPAD-NOTES.md 6.3), so they
    describe exactly one binary - and `check` refuses rather than warning,
    because a wrong base reads as plausible numbers rather than as an error.
    """

    def __init__(self, m):
        self.m = m
        self.off = offsets(NPBIN)
        self.seg = self._find()

    def _find(self, name=b"NOTEPAD", lo=0x1000, hi=0xA000):
        # A package sits on a paragraph boundary off the top of the heap
        # (SPEC.md 20.1/50.3), so the scan is over paragraphs and the match is
        # 'O8' + version 3 + the header's own name field.
        hits = []
        for base in range(lo, hi, 0x400):
            n = min(0x400, hi - base) * 16
            try:
                data = self.m.read(base * 16, n)
            except Exception:
                continue
            for i in range(0, len(data) - 32, 16):
                if (data[i] == 0x4F and data[i + 1] == 0x38 and data[i + 2] == 3
                        and bytes(data[i + 16:i + 32]).split(b"\0")[0] == name):
                    hits.append(base + i // 16)
        if not hits:
            raise SystemExit("npscroll: no NOTEPAD package header in RAM")
        return hits[-1]

    def w(self, name):
        return u16(self.m.readseg(self.seg, self.off[name], 2))

    def b(self, name):
        return self.m.readseg(self.seg, self.off[name], 1)[0]

    def poke(self, name, val, size=2):
        addr = (self.seg << 4) + self.off[name]
        self.m.write(addr, bytes((val & 0xFF, val >> 8)) if size == 2
                     else bytes((val & 0xFF,)))

    def owe(self, from_top=True):
        """Put the height count back where a freshly-opened note has it -
        np_hmark's own three stores (SPEC.md 27.7.3), from the host.

        The alternative is to race the worker: the count is ~24 guest seconds
        and how much of it a row has spent by the time a leg starts depends on
        how fast the HOST got there, which is the one thing a measurement must
        never depend on. tests/fishedge.py pokes [sv_hlim] for the same
        reason - the state under test is arranged, not waited for.

        **THE GUEST IS STOPPED FIRST, and that is np_hmark's own rule applied
        to the host.** §27.7.3 says raising the flag and forgetting the resume
        pair are ONE event because the pair describes the note it was taken
        from - and three `write`s to a RUNNING guest are not one event. The
        worker's own `.more` store landed between two of them here, leaving
        [np_hrow] at row 545 with [np_hi] at index 0: the next chunk then laid
        the note out from the top while believing it was half way down, and
        the count came back 1,263 rows for a 718-row note. It read exactly
        like a defect in the code under test.
        """
        self.m.pause()
        if from_top:
            self.poke("np_hrow", 0)
            self.poke("np_hi", 0)
        self.poke("np_hdirty", 1, size=1)

    def check(self):
        """np_len must be the document that was opened, or nothing else here
        means anything. Note Pad folds CRLF to LF on load (SPEC.md 27.1), so
        the number is the raw file minus its carriage returns - and the raw
        file is build/readme-plain.txt, because build/readme.txt is the
        COMPRESSED artefact (SPEC.md 20.13.5)."""
        raw = open(RAWDOC, "rb").read()
        want = len(raw) - raw.count(b"\r")
        got = self.w("np_len")
        if got != want:
            raise SystemExit(
                "npscroll: REFUSING - np_len reads %d, README.TXT is %d "
                "characters after the CRLF fold. The offsets come from %s; if "
                "that is not the binary the guest is running, every field here "
                "is garbage (docs/plans/completed/NOTEPAD-NOTES.md 6.3)." % (got, want, NPBIN))
        return got


def cga_cell(m, x1, x2, y1, y2):
    """The framebuffer bytes covering columns x1..x2 of rows y1..y2.

    SPEC.md 39.3's two-bank CGA layout, the arithmetic os88marty.vram uses -
    rounded out to whole bytes, because the columns either side of the bar are
    the window's own border and do not move during a scroll.
    """
    fb = m.read(0xB8000, 0x4000)
    b1, b2 = x1 >> 3, x2 >> 3
    out = bytearray()
    for y in range(y1, y2 + 1):
        off = (y % 2) * 0x2000 + (y // 2) * 80
        out += fb[off + b1:off + b2 + 1]
    return bytes(out)


def press(ui, np_, x, y, budget, until=None, step=1_000_000):
    """One click at (x, y), with the GUEST TIME IT COSTS under our control.

    The pointer is placed with the machine running - os88mouse reads the
    cursor back, so the position is confirmed rather than dead-reckoned - and
    the button edge is then injected on a PAUSED machine and paid for in
    cycles. That is the whole of what makes these legs measurements:
    os88mouse's own `click` settles for a WALL interval, and a host that runs
    the guest at four times real time hands it seconds of background count
    that no assertion here can then tell from the freeze under test.

    `until` is a predicate on guest state, and it is what keeps the WORKER out
    of the answer: with it the guest is advanced only until the click has
    visibly landed, so the count's own background passes (SPEC.md 27.7.3) have
    not had time to move the numbers being read back. Without it the whole
    budget is spent, which is what leg A wants - there the click changes
    nothing at all when it is right.

    Answers the cycles spent, so a leg can say what the click cost.
    """
    m = ui.m
    m.run()                         # os88mouse confirms against the guest's
    ui.mo.to(x, y)                  # own cursor, so it needs one that is
    m.pause()                       # RUNNING - and `advance` leaves it paused
    m.mouse(0, 0, l=True)
    m.step(1)                       # deliver it: a packet queued on a paused
    spent = 0                       # machine arrives during a LATER advance
    while spent < budget:
        n = min(step, budget - spent) if until else budget
        m.advance(cycles=n)
        spent += n
        if until and until():
            break
    m.mouse(0, 0)
    m.step(1)
    m.advance(cycles=400_000)
    m.run()
    return spent


def leg_a(ui, np_, say, total):
    """The up arrow at the top of the note must not FREEZE the machine.

    THE ASSERTION IS THE GFX LOCK, which is the freeze itself and not a proxy
    for it: ui_task takes it around the whole event handler
    (docs/plans/completed/UI-FREEZE-PLAN.md 1), so while a click is inside an unbounded walk
    the flag is held and nothing else on the machine draws or moves. Sampled
    across ~630 ms of guest time, an up arrow that returns leaves it free
    almost throughout - the only other taker is the count's own worker, ~25 ms
    of every 190 (SPEC.md 27.7.3) - and one that walks 718 rows holds it for
    every sample of every second of the ~4.8 s it takes.

    State cannot do this job here, and it is worth saying why rather than
    leaving the next reader to rediscover it: `[np_hdirty]` is cleared at the
    END of the walk, so within any budget short enough to keep the WORKER from
    finishing the count in the background it reads 1 on both builds. The one
    that reads 1 because there is nothing to do and the one that reads 1
    because it is 2% of the way through a four-second hold are the same byte.
    """
    m = ui.m
    m.pause()                                   # one event, not four
    np_.poke("np_drows", np_.w("np_len") // np_.w("np_rcols"))
    np_.owe()
    top, drows = np_.w("np_top"), np_.w("np_drows")
    say("A: before  top=%d drows=%d total=%d" % (top, drows, total))
    if top != 0:
        raise SystemExit("npscroll: leg A cannot arrange its case - it needs "
                         "the view at row 0 and this one is at %d" % top)
    if drows - np_.w("np_vrows") <= 0:
        raise SystemExit("npscroll: leg A cannot arrange its case - nothing is "
                         "counted, so even a positive request would want the "
                         "count and the sign is not what is being tested")
    lock = m.sym("gfx_lock_flag")
    x = np_.w("np_sbr") - NP_SB_W // 2
    m.run()
    ui.mo.to(x, np_.w("np_ty") + NP_SB_ARR // 2)
    m.pause()
    m.mouse(0, 0, l=True)
    m.step(1)
    held = 0
    for _ in range(LOCK_SAMPLES):
        m.advance(cycles=LOCK_STEP)
        held += 1 if m.read(lock, 1)[0] else 0
    m.mouse(0, 0)
    m.step(1)
    m.advance(cycles=400_000)
    m.run()
    say("A: after   top=%d drows=%d hdirty=%d; the gfx lock was HELD at %d of "
        "%d samples over %.0f ms"
        % (np_.w("np_top"), np_.w("np_drows"), np_.b("np_hdirty"), held,
           LOCK_SAMPLES, LOCK_SAMPLES * LOCK_STEP / 4772.0))
    if held * 2 > LOCK_SAMPLES:
        return ("the up arrow at the top of the note froze the machine: the "
                "gfx lock was held at %d of %d samples across %.0f ms, which "
                "is a walk and not a click (SPEC.md 27.7.6.1)"
                % (held, LOCK_SAMPLES, LOCK_SAMPLES * LOCK_STEP / 4772.0))
    return None


def leg_b(ui, np_, say, total):
    """A request past the counted extent CARRIES the count; it does not finish
    it (SPEC.md 27.7.6.1).

    The case has to be arranged, and the arrangement is the freshly-opened
    state exactly: `[np_drows]` at §27.7.4's estimate - the characters over the
    cells a row holds, which is what np_bounds publishes on every pass anyway -
    and the count owed from the top. Then the thumb is dragged to the bottom of
    the track, which goes through np_scrollto alone (SPEC.md 13.10.5) and so
    asks the count for nothing, and one DOWN arrow then reaches four rows past
    everything that has been counted.

    THE ASSERTION IS [np_drows], not the clock. A bounded walk stops just past
    the row it was asked about; an unbounded one ends at the note's last
    character and clears [np_hdirty] on the way. Those are ~536 rows and ~718
    rows of walking - 34% apart, which no timeout could separate - and two
    completely different numbers to read back.
    """
    ty, sbb, sbr = np_.w("np_ty"), np_.w("np_sbb"), np_.w("np_sbr")
    x = sbr - NP_SB_W // 2
    vrows, rcols = np_.w("np_vrows"), np_.w("np_rcols")
    guess = np_.w("np_len") // rcols            # SPEC.md 27.7.4's estimate
    if guess + vrows >= total:
        raise SystemExit(
            "npscroll: leg B cannot arrange its case - §27.7.4's estimate (%d "
            "rows) already reaches this note's true height (%d), so no scroll "
            "can ask about a row the count has not seen" % (guess, total))
    wait_idle(ui)
    ui.m.pause()                                # one event, not three: see owe()
    np_.poke("np_drows", guess)
    np_.owe()
    ui.m.run()
    ui.mo.drag(x, ty + NP_SB_ARR + 2, x, sbb - NP_SB_ARR - 2)
    ui.m.advance(cycles=4_000_000)
    top, drows = np_.w("np_top"), np_.w("np_drows")
    say("B: at the extent  top=%d drows=%d vrows=%d total=%d"
        % (top, drows, vrows, total))
    if top + NP_SB_STEP <= drows - vrows:
        raise SystemExit(
            "npscroll: leg B cannot arrange its case - the drag left the view "
            "at %d, and %d rows are counted, so the next arrow click (row %d) "
            "is still inside the counted extent"
            % (top, drows, top + NP_SB_STEP))
    np_.owe()                                   # the drag's own walks may have
                                                # moved the resume pair
    # STOP AS SOON AS THE VIEW MOVES, and not on a budget: [np_top] changes in
    # np_scrollto, which is the instruction AFTER the count returns, so both a
    # bounded walk and an unbounded one are read at the same point in the same
    # handler - and the worker has had no chance to carry the count itself.
    spent = press(ui, np_, x, sbb - NP_SB_ARR // 2, WALK_CYCLES,
                  until=lambda: np_.w("np_top") != top)         # the DOWN arrow
    ntop, ndrows, now = np_.w("np_top"), np_.w("np_drows"), np_.b("np_hdirty")
    say("B: after         top=%d drows=%d hdirty=%d (%.2f M cycles)"
        % (ntop, ndrows, now, spent / 1e6))
    if now != 1 or ndrows >= total:
        return ("a scroll past the counted extent ran the count to the END "
                "(hdirty -> %d, drows %d -> %d of a %d-row note): np_height "
                "is unbounded again (SPEC.md 27.7.6.1)"
                % (now, drows, ndrows, total))
    if ntop != top + NP_SB_STEP:
        return ("the bounded count did not go far enough for the clamp: top "
                "%d -> %d, wanted %d" % (top, ntop, top + NP_SB_STEP))
    if ndrows < ntop + vrows:
        return ("drows %d is short of the view the clamp just allowed (%d)"
                % (ndrows, ntop + vrows))
    return None


def wait_idle(ui, cap=80_000_000):
    """Advance until nothing is holding the gfx lock, and answer the cycles.

    A leg that leaves the machine INSIDE a hold - which is what leg A does on
    a build that has the defect - hands the next leg a UI task that cannot
    take its click, and the next leg then reports "the arrow did not scroll"
    about a perfectly good arrow. So each leg starts from a machine that is
    demonstrably free rather than from one that is probably free.
    """
    m = ui.m
    lock = m.sym("gfx_lock_flag")
    spent = 0
    while spent < cap:
        if not m.read(lock, 1)[0]:
            return spent
        m.advance(cycles=1_000_000)
        spent += 1_000_000
    raise SystemExit("npscroll: the gfx lock was still held after %.1f guest "
                     "seconds - the machine is not coming back" % (cap / 4.772e6))


def settle_count(ui, np_):
    """Run the background count out, and answer the note's TRUE height.

    Leg C needs it because the worker draws the bar while the count moves
    (SPEC.md 27.7.3) and leg C samples the bar; leg B needs the number itself,
    as the thing an unbounded walk would reach.
    """
    for _ in range(400):
        if np_.b("np_hdirty") == 0:
            break
        ui.m.advance(frames=60)
    if np_.b("np_hdirty"):
        raise SystemExit("npscroll: the background height count never settled")
    return np_.w("np_drows")


def watch_bar(ui, np_, say, tag, y, steps, down=False):
    """Click the bar at y and answer how far its UP-ARROW CELL ever moved.

    The reference is that cell as it stood before the press, and the number
    owed is zero at every sample: the arrow cells are outside the thumb's
    travel (os88ui clamps it between the two rules), the pointer is parked on
    whatever is being clicked - which is never the up arrow - and no scroll
    draws there at all. So anything at all is furniture being erased.
    """
    m = ui.m
    rgt, sbr = np_.w("np_rgt"), np_.w("np_sbr")
    ty = np_.w("np_ty")
    box = (rgt + 1, sbr, ty, ty + NP_SB_ARR - 1)
    x = sbr - NP_SB_W // 2
    m.run()
    ui.mo.to(x, y)
    m.advance(frames=90)
    ref = cga_cell(m, *box)
    if cga_cell(m, *box) != ref:
        raise SystemExit("npscroll: %s is not settled - the bar's arrow cell "
                         "differs between two idle reads" % tag)
    before = np_.w("np_top")
    m.pause()
    m.mouse(0, 0, l=True)                       # the press, on a paused
    m.step(1)                                   # machine: see drive.tap
    worst, seen = 0, 0
    for _ in range(steps):
        m.advance(cycles=STEP_CYCLES)
        d = sum(1 for a, b in zip(ref, cga_cell(m, *box)) if a != b)
        worst = max(worst, d)
        seen += 1 if d else 0
    m.mouse(0, 0)
    m.step(1)
    m.run()
    after = np_.w("np_top")
    say("%s: %d samples, %d of them differ, worst %d byte(s); top %d -> %d"
        % (tag, steps, seen, worst, before, after))
    if after == before:
        raise SystemExit("npscroll: %s cannot arrange its case - the click at "
                         "y=%d did not scroll, so nothing repainted" % (tag, y))
    if down and after < before:
        raise SystemExit("npscroll: %s cannot arrange its case - the click at "
                         "y=%d paged UP, so it landed ABOVE the thumb and not "
                         "below it (top %d -> %d)" % (tag, y, before, after))
    return seen, worst


def leg_c(ui, np_, say, total):
    """Neither a scroll NOR the repaint behind one may take the bar off the
    screen (SPEC.md 27.7.2, 27.7.2.1).

    Two clicks, because they are two different paths and each had its own
    defect. An ARROW is four rows, so np_scrollpaint blits and the bar was
    caught by the blit's own x span. A TRACK click is `[np_vrows]` on the nose,
    which retains nothing - so the blit is refused and np_redraw repaints, and
    the bar was caught by the full-content white fill in front of that.
    """
    m = ui.m
    spent = wait_idle(ui)
    if spent:
        say("C: waited %.2f M cycles for the previous leg's hold to end"
            % (spent / 1e6))
    m.pause()
    np_.poke("np_hdirty", 0, size=1)            # the worker draws the bar while
    np_.poke("np_drows", total)                 # the count moves (SPEC.md
                                                # 27.7.3) and this leg reads the
                                                # bar, so the debt the legs
                                                # above put back is cancelled
                                                # rather than walked out again -
                                                # 27 guest seconds for a picture
                                                # that does not depend on it.
                                                # BOTH stores are the truth and
                                                # not a fixture: settle_count
                                                # ran this note's count to the
                                                # end and `total` is what it
                                                # answered. It also leaves the
                                                # view room below it for a whole
                                                # page, which is what C2 needs
    sbb = np_.w("np_sbb")
    bad = []
    seen, worst = watch_bar(ui, np_, say, "C1 down arrow",
                            sbb - NP_SB_ARR // 2, STEP_COUNT)
    if worst:
        bad.append("the scroll blit reached the scroll bar: %d sample(s) of "
                   "%d show the arrow cell altered, worst %d bytes (SPEC.md "
                   "27.7.2)" % (seen, STEP_COUNT, worst))
    # ...and the TRACK, near the BOTTOM of it - which is the report's own
    # gesture, "below the thumb but not on the arrow", and is checked to have
    # been that rather than assumed: `down` refuses a click that paged UP. The
    # repaint behind a refused blit letters every visible row, so this one is
    # sampled over six times as long.
    wait_idle(ui)
    seen, worst = watch_bar(ui, np_, say, "C2 track below the thumb",
                            sbb - NP_SB_ARR - 4, PAGE_STEPS, down=True)
    if worst:
        bad.append("a track click took the scroll bar off the screen: %d "
                   "sample(s) of %d show the arrow cell altered, worst %d "
                   "bytes - the repaint behind a refused blit is filling over "
                   "the bar (SPEC.md 27.7.2.1)" % (seen, PAGE_STEPS, worst))
    return "; and ".join(bad) if bad else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine",
                    default=os88marty.machine("os8088_5150_cga"))
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()
    say = (lambda s: None) if a.quiet else (lambda s: print("   ", s, flush=True))

    bad = []
    with os88ui.boot(IMG, machine=a.machine, verbose=not a.quiet) as ui:
        ui.open_drive("A")
        ui.open("README.TXT")                   # SPEC.md 54's association
        ui.settle()
        np_ = Note(ui.m)
        say("package at 0x%04x, np_len %d" % (np_.seg, np_.check()))
        # The true height FIRST: leg A and leg B are both about what a walk is
        # allowed to reach, so both need the number one is not allowed to. A
        # and C then run while the view is still at row 0, which is where it
        # opens - dragging it back would be a second thing to get right.
        total = settle_count(ui, np_)
        say("the note is %d rows" % total)
        # A, then B, then C. A needs the view at the TOP - the up arrow there
        # is the whole of what it tests - and B is what moves it away, taking
        # the thumb to the bottom of the track; C works from wherever it is
        # left, having put [np_drows] back to the truth first.
        for name, leg in (("A", leg_a), ("B", leg_b), ("C", leg_c)):
            why = leg(ui, np_, say, total)
            if why:
                bad.append("leg %s: %s" % (name, why))
    for b in bad:
        print("FAIL", b)
    print("npscroll:", "ok" if not bad else "%d leg(s) FAILED" % len(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
