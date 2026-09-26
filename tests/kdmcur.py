#!/usr/bin/env python3
"""`INT 33h` DRAWS the text cursor under kern_dos, and NOT in the window.

    make kdostest && python3 tests/kdmcur.py

SPEC.md 96.10.5.  A DOS mouse driver draws its own pointer - there is no
compositor and no arrow the machine keeps for it, so `01h` means *put a cursor
on the screen and keep it under the mouse*, and if the driver does not,
nothing does.  This box answered `01h` and `02h` with a shrug, on the
reasoning that the kernel owns the pointer - which is right in the WINDOWED
host and wrong under `kern_dos`, where the program owns every pixel and the
kernel is not running at all.

**SO THE ROW IS TWO ARMS AND BOTH ARE ASSERTIONS.**  Under `kern_dos` the
cursor must be drawn; in the window it must NOT be, because there `B800` is
the kernel's framebuffer and writing to it is corruption rather than a
feature.  A row that only ran arm 3 would pass just as happily with a box
that scribbled on the desktop.

**THE ASSERTION IS ARITHMETIC, NOT A PHOTOGRAPH.**  A text cursor is not a
bitmap, it is an attribute the driver flips:

    displayed = (cell AND screen_mask) XOR cursor_mask

so MCURSOR.COM puts a KNOWN word in every cell, shows the cursor, and reads
the cell under the pointer back out of the framebuffer.  It never has to see
the screen, the mouse never has to move, and there is nothing to settle.  It
fills with `stosw` and not through the BIOS on purpose: that is what a DOS
application does, and a driver that only saw `int 10h` writes would pass a
test written the other way and fail every real program.

WHAT THE FIVE LETTERS CHECK, each a different way for this to be broken:

  A  the cell under the pointer is the INVERSE of what was written
  B  ...and `02h` puts the original back, byte for byte
  C  the masks are STATE - a second `0Ah` with 80FF/F000 repaints to those.
     Microsoft Works sets 77FF/7700 and then 80FF/F000 twice more, measured
     against IBM DOS 3.30 with CTMOUSE, so a hard-coded pair draws the wrong
     cursor for most of a session
  D  the counter NESTS: hide, hide, show leaves it HIDDEN
  E  ...and the fourth call brings it back
  F  **A FUNCTION WITH NO RETURN VALUE LEAVES `AX` ALONE** (SPEC.md 96.10.6)
     - Microsoft Works's own test, instruction for instruction: it sets the Y
     range with `08h` and stores `AL` as its `mouse present` flag, so a box
     that zeroes `AX` there has told it there is no mouse at the end of an
     init it answered correctly. That is what the first version of this
     feature shipped, and A-E could not see it: the program then never calls
     `01h` at all, while the handler it installed at `0Ch` keeps working, so
     the buttons work and nothing is drawn (docs/FIELD-NOTES.md 54)

MEASURED against the build before the feature: the window arm reads exactly
as it does now, and the kern_dos arm read `A 0741 want 7041 BAD`, `C ... BAD`,
`E ... BAD` with B and D passing - because B and D expect the untouched cell
and an absent cursor gives them that for free.  Those two are the controls
rather than the finding, which is why all five are required and not three.

MCURSOR.COM runs under a real DOS unchanged (docs/DOS-DEBUGGING.md), so every
answer is checkable against CTMOUSE.  With no driver `INT 33h` is not
installed, function 0 answers AX != FFFF and it prints SKIP.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import kdhand                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
CUR = "build/mcursor360.img"
MACH = "os8088_5150_cga_gla"
CELL = "0741"                      # what MCURSOR.COM writes into every cell


def fail(msg):
    print("kdmcur: FAIL: %s" % msg)
    sys.exit(1)


def verdict(m, limit=240.0):
    """Wait for MCURSOR's report and return (verdict, {letter: (got, want)})."""
    seen = {"rows": []}

    def reported(_):
        seen["rows"] = [r.rstrip() for r in (m.screen() or []) if r.strip()]
        return any("MCURSOR" in r for r in seen["rows"])
    try:                                # `limit` becomes a GUEST budget
        os88marty.until(m, reported, "MCURSOR.COM to report", poll=0.5,
                        limit=limit)
    except os88marty.MartyError:
        fail("MCURSOR.COM never reported; the last screen was %r"
             % seen["rows"][:12])
    rows = seen["rows"]

    out, said = {}, None
    for r in rows:
        t = r.strip()
        if t.startswith("MCURSOR "):
            said = t.split()[1]
        if " want " in t and t[:1] in "ABCDE":
            parts = t.split()
            # `A shown    0741 want 7041 BAD` - the got is the token before
            # `want` and the want the one after it. THE PROBE'S LABELS ALL END
            # IN A SPACE for this: without one, `remasked0741` is a single
            # token and the got comes back as the label, which reads as the
            # cursor having been drawn where it was not.
            i = parts.index("want")
            g, w = parts[i - 1].upper(), parts[i + 1].upper()
            if len(g) != 4 or len(w) != 4:
                fail("could not read %r as `<label> <got> want <want> <ok>` - "
                     "the probe's labels must each end in a space" % t)
            out[t[0]] = (g, w)
    if said is None:
        fail("no MCURSOR verdict line; the screen was %r" % rows[:12])
    for r in rows:
        if "MCURSOR" in r or " want " in r:
            print("  | %s" % r.strip())
    return said, out


