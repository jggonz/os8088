#!/usr/bin/env python3
"""The text cursor behaves as a REAL DRIVER does when the program draws over
it (SPEC.md 96.10.5.4).

    make kdostest && python3 tests/kdmredraw.py

`tests/kdmcur.py` asks whether the driver can draw a cursor at all.  This asks
what happens when the application draws over what the driver drew - which
every DOS application does, constantly, by writing straight into B800.

**THIS ROW WAS WRITTEN TO PROVE A DEFECT AND MEASURED THE OTHER WAY ROUND, so
read the finding before changing anything here.**  A software text cursor is
an attribute flipped into a cell the driver does not own, and it gets no
notification of the program's store, so it is LOST until the pointer next
moves.  That looked like an obvious bug - a status line or a clock redrawing
under a hand that is holding still takes the cursor with it - and the probe
went red on this box exactly as predicted:

    A drawn     7041 want 7041 ok
    B redrawn   1E2A want 612A BAD          <- the predicted defect
    C restored  1E2A want 1E2A ok
    D unmoved   0000 want 0000 ok

**CuteMouse 1.9.1 under IBM DOS 3.30, on the same machine, answers all four
identically** (COM1 03F8h/IRQ4, Microsoft mode; the driver is demonstrably
alive because A passes and only a driver can draw that cell).  A serial mouse
that is not moving raises no interrupt, so there is nothing to repaint from,
and every DOS program of the era was written against drivers that do this.
So the behaviour stays and this row is a COMPATIBILITY RATCHET: it goes red if
this box ever starts repainting where CuteMouse does not.

WHAT THE FOUR LETTERS CHECK:

  A  the cursor is drawn on the cell to begin with            (the control)
  B  ...and the PROGRAM'S OWN WORD SURVIVES a store over it
  C  a hide then leaves that word alone, rather than restoring the cell the
     driver had saved - which would be a character the program never wrote
  D  the pointer never moved, so B and C are about the cell they claim

**C IS THE HALF THAT IS EASY TO GET WRONG IN THE OTHER DIRECTION.**  Re-saving
and re-drawing on every call banks the driver's OWN inverted cell as the thing
to restore, after which the inversion is permanent and travels with the
pointer for the rest of the session.  That is the failure `apps/dos/dos.asm`'s
own comment warns about and the reason its early return is there.  The probe
polls more than once precisely so C can see it.

VERIFIED TO FAIL: the guarded re-save that would have been the "fix" - repaint
when the cell no longer holds what we wrote - takes B red here, which is the
divergence from CuteMouse this row exists to catch.

BOTH ARMS, because the two hosts reach `dos_m33_paint` differently - the
windowed box through `dos_mou_read` on every `dos_getkey`, `kern_dos` through
that and the tick - and because a windowed arm that asserted the wrong thing
is how the cursor shipped broken once already (docs/FIELD-NOTES.md 54).

MREDRAW.COM runs under a real DOS unchanged (docs/DOS-DEBUGGING.md), which is
how the parity above was measured rather than argued.  With no driver INT 33h
is not installed, function 0 answers AX != FFFF and it prints SKIP.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kdhand                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
RED = "build/mredraw360.img"
MACH = "os8088_5150_cga_gla"


def fail(msg):
    print("kdmredraw: FAIL: %s" % msg)
    sys.exit(1)


def verdict(m, limit=240.0):
    """Wait for MREDRAW's report and return (verdict, {letter: (got, want)})."""
    rows = []

    def reported(_m):
        rows[:] = [r.rstrip() for r in (m.screen() or []) if r.strip()]
        return any("MREDRAW" in r for r in rows)
    try:                                # `limit` is GUEST time
        os88marty.until(m, reported, "MREDRAW's report", poll=0.5, limit=limit)
    except os88marty.MartyError:
        fail("MREDRAW.COM never reported; the last screen was %r" % rows[:12])

    out, said = {}, None
    for r in rows:
        t = r.strip()
        if t.startswith("MREDRAW "):
            said = t.split()[1]
        if " want " in t and t[:1] in "ABCD":
            parts = t.split()
            # `B kept      1E2A want 1E2A ok` - the got is the token before
            # `want` and the want the one after.  THE PROBE'S LABELS ALL END
            # IN A SPACE for this: without one `kept1E2A` is a single token
            # and the got comes back as the label, which reads as a match
            # that never happened.
            i = parts.index("want")
            g, w = parts[i - 1].upper(), parts[i + 1].upper()
            if len(g) != 4 or len(w) != 4:
                fail("could not read %r as `<label> <got> want <want> <ok>` - "
                     "the probe's labels must each end in a space" % t)
            out[t[0]] = (g, w)
    if said is None:
        fail("no MREDRAW verdict line; the screen was %r" % rows[:12])
    for r in rows:
        if "MREDRAW" in r or " want " in r:
            print("  | %s" % r.strip())
    return said, out


def judge(said, got, where):
    if said == "SKIP":
        fail("the probe SKIPPED %s - it found no INT 33h or no text mode, and "
             "under THIS box both are the thing under test" % where)
    if said != "PASS":
        bad = ", ".join("%s got %s want %s" % (k, v[0], v[1])
                        for k, v in sorted(got.items()) if v[0] != v[1])
        extra = ""
        if got.get("B", ("", ""))[0] != got.get("B", ("", ""))[1]:
            extra += (" - B means this box REPAINTED where CuteMouse 1.9.1 "
                      "does not, so a DOS program is now handed a cursor put "
                      "back into a cell it had just drawn itself. That is a "
                      "divergence from the reference rather than an "
                      "improvement on it (SPEC.md 96.10.5.4)")
        if got.get("C", ("", ""))[0] != got.get("C", ("", ""))[1]:
            extra += (" - C means a hide put back the cell the DRIVER had "
                      "saved, which is a character the program never wrote "
                      "and is permanent once it happens")
        fail("%s the cursor does not match the reference driver: %s%s"
             % (where, bad or "see above", extra))


def arm_window():
    """ARM 1: in the window, on the fsx bracket's own surface."""
    with os88ui.boot(SYS, apps=RED, machine=MACH) as ui:
        if not ui.path("B:/MREDRAW.COM"):
            fail("double-clicking B:/MREDRAW.COM opened no window")
        said, got = verdict(ui.m)
        judge(said, got, "IN THE WINDOW")
        print("kdmredraw: in the window, the program's own redraw stands")


def arm_whole():
    """ARM 3: the whole machine.

    A BOOT OF ITS OWN, for `tests/kdmcur.py`'s reason: MREDRAW.COM ends on
    `AH=08h` waiting for a key, so arm 1 leaves a program RUNNING in the box
    and the Memory page cannot be re-armed underneath one.
    """
    with os88ui.boot(SYS, apps=RED, machine=MACH) as ui:
        try:
            kdhand.launch_whole(ui, "MREDRAW.COM")
        except RuntimeError as e:
            fail(str(e))
        said, got = verdict(ui.m)
        judge(said, got, "UNDER kern_dos")
        print("kdmredraw: under kern_dos, the program's own redraw stands")


def main():
    for p in (SYS, RED):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the gate disks" % p)
    arm_window()
    arm_whole()
    print("kdmredraw: ok - BOTH arms agree with CuteMouse 1.9.1, and a hide "
          "leaves the program's own cell alone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
