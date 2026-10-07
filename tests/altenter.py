#!/usr/bin/env python3
"""Does Alt+Enter reach full screen in BOTH of the mechanisms apps use?
(SPEC.md 11.2.1.1, 9.7.1)

    make && python3 tests/altenter.py

tests/dosaltenter.py is the same key on the DOS box and covers the kernel
half - the latch, the typematic guard, the claim, the fsx_restore drop. This
row is about the APP half, and it exists because SPEC.md 11.2.1.1 binds the
chord across two mechanisms that share nothing:

  THE LATCH (SPEC.md 11.2)     ArtfulType. The window stays an ordinary
                               window with WF_FULL set, so it keeps taking
                               W_ONKEY while it is full screen - ONE
                               `cmp ax, KEY_ALTENTER` is both directions, and
                               what this leg proves is that the kernel's
                               synthesised keystroke (SPEC.md 9.7.1) reaches
                               an app that did nothing but test for it.

  THE BRACKET (SPEC.md 53)     Tracker. Inside a bracket `ui_task` dispatches
                               nothing at all (53.1), so the synthesised key
                               cannot arrive and the app's own int 16h poll
                               cannot see it either - the two directions are
                               read from two different places, and the
                               leaving one is apps/os88alt.inc's poll of the
                               key-state map. Nothing else in the suite
                               executes that file.

THE ASSERTION IS A KERNEL BYTE, never a screenshot, and there are TWO of them
because the mechanisms are two:

  `[wm_fs]`      the latched window, 0 = none (SPEC.md 11.2)
  `[fsx_task]`   the bracket's owner, 0xFFFF = no bracket up (SPEC.md 53.1)

**`[fsx_cur]` IS THE WRONG BYTE HERE and cost this row an hour.** It is the
FSXM_* mode a bracket set, and Paint's is a SAME-MODE bracket (SPEC.md 53.7):
it takes the screen exclusively and sets no mode at all, so `[fsx_cur]` stays
0xFF throughout and reads exactly like a feature that does not work. Missile
Command, Tank and Clear Skies are the same shape. Only the DOS box and Telnet
set a mode, which is why tests/dosaltenter.py can use it and this cannot.

**THE HOLD IS THE TEST, not padding**, on every leg that leaves a bracket.
The leaving direction is a LEVEL read (SPEC.md 9.7), so the key has to still
be down when the app's loop next looks - and those loops run at their own
frame pace, not the harness's. Measured while writing this: Paint leaves
reliably on a 0.5 s hold and NOT on 0.12 s, which is a poll interval and not
a defect - no finger produces a 0.12 s Alt+Enter, the debug server does. The
entering direction needs no hold at all, being an edge the ISR latched.
"""
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))

import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = "os8088_xt_vga"
NO_BRACKET = 0xFFFF             # [fsx_task] when no SPEC.md 53 bracket is up
HOLD = 0.5                      # a human keystroke, in host seconds - and
                                # host is right HERE, because the hold is the
                                # harness holding a key down and not the row
                                # waiting for the guest: m.alt's own argument
                                # is wall-clock. What has to be guest time is
                                # every WAIT below (see State.wait)
GUEST_HZ = getattr(os88marty, "GUEST_HZ", 4772727.0)


def _fail(msg):
    print("FAIL: " + msg)
    return False


class State:
    def __init__(self, m):
        self.m = m
        self.wm_fs = m.sym("wm_fs")
        self.fsx = m.sym("fsx_task")

    def latched(self):
        b = self.m.read(self.wm_fs, 2)
        return b[0] | (b[1] << 8)

    def bracket(self):
        b = self.m.read(self.fsx, 2)
        return b[0] | (b[1] << 8)

    def wait(self, read, want, limit=10.0):
        """Poll a reader until it agrees with `want`, on the guest's terms.

        A latch repaints the whole window and a bracket restore repaints the
        desktop, so neither transition is instant - and a host sleep sized on
        an idle box hands a loaded one 37% less work (SOAK-PARALLEL 1).

        THAT SENTENCE WAS HERE AND THE CODE DID NOT DO IT: the budget was
        `time.time() + limit`, which is host seconds, so 10 of them are ~6.3
        guest seconds under a four-wide soak and fewer still on a busier box.
        The 2026-09-21 run read `a SECOND Alt+Enter did not enter` for a row
        that classifies 0/3 alone - a wait that gave up, reported as the
        feature refusing. It is guest cycles now, which a loaded box cannot
        shorten.
        """
        c0 = self.m.status()["cycles"]
        while (self.m.status()["cycles"] - c0) / GUEST_HZ < limit:
            if want(read()):
                return read()
            time.sleep(0.15)
        return read()

    def rest(self, secs):
        """Let `secs` of the GUEST's clock go by - `time.sleep`'s replacement.

        The two places this row pauses without polling are a splash that
        animates and the bounce check after a restore, and both are about
        giving the MACHINE time to do something wrong. Host seconds are the
        wrong unit for that for the reason above."""
        c0 = self.m.status()["cycles"]
        while (self.m.status()["cycles"] - c0) / GUEST_HZ < secs:
            time.sleep(0.05)