def arm_window():
    """ARM 1: the cursor must be drawn HERE TOO.

    **THIS ARM ASSERTED THE OPPOSITE AND PASSED** (docs/FIELD-NOTES.md 54).
    96.10.5.1 first refused `DHK_TXT` to the windowed host, on the reasoning
    that there the OS owns every pixel - so this arm was written to check that
    nothing is drawn, and it encoded that premise as a REQUIREMENT. It passed
    for exactly the reason the code was wrong, which is a negative control's
    one failure mode and is worth the paragraph.

    A DOS program is never outside the fullscreen bracket: `dos_fsx_main`
    calls `OSAPI_FSX_MODE` with `FSXM_TEXT80` before the program is entered
    and the kernel hands back the surface in `dos_fsi` - the screen the box's
    own console already writes into. So both arms draw, and what differs is
    only where the hook reads the surface FROM.
    """
    with os88ui.boot(SYS, apps=CUR, machine=MACH) as ui:
        m = ui.m
        if not ui.path("B:/MCURSOR.COM"):
            fail("double-clicking B:/MCURSOR.COM opened no window")
        said, got = verdict(m)
        if said == "SKIP":
            fail("the probe SKIPPED in the window - it found no INT 33h or no "
                 "text mode, and under THIS box both are the thing under test")
        if said != "PASS":
            bad = ", ".join("%s got %s want %s" % (k, v[0], v[1])
                            for k, v in sorted(got.items()) if v[0] != v[1])
            fail("IN THE WINDOW the cursor is not right: %s. The windowed "
                 "host draws on the fsx bracket's own surface (dos_fsi), "
                 "which SPEC.md 96.10.5.1 first refused it - see "
                 "docs/FIELD-NOTES.md 54" % (bad or "see above"))
        print("kdmcur: in the window, the cursor is drawn on the bracket's "
              "own surface")


def arm_whole():
    """ARM 3: the whole machine, where it must be drawn.

    **A BOOT OF ITS OWN, and that is not laziness.** The two arms cannot share
    one: MCURSOR.COM ends on `AH=08h` waiting for a key, so the window arm
    leaves a program RUNNING in the box - and the Memory page cannot be
    re-armed underneath one (tests/kdmouse.py carries the same rule). Tearing
    that down is more moving parts than a second boot, and every one of them
    fails as a message about the wrong subject.
    """
    with os88ui.boot(SYS, apps=CUR, machine=MACH) as ui:
        m = ui.m
        # `tests/kdmouse.py`'s sequence, and for its reasons: open the BOX and
        # not the program (a program launched by association is running in the
        # window and the Memory page cannot be re-armed underneath it), change
        # drive first, then name the program without one (SPEC.md 96.48).
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open DOS.O88 off the system disk")
        os88marty.settle(m)
        m.type_text("B:\n")
        os88marty.settle(m)
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        base = pseg << 4
        mo = os88mouse.Mouse(marty=m)

        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        dis = kdhand.rec(m, pseg, dm, kdhand.RD_DIS)
        if dis & (1 << kdhand.WHOLE):
            fail("the Shut down the OS arm is GREYED - this build does not "
                 "carry kern_dos as a part (SPEC.md 96.36.1)")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = kdhand.rec(m, pseg, dm, kdhand.RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + kdhand.WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if kdhand.rec(m, pseg, dm, kdhand.RD_SEL) != kdhand.WHOLE:
            fail("clicking the Shut down the OS arm did not pick it")
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_pln"))
        os88marty.settle(m)
        m.type_text("MCURSOR.COM")
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if kdhand.alert_up(m, base, dm):
            mo.click(*kdhand.alert_button(m, base, dm, 1))      # Proceed

        said, got = verdict(m)
        if said == "SKIP":
            fail("the probe SKIPPED under kern_dos - INT 33h not installed, "
                 "or kd_mou_txt refused the mode (SPEC.md 96.10.5.1)")
        if said != "PASS":
            bad = ", ".join("%s got %s want %s" % (k, v[0], v[1])
                            for k, v in sorted(got.items()) if v[0] != v[1])
            fail("under kern_dos the cursor is not right: %s. SPEC.md "
                 "96.10.5 says what each letter means" % (bad or "see above"))
        print("kdmcur: under kern_dos, the cursor is drawn, taken off, "
              "remasked and nested")


def main():
    for p in (SYS, CUR):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the gate disks" % p)
    arm_window()
    arm_whole()
    print("kdmcur: ok - drawn on BOTH arms, and a no-output function "
          "leaves AX alone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
