#!/usr/bin/env python3
"""Does Alt+Enter take the DOS box into full screen, and back out again?
(SPEC.md 96.33.5.1, 9.7.1)

    make && python3 tests/dosaltenter.py

**THE KEY IS INVISIBLE TO int 16h ON THE MACHINE THIS PROJECT IS FOR**, which
is the whole reason there is anything to test. Measured on the period XT ROM
this row boots, by reading the guest's own BIOS key buffer at 0040:001A/001C
as each key arrived: `Enter` moves the tail and enqueues `1C0D`, `Alt+F` moves
it and enqueues `2100`, and **Alt+Enter does not move it at all**. Alt reaches
the ROM perfectly well - 0040:0017 bit 3 reads 1 while it is held - and the
83-key translation table simply has no entry for the combination. So the
kernel latches the SCANCODE instead (SPEC.md 9.7.1) and `ui_task` hands the
front window `AX = 1C00`.

Leg 0 is that measurement, kept runnable, because every other leg here is
evidence about a mechanism that only needs to exist if this stays true. A ROM
that started delivering Alt+Enter would make legs 1-3 pass through a path they
are not testing.

THE ASSERTION IS THE KERNEL'S OWN `fsx_cur`, not a screenshot. §53's bracket
is either up or it is not: 0xFF is "no mode set" and anything else is the
FSXM_* the app asked for, so "did the box go full screen" is one byte and
needs no pixels squinted at. A screenshot of an 80x25 text mode and a
screenshot of a desktop with a DOS window on it differ in ten thousand ways
that have nothing to do with this key.

FOUR LEGS, and the last two are the ones that were RED while this was being
written:

  0. the BIOS enqueues nothing for Alt+Enter, and does for Enter and Alt+F
  1. Alt+Enter from the window puts the bracket UP
  2. Alt+Enter from full screen takes it DOWN - and STAYS down. The first
     build bounced here: the press that ends the bracket is still held when
     `fsx_restore` runs, and the kernel's latch was still set from it, so
     `ui_task` handed the window a synthesised Alt+Enter and the box went
     straight back in. That is why `fsx_restore` clears `[kbd_ae]`.
  3. Esc still leaves, because §96.33.3 says it does and this row must not be
     the thing that quietly took it away

A HELD key must not toggle either: a typematic repeat is byte-identical to a
fresh press through int 16h, and it is the MAP that tells them apart
(SPEC.md 9.7.1). Leg 2 covers it by construction - the harness holds Alt down
across the Enter press, so the repeat arm is exercised on every leg here.
"""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))

import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = "os8088_5150_cga_gla"
FSX_NONE = 0xFF                 # [fsx_cur]: no mode set, so no bracket is up


def _fail(msg):
    print("FAIL: " + msg)
    return False


def _bracket(ui):
    """[fsx_cur] - FSX_NONE when no bracket owns the screen.

    `sym` answers FLAT, so this is `read` and not `readseg`: the pair land
    0x600 apart, in real code, on a byte that is simply never written - which
    reads as "a bracket is already up" and is what this row did first.
    """
    return ui.m.read(ui.m.sym("fsx_cur"), 1)[0]


def _settle_bracket(ui, want, limit=8.0):
    """Wait for [fsx_cur] to reach `want`, on the GUEST's clock.

    A mode set is an int 10h and a restore is a repaint, so neither is
    instant - and a host sleep sized on an idle box is what SOAK-PARALLEL §1
    measured hands a loaded one 37% less work. This reads the byte that
    answers the question instead.
    """
    try:                                # the budget is GUEST seconds, so a
        os88marty.until(ui.m, lambda _: _bracket(ui) == want,  # loaded box
                        "[fsx_cur] to reach %02X" % want,      # cannot cut it
                        poll=0.15, limit=limit)
    except os88marty.MartyError:
        pass                            # the caller reports what it reads
    return _bracket(ui)


def leg0_bios(ui):
    """The BIOS enqueues NOTHING for Alt+Enter - the premise of all of it."""
    m = ui.m

    def tail():
        return m.readseg(0x0040, 0x001C, 2)[0]

    def press(fn):
        t0 = tail()
        fn()
        os88marty.pace(m, 0.8)          # nothing to wait ON for Alt+Enter,
                                        # whose whole finding is no change
        return t0, tail()

    ok = True
    t0, t1 = press(lambda: m.key("Enter"))
    if t1 == t0:
        ok = _fail("leg 0: the ROM enqueued nothing for a plain Enter - this "
                   "row cannot say anything about Alt+Enter")
    else:
        print("  leg 0: Enter      tail %02X -> %02X, enqueued" % (t0, t1))

    t0, t1 = press(lambda: m.alt("KeyF"))
    if t1 == t0:
        ok = _fail("leg 0: the ROM enqueued nothing for Alt+F either, so Alt "
                   "is not reaching it and leg 0's real finding is untested")
    else:
        print("  leg 0: Alt+F      tail %02X -> %02X, enqueued" % (t0, t1))

    t0, t1 = press(lambda: m.alt("Enter"))
    if t1 != t0:
        ok = _fail("leg 0: this ROM DOES enqueue Alt+Enter (tail %02X -> %02X)"
                   ". SPEC.md 9.7.1's premise does not hold on it, and legs "
                   "1-3 below are passing through a path they do not test"
                   % (t0, t1))
    else:
        print("  leg 0: Alt+Enter  tail %02X unchanged - NOTHING ENQUEUED, "
              "which is the finding" % t0)
    return ok