def leg_latch(ui, st):
    """ArtfulType: SPEC.md 11.2's latch, where one test is both directions."""
    ok = True
    ui.path("B:/APPS/ARTFUL.O88")
    if st.latched():
        return _fail("latch: a window was already full screen before any key")

    ui.m.alt("Enter")
    got = st.wait(st.latched, lambda v: v != 0)
    if got == 0:
        ok = _fail("latch: Alt+Enter did not put ArtfulType full screen - "
                   "[wm_fs] is still 0. The kernel's synthesised keystroke "
                   "(SPEC.md 9.7.1) reached nothing, or the app never armed "
                   "the key-state map (OS88_ALTENTER_ARM)")
    else:
        print("  latch:   Alt+Enter -> full screen, [wm_fs]=%04X" % got)
        ui.m.alt("Enter", hold=HOLD)
        got = st.wait(st.latched, lambda v: v == 0)
        if got != 0:
            ok = _fail("latch: Alt+Enter did not come back out - [wm_fs] is "
                       "%04X. A latched window keeps taking W_ONKEY, so the "
                       "same test serves both ways and one of them is "
                       "unreachable" % got)
        else:
            print("  latch:   Alt+Enter -> windowed again")
    ui.menu_pick("ArtfulType", "Close")
    return ok


def leg_bracket(ui, st):
    """Tracker: SPEC.md 53's bracket, where leaving is os88alt.inc's poll."""
    ok = True
    ui.path("B:/APPS/TRACKER.O88")
    st.rest(2.0)                        # its splash animates, so `settle`
                                        # cannot return - see SOAK-PARALLEL 11
    if st.bracket() != NO_BRACKET:
        return _fail("bracket: one was already up before any key")

    ui.m.alt("Enter")
    got = st.wait(st.bracket, lambda v: v != NO_BRACKET)
    if got == NO_BRACKET:
        # WHICH of the three it was: the key is latched in [kbd_ae] and spent
        # on the FRONT window only (SPEC.md 9.7.1), so a front window that is
        # not Tracker, a latch still set, and a refusal on the toast are three
        # different defects that read identically below
        fr = ui.front()
        print("  bracket: front=%r kbd_ae=%02X toast=%r"
              % (fr.title if fr else None,
                 ui.m.read(ui.m.sym("kbd_ae"), 1)[0], ui.toast()))
        return _fail("bracket: Alt+Enter did not take Tracker full screen - "
                     "[fsx_task] is still %04X. Either the kernel's "
                     "synthesised keystroke (SPEC.md 9.7.1) reached nothing, "
                     "or the app never armed the map (OS88_ALTENTER_ARM)"
                     % got)
    print("  bracket: Alt+Enter -> full screen, [fsx_task]=%04X" % got)

    ui.m.alt("Enter", hold=HOLD)
    got = st.wait(st.bracket, lambda v: v == NO_BRACKET)
    if got != NO_BRACKET:
        ok = _fail("bracket: Alt+Enter did not leave - [fsx_task] is %04X. "
                   "Inside a bracket nothing is dispatched (SPEC.md 53.1), "
                   "so this direction is apps/os88alt.inc's poll of the "
                   "key-state map and nothing else can carry it" % got)
    else:
        st.rest(1.5)                    # a bounce takes one ui_task pass,
        again = st.bracket()            # so look after one
        if again != NO_BRACKET:
            ok = _fail("bracket: it left and went STRAIGHT BACK IN "
                       "([fsx_task]=%04X). Either fsx_restore did not drop "
                       "the kernel's latch (SPEC.md 9.7.1) or the app did "
                       "not seed OS88_ALTENTER_SEED and read the press that "
                       "opened the bracket as the press that closes it"
                       % again)
        else:
            print("  bracket: Alt+Enter -> windowed, and it stays there")

    # --- ...and TWICE, because once proves less than it looks --------------
    # THE SECOND CYCLE IS THE WHOLE POINT OF THIS BLOCK. Paint's first was
    # perfect and its second entry was refused, for a fortnight of debugging
    # that blamed the bracket poll: os88alt.inc declared `section .bss`, which
    # in a package ALIASES the first byte of its own hand-chained bss, and the
    # seed's store of 1 landed on a byte that behaves like `[pt_fs]` left set
    # (SPEC.md 11.2.1.1). One round trip cannot see that class of bug at all.
    if st.bracket() == NO_BRACKET:
        ui.m.alt("Enter")
        if st.wait(st.bracket, lambda v: v != NO_BRACKET) == NO_BRACKET:
            ok = _fail("bracket: a SECOND Alt+Enter did not enter. One cycle "
                       "is not evidence: apps/paint passes the first and "
                       "refuses the second (SPEC.md 11.2.1.1)")
        else:
            print("  bracket: ...and a second cycle enters too")
            ui.m.alt("Enter", hold=HOLD)
            if st.wait(st.bracket, lambda v: v == NO_BRACKET) != NO_BRACKET:
                ok = _fail("bracket: the second cycle went in and would not "
                           "come out - the poll works once and then does not")
            else:
                print("  bracket: ...and leaves again, so the poll is not "
                      "one-shot")
    return ok


def main():
    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine=MACHINE) as ui:
        st = State(ui.m)
        ok = leg_latch(ui, st)
        ok = leg_bracket(ui, st) and ok
    print("altenter: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
