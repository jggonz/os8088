#!/usr/bin/env python3
"""Can the harness drive the UI with BREAKPOINTS ARMED? (os88marty.bp_trace)

    make && python3 tests/bptrace.py

Until `bp_trace` existed it could not, and the reason was never the emulator:
every verb in `os88ui` and `os88mouse` confirms what it did by reading guest
state - `to` polls the published `mouse_x` until it agrees, `_edge` polls
`mouse_btn`, `open_drive` reads `wm_wins` - and a guest stopped at a
breakpoint publishes nothing new. So an armed breakpoint does not send a click
to the wrong place; it makes the click's own PROOF unobtainable. Seventy-eight
files under tests/ arm breakpoints and, before this, not one of them could use
os88ui at all.

THE ROW IS AN A/B AND HAS TO BE. A pumped gesture that passes proves nothing
on its own - it would pass just as well against a breakpoint that never fired,
which is exactly the failure docs/WRITING-TESTS.md 1 is about. So every
section that asserts the pump works is paired with the same gesture NOT
pumped, which must fail, and fail NAMING THE MACHINE:

  arm A   a bare `bp_exec`, then drive. The guest freezes at the first hit and
          the next verb needing guest progress must raise about the CLOCK
          within a few seconds. MEASURED on this container, the same gesture
          before the `_alive` guard in os88mouse took **332.1 seconds** to
          reach the same conclusion - and reached it from `guest_sleep`'s
          stall arm inside `to`'s retry, having spent all of that in
          `_landed`, whose deadline is in guest CYCLES that a stopped machine
          never spends. It is 2.2s now.
  arm B   the same symbols and the same gesture inside `bp_trace`, which must
          complete, confirm, and hand back the hits.

WHAT IT DELIBERATELY DOES NOT ASSERT. An exact hit count for a gesture: a
repaint's shape is the kernel's business and a row that pins it would fail for
every legitimate redraw change and teach nobody anything (that is what
`os88span.py` and the per-subject rows are for). What is asserted here is the
INSTRUMENT - that the stops are recorded, that they dedupe, that the guest
keeps running between them, and that a symbol never reached records nothing.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

import os88marty                                                 # noqa: E402
import os88ui                                                    # noqa: E402
from os88marty import MartyError                                 # noqa: E402
from os88mouse import Mouse                                      # noqa: E402

IMG = "build/os8088-360.img"
APPS = "build/apps360.img"

# The stall guard allows GUEST_STALL (2s) of a frozen clock before it fires,
# and a round trip on a loaded box is not free. Ten seconds is five times the
# guard and still two orders below what the un-guarded loop cost.
A_LIMIT = 10.0

fails = []


def check(ok, what):
    print("   %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        fails.append(what)
    return ok


def closed(ui):
    """Shut every window, so the next open really is a window PAINT.

    Section 8 asked for one by CLOSING a window and recorded nothing: a close
    restores what was underneath (gfx_restore, wm_su_try) and paints no
    window at all. That is a fact about the window manager worth writing down
    rather than working around silently - the row was wrong, not the trace.
    """
    for w in list(ui.windows()):
        if w.visible:
            ui.close(w)
    os88marty.settle(ui.m)


def unfreeze(m):
    """Put the machine back after arm A left it at a breakpoint."""
    m.breakpoints([])
    m.run()


def main():
    with os88ui.boot(IMG, apps=APPS) as ui:
        m = ui.m
        mo = Mouse(marty=m)

        # --- 1. arm A: a bare breakpoint, and what it costs ------------------
        print("\n1. a bare breakpoint stops the UI dead, and SAYS SO")
        m.bp_exec("wm_draw_win")
        try:
            ui.open_drive("B")              # this one may well confirm: the
        except MartyError:                  # window record is written before
            pass                            # the paint the breakpoint is on
        # THE GESTURE IS NOT THE STOP, and this sampled as though it were.
        # `open_drive` confirms on the window RECORD, which wm_draw_win is
        # reached after - so on a loaded box it returns while the guest is
        # still short of the breakpoint. This read `m.status()` TWICE, once
        # for the verdict and once for the message, and a soak caught the gap
        # between them exactly: `FAIL the guest is at a breakpoint after the
        # gesture ('breakpoint')` - a verdict contradicting its own diagnostic,
        # which is the most expensive thing a failure can print. One sample
        # now, and a bounded wait on the GUEST's clock when it has to.
        st = m.status().get("state")
        if st == "running":
            st = m.wait_stop(limit=A_LIMIT) or m.status().get("state")
        check(st == "breakpoint",
              "the guest is at a breakpoint after the gesture (%r)" % st)
        t0 = time.time()
        try:
            mo.to(300, 120)                 # ...but the POINTER cannot move
            check(False, "moving the pointer against a frozen guest must fail")
        except MartyError as e:
            took = time.time() - t0
            check("GUEST CLOCK HAS NOT MOVED" in str(e),
                  "it raises about the CLOCK, not about the mouse")
            check("breakpoint" in str(e),
                  "...and names the state the machine is actually in")
            check(took < A_LIMIT,
                  "it fails FAST: %.1fs (332.1s before the guard)" % took)
        unfreeze(m)
        os88marty.settle(m)

        # --- 2. arm B: the same symbols, pumped ------------------------------
        print("\n2. the same breakpoints inside bp_trace, driving os88ui")
        t0 = time.time()
        with os88marty.bp_trace(m, "wm_draw_win", "wm_show",
                                "menu_draw_bar") as tr:
            w = ui.path("B:/APPS")          # a full navigation: drive window,
            mo.to(300, 120)                 # folder, a raw pointer move...
            bar = [c[0] for c in ui.menus()]
            ui.menu_pick("Apple", "Task Manager")     # ...and a real menu,
            tm = ui.wait_window("Task Manager")       # which is the press,
        took = time.time() - t0                       # drag and release
        check(w is not None, "os88ui.path CONFIRMED a window: %r" % (w,))
        check("Apple" in bar, "ui.menus() read the live bar: %r" % (bar,))
        check(tm is not None, "menu_pick opened %r" % (tm and tm.title,))
        check(tr.n > 0, "the trace recorded %d stop(s) in %.1fs" % (tr.n, took))
        check(len(tr.names()) >= 2,
              "...across more than one symbol: %r" % (tr.names(),))
        check(m.status().get("state") == "running",
              "the block leaves the guest RUNNING")
        check(tr.error is None, "the pump did not die")
        if tm:                              # ...and close it again: its
            ui.close(tm)                    # worker animates the CPU meter,
        os88marty.settle(m)                 # so no settle below could ever
                                            # converge with it on the screen

        # --- 3. the invariants the pump is built on --------------------------
        print("\n3. the two fixes the pump inherits from bp_count")
        # DEDUPED ON THE SERVER'S `stops` SEQUENCE, not on `instructions`.
        # The pump was written to use the latter and it is not a clock:
        # machine.run() accumulates that count at the END of a batch and
        # returns early when a breakpoint hits, so the batch a stop lands in
        # never reaches it, and what separated two stops was the single
        # instruction the resume itself steps. `stops` counts entries into a
        # stopped state and is exact.
        seq = [h["stops"] for h in tr.hits if h["stops"] is not None]
        if seq:
            check(len(seq) == len(set(seq)) and seq == sorted(seq),
                  "every stop carries a distinct, increasing `stops` "
                  "(%d kept, %s)" % (len(seq), seq[:6]))
        else:
            check(len(tr.hits) == len(set(h["cycles"] for h in tr.hits)),
                  "no `stops` field - the cycles fallback deduped %d stop(s)"
                  % len(tr.hits))
        check(all(h["name"] in ("wm_draw_win", "wm_show", "menu_draw_bar")
                  for h in tr.hits),
              "every stop resolved to an armed symbol, none to a raw address")
        cyc = [h["cycles"] for h in tr.hits]
        check(cyc == sorted(cyc), "the stops are in guest-time order")

        # --- 4. the guest really did keep running -----------------------------
        print("\n4. the guest advances BETWEEN the stops")
        # The claim under test is that the pump resumes, not that the verbs
        # happened not to need it. A gesture whose stops were never resumed
        # would record its first hit and no more, and the clock would stand
        # still - which is what section 1 measures from the other side.
        if len(cyc) >= 2:
            check(cyc[-1] > cyc[0],
                  "guest advanced %.2f Mcycles across the trace"
                  % ((cyc[-1] - cyc[0]) / 1e6))

        # --- 5. `until` is not raised spuriously by a pumped stop ------------
        print("\n5. os88marty.until tolerates a pumped stop")
        # until() raises on a guest that is not running, and it is right to.
        # Under a pump the same reading is a poll landing in a stop window
        # about to end. Without the `_pumping` counter this raises at random,
        # which is the intermittent worth spending a row on.
        with os88marty.bp_trace(m, "wm_draw_win", poll=0.05) as tr2:
            rounds = [0]

            def cond(_):
                rounds[0] += 1
                return rounds[0] > 6        # several polls, so at least one
            try:                            # of them lands in a stop window
                os88marty.until(m, cond, "a few pumped polls",
                                poll=0.15, limit=60.0)
                check(True, "until() survived %d polls under a pump"
                      % rounds[0])
            except MartyError as e:
                check(False, "until() raised under a pump: %s" % str(e)[:120])
            os88marty.guest_sleep(m, 1.0)
        check(tr2.error is None, "the second pump did not die either")

        # --- 6. a symbol never reached records nothing -----------------------
        print("\n6. a trace that should be empty IS empty")
        # The null half of section 2, and the one that makes it mean
        # something: an instrument that reports hits for a symbol nothing
        # calls is reporting noise, and every count above would be worthless.
        with os88marty.bp_trace(m, "wm_show") as tr3:
            os88marty.guest_sleep(m, 2.0)   # nothing opens a window here
        check(tr3.n == 0, "no window shown, no stop recorded (n=%d)" % tr3.n)

        # --- 7. a hot symbol overflows rather than wedging --------------------
        print("\n7. a hot symbol is capped, not fatal")
        # The ceiling this instrument has, exercised rather than described: a
        # breakpoint on a symbol a repaint reaches constantly costs two round
        # trips a hit, so the trace stops KEEPING them and goes on pumping.
        # Wedging there would be the worst outcome - the body would hang.
        #
        # THE GESTURE IS AN OPEN AND THE CAP IS 1, and both are deliberate.
        # This was a window DRAG against `cap=5`, which is a count the kernel
        # decides rather than one this row may pin: the same line read 21, 18
        # and then 3 across three runs, the last one under a loaded box, and
        # failed. A window opening paints a title bar, a menu bar, a content
        # area and eighteen file rows, so "more than one gfx_fill" is a
        # property of the window manager and not of the box - and what is
        # ASSERTED under it is the invariant, that `hits` never exceeds the
        # cap and `overflowed` says so exactly when `n` has passed it.
        closed(ui)
        with os88marty.bp_trace(m, "gfx_fill", cap=1) as tr4:
            ui.open_drive("B")
        check(len(tr4.hits) <= 1, "the cap held: %d kept" % len(tr4.hits))
        check(tr4.overflowed == (tr4.n > 1),
              "overflowed agrees with n (n=%d, overflowed=%s)"
              % (tr4.n, tr4.overflowed))
        check(tr4.n > 1, "a window open reached gfx_fill %d times" % tr4.n)
        check(m.status().get("state") == "running",
              "...and the guest is still running after it")

        # --- 8. regs=True attributes a hit -----------------------------------
        print("\n8. regs=True records the registers that attribute a hit")
        closed(ui)
        with os88marty.bp_trace(m, "wm_draw_win", regs=True) as tr5:
            ui.open_drive("B")
        got = [h for h in tr5.hits if "regs" in h]
        check(bool(got), "%d of %d stops carry a register set"
              % (len(got), len(tr5.hits)))
        if got:
            check(all(k in got[0]["regs"] for k in ("di", "bx", "cs", "ip")),
                  "...with the slots os88span.py attributes windows by")

        # --- 9. on_hit reads guest state AT the stop -------------------------
        print("\n9. on_hit reads the .bss while the guest is stopped")
        # The primitive the hand-rolled pumps actually needed. A value like
        # wm_clip_n is only true INSIDE the routine the breakpoint is on - it
        # is gone a microsecond after the resume - so a row that reads it from
        # the body reads a number belonging to nothing. This is the only way
        # the four converted rows keep meaning what they meant.
        clipn = m.sym("wm_clip_n")

        def peek(mm, rec):
            return mm.read(clipn, 1)[0]

        closed(ui)
        with os88marty.bp_trace(m, "wm_draw_win", on_hit=peek) as tr6:
            ui.open_drive("B")
        got = [h for h in tr6.hits if "hit" in h]
        check(len(got) == len(tr6.hits) and bool(got),
              "every one of %d stop(s) carries its on_hit answer" % len(got))
        check(all(isinstance(h["hit"], int) for h in got),
              "...and the answers are the bytes it read: %r"
              % ([h["hit"] for h in got[:4]],))
        check(tr6.error is None, "an on_hit that works does not fail the trace")

        # --- 10. tr.until: staying in the block until the work has RUN ------
        print("\n10. tr.until - the mistake that cost both hard conversions")
        # A `with` block ends when its BODY ends, and a gesture returns when
        # it is DECODED - not when the repaint it triggers has run. Both of
        # the two harder conversions failed this way in one run, and neither
        # sentence they printed was about the harness: blitcut said "no
        # straddling canvas blit arrived in 240s" and paintsu said the raise
        # cache asks for None KB.
        closed(ui)
        seen_one = []

        def tick(mm, rec):
            seen_one.append(1)
            return None

        with os88marty.bp_trace(m, "wm_draw_win", on_hit=tick) as tr7:
            ui.open_drive("B")
            got = tr7.until(lambda: bool(seen_one), "a window paint",
                            limit=60.0)
        check(got is True, "until() returns True when the work arrives")

        with os88marty.bp_trace(m, "wm_show") as tr8:
            never = tr8.until(lambda: False, "something that cannot happen",
                              limit=5.0, required=False)
        check(never is False,
              "required=False ANSWERS rather than raising - which paintsu's "
              "uncover needs, a blit that never runs being its finding")

        with os88marty.bp_trace(m, "wm_show") as tr9:
            try:
                tr9.until(lambda: False, "something that cannot happen",
                          limit=5.0)
                check(False, "required=True must raise on a timeout")
            except MartyError:
                check(True, "...and required=True raises")

        # --- 11. the stop already there is not charged to the block ---------
        print("\n11. a stop the block did not cause is not counted")
        # What `go()`'s mark buys, and the third thing bp_count was found to
        # be counting. A machine already sitting at a breakpoint when the
        # block opens has not been stopped BY the block, and recording it
        # attributes a stop to a gesture that had not been made yet.
        closed(ui)
        m.bp_exec("wm_draw_win")
        try:
            ui.open_drive("B")              # ...freezes the guest, as in 1
        except MartyError:
            pass
        # THE GESTURE IS NOT THE STOP (section 1's note): open_drive confirms
        # on the window record, which wm_draw_win is reached after, so the
        # stop is waited for on the GUEST's clock rather than sampled once.
        # Sampled once, this was the whole of why the row ran `alone`: at four
        # emulators it read 'running' and failed, with the park happening a
        # moment later.
        was = m.status().get("state")
        if was == "running":
            was = m.wait_stop(limit=A_LIMIT) or m.status().get("state")
        check(was == "breakpoint", "the guest is parked at a stop (%r)" % was)
        with os88marty.bp_trace(m, "wm_show") as tr10:
            os88marty.guest_sleep(m, 1.0)   # nothing opens a window here
        check(tr10.n == 0,
              "the parked stop was not charged to the block (n=%d)" % tr10.n)
        check(m.status().get("state") == "running",
              "...and the block released the machine it found stopped")

    print("")
    if fails:
        print("bptrace: %d FAILED" % len(fails))
        for f in fails:
            print("  FAIL:", f)
        return 1
    print("bptrace: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