def main():
    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine=MACHINE) as ui:
        m = ui.m
        ok = leg0_bios(ui)

        ui.path("A:/APPS/DOS.O88")
        print("  DOS.O88 is open")

        up = _bracket(ui)
        if up != FSX_NONE:
            return _fail("a bracket was already up (%02X) before any key" % up)

        # --- leg 1: into full screen ------------------------------------
        m.alt("Enter")
        got = _settle_bracket(ui, 0)            # FSXM_TEXT80 is 0
        if got == FSX_NONE:
            ok = _fail("leg 1: Alt+Enter in the window did NOT go full screen "
                       "- [fsx_cur] is still %02X" % got)
        else:
            print("  leg 1: Alt+Enter -> full screen, [fsx_cur]=%02X" % got)

            # --- leg 2: ...and back out, and it STAYS out ---------------
            # HELD, and the hold is the test rather than padding: inside the
            # bracket ui_task is not dispatching (SPEC.md 53.1), so this
            # direction is read off a LEVEL poll once a frame - 55 ms here,
            # the box asking for no FSXF_FASTTICK - and a press and release
            # inside one interval is invisible to it. No finger produces one;
            # the debug server does, and that is the difference being paid
            # for. 0.08 host seconds is ~0.3 guest, five frames, and stays
            # under the ~0.5 s typematic delay leg 4 is about.
            m.alt("Enter", hold=0.08)
            got = _settle_bracket(ui, FSX_NONE)
            if got != FSX_NONE:
                ok = _fail("leg 2: Alt+Enter in full screen did not come back "
                           "- [fsx_cur] is %02X" % got)
            else:
                os88marty.pace(m, 1.5)          # the bounce takes one ui_task
                again = _bracket(ui)            # pass, so LOOK after one
                if again != FSX_NONE:
                    ok = _fail("leg 2: it came back and then went STRAIGHT IN "
                               "again ([fsx_cur]=%02X) - the latch the ISR set "
                               "for the leaving press was not dropped "
                               "(SPEC.md 9.7.1, fsx_restore)" % again)
                else:
                    print("  leg 2: Alt+Enter -> windowed, and it stays there")

        # --- leg 3: Esc is untouched (SPEC.md 96.33.3) ---------------------
        if _bracket(ui) == FSX_NONE:
            m.alt("Enter")
            if _settle_bracket(ui, 0) == FSX_NONE:
                ok = _fail("leg 3: could not get back into full screen to "
                           "test Esc")
            else:
                m.key("Escape")
                got = _settle_bracket(ui, FSX_NONE)
                if got != FSX_NONE:
                    ok = _fail("leg 3: ESC NO LONGER LEAVES full screen "
                               "([fsx_cur]=%02X) - SPEC.md 96.33.3's way out "
                               "was taken away by this change" % got)
                else:
                    print("  leg 3: Esc still leaves full screen")

        # --- leg 4: a HELD key is ONE toggle (SPEC.md 9.7.1) ---------------
        # The ROM repeats the LAST key pressed at ~10Hz and a repeat is
        # byte-identical to a press through int 16h; the map is what tells
        # them apart, and `kbd_track` arms the latch only when the bit was
        # CLEAR before this make set it.
        #
        # WHERE THAT IS VISIBLE took a negative control to find, and it is
        # not where it was first looked for. Holding Alt+Enter from the
        # WINDOW proves nothing: the first press puts the bracket up, and
        # from then on ui_task is not dispatching (SPEC.md 53.1) and
        # `[dos_aedn]` absorbs the rest - so that leg passed with the guard
        # taken out, which is a green leg testing nothing.
        #
        # It is visible on the way BACK. Leaving full screen on a press that
        # is still held puts the box in the window with Enter repeating and
        # Alt down, and `fsx_restore` has just cleared the latch - so every
        # repeat is a fresh chance to set it again. Without the guard the box
        # is thrown straight back into full screen by a key the user is
        # merely slow to let go of; with it, a hold is one toggle.
        if _bracket(ui) == FSX_NONE:
            m.alt("Enter", hold=0.08)               # ...in, and released
            if _settle_bracket(ui, 0) == FSX_NONE:
                ok = _fail("leg 4: could not get into full screen to start")
            else:
                m.alt("Enter", hold=0.6)            # ...and back out, HELD
                got = _settle_bracket(ui, FSX_NONE) # well past the ~0.5s
                if got != FSX_NONE:                 # typematic delay
                    ok = _fail("leg 4: a HELD Alt+Enter never showed the box "
                               "windowed ([fsx_cur]=%02X). With the guard out "
                               "this is what a re-entry looks like from here: "
                               "each repeat re-arms the latch, so the very "
                               "next ui_task pass puts the bracket back up "
                               "and the windowed state is never observed "
                               "(SPEC.md 9.7.1)" % got)
                else:
                    os88marty.pace(m, 1.5)
                    again = _bracket(ui)
                    if again != FSX_NONE:
                        ok = _fail("leg 4: it left full screen and a "
                                   "TYPEMATIC REPEAT of the same held key put "
                                   "it back ([fsx_cur]=%02X) - kbd_track is "
                                   "latching repeats as presses "
                                   "(SPEC.md 9.7.1)" % again)
                    else:
                        print("  leg 4: a HELD Alt+Enter is ONE toggle - its "
                              "repeats do not put it back")
        else:
            ok = _fail("leg 4: the box was still full screen going in")

    print("dosaltenter: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
