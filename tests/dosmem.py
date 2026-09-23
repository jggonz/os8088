#!/usr/bin/env python3
"""The Memory page's two arms and their subsections (SPEC.md 96.36, 96.25, 47).

The choice of how much of the machine a DOS program gets used to be a CHECK
BOX, which holds two answers.  It was three arms and is TWO since SPEC.md
96.36.5 - `Keep the disk cache` and `Take the disk cache too` differed in the
cache and in nothing else, so they were one mode with a dial set two ways and
the dial is its own control now (96.36.6).  What is left is WHERE the program
runs, and the second arm - shut os8088 down and take the machine - is LIVE on
every shipped disk since SPEC.md 96.40.3.

Each arm heads a SUBSECTION, so the pitch is a subsection's height and
`os88ui_rad` centres its ring in a ROW rather than in the pitch (13.17.5).

**THIS ROW ASSERTED THE OPPOSITE FOR FOUR WAVES AND WAS RIGHT TO.** Arm 3 was
greyed with its reason on the glass, first because `kern_dos` was not written
and then because it rode a gate disk while `$(SYSROOT)` shipped the plain
package.  `dos_mem_whole` reads the part table's own length word, so the arm
is live exactly when the package the box is running from carries `kern_dos` -
and it does now.  So every assertion here is inverted, and what the row
catches is inverted with it: a shipped floppy that has lost the part, a
`dos_mem_whole` that has started refusing, or a reason drawn beside an arm
that works.  `tests/kdpart.py` says the same thing about the DISK; this one
says it about the GLASS.

WHAT IT WOULD CATCH, and every one was seen FAILING on the way to writing it
(docs/WRITING-TESTS.md 1):

  - the record filled in the wrong ORDER      -> dos_mem_whole answers in SI
    (dos_mrad_place's own bug, twice)            and so does the record
                                                 pointer: the group drew at
                                                 screen 0,0 over the menu bar
                                                 and the rect stayed 0,0,0,0
  - [dos_keepc] not being OS88UI_RD_SEL        -> the page shows one arm and
    (a second copy of the pick)                  dos_run sizes for another
  - the DEFAULT arm flipping polarity          -> DOS_MEM_IN is 0 and a
    (a copied test's "1 means keep")             copied tick byte was 1, so a
                                                 test that assumed the tick
                                                 shut the OS down exactly when
                                                 it should not
  - the third arm greyed on a shipped disk     -> either the disk lost the
                                                 part (SPEC.md 96.40.3's one
                                                 Makefile variable) or
                                                 dos_mem_whole has started
                                                 refusing. Both are a
                                                 capability silently gone
  - the arm live and NOT pickable              -> os88ui_radhit reading the
                                                 DIS bits wrongly
  - the reason still drawn under a live arm    -> it sits at the labels' own
                                                 indent, so it reads as a
                                                 fourth arm that works
  - dos_mem_fix demoting a pick that IS        -> a .LNK asking for the whole
    honourable                                   machine quietly sized against
                                                 the arm below

IT RUNS ON A 1bpp ADAPTER ON PURPOSE (SPEC.md 47.2): grey rounds to black in
text there, so a greyed label is a CHECKERBOARD and a live one is solid - the
one adapter class where "is this row disabled" is a pixel fact rather than a
colour.  On VGA both are legible and the assertion would be about CDGRAY.
That is why the pixel half survives the inversion: `all three labels are
solid` is as sharp a reading as `one of them is stippled`.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
MACH = "os8088_5150_cga_gla"

# os88ui.inc's record, mirrored here for the same reason the test carries no
# layout: these are the SDK's offsets and the box does not own them.
RD_RECT, RD_ITEMS, RD_N, RD_SEL, RD_PITCH, RD_DIS = 0, 8, 10, 12, 14, 16
INOS, WHOLE, NARM = 0, 1, 2
CK_RECT, CK_LABEL, CK_ON = 0, 8, 10     # os88ui.inc's check box
RDROWH = 20                             # ...and the cap a tall pitch centres in
DR_SEL, DR_OPEN, DR_TOP = 12, 16, 22    # ...and the DROP-DOWN's, for the dial
DRIH = 12                               # OS88UI_DRIH, an item's pitch
# [dos_cache]'s rows - ONE LIST ON BOTH ARMS since SPEC.md 18.95.8 gave the
# box a door that takes a width (96.36.6). Auto is 0 and is deliberately NOT
# driven below: on a 640KB machine the kernel's own solve picks 32K, so Auto
# and 32K read the same figure and a distinctness check over them would fail
# for a true reason.
CA_32, CA_18, CA_9, CA_OFF = 1, 2, 3, 4


def fail(msg):
    print("dosmem: FAIL: %s" % msg)
    sys.exit(1)


def u16(m, at):
    return int.from_bytes(m.read(at, 2), "little")


def rec4(m, pseg, dm, name):
    """a furniture rect out of the guest's own bss."""
    return [u16(m, (pseg << 4) + dm[name] + i * 2) for i in range(4)]


def marn(m, pseg, dm):
    """the arena line's digits, as an int."""
    raw = m.read((pseg << 4) + dm["dos_marn"], 8).split(b"\0")[0].decode("latin-1")
    try:
        return int(raw.rstrip("KB").strip())
    except ValueError:
        fail("dos_marn reads %r and should be digits and a KB - the page has "
             "never been painted (SPEC.md 96.36.3)" % raw)


def rec(m, pseg, dm, off):
    return u16(m, (pseg << 4) + dm["dos_mrad"] + off)


def arm_centre(m, pseg, dm, i):
    """The middle of row i, out of the GUEST's own rect and pitch."""
    x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
    pitch = rec(m, pseg, dm, RD_PITCH)
    return (x1 + x2) // 2, y1 + i * pitch + pitch // 2


def band(m, x1, y1, x2, y2):
    """The pixels of a rect, as a flat tuple - for 'did anything change'."""
    return tuple(os88marty.crop_rgb(m, x1, y1, x2 - x1 + 1, y2 - y1 + 1))


def dithered(m, x, y, w, h):
    """Is this run of text a CHECKERBOARD? (SPEC.md 47 rule 3)

    A solid glyph has runs of set pixels side by side; font_ink's disabled
    mask leaves no two horizontally adjacent ones at all, because the stipple
    is laid on in 1-pixel columns.  So the question is answered by counting
    ADJACENT PAIRS rather than by counting ink, which would only say how much
    text there is.
    """
    px = os88marty.crop_rgb(m, x, y, w, h)
    pairs = ink = 0
    for row in range(h):
        prev = False
        for col in range(w):
            i = (row * w + col) * 3
            dark = px[i] < 128
            ink += dark
            pairs += dark and prev
            prev = dark
    return ink, pairs


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACH) as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open the DOS box")
        os88marty.settle(m)
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        mo = os88mouse.Mouse(marty=m)

        # --- 1: on to the Setup page -----------------------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        rect = dosmap.rect(m, pseg, dm, "dos_mrad")
        if rect == [0, 0, 0, 0]:
            fail("the radio's rect is still zero after the page painted - "
                 "dos_mrad_place never ran, or it wrote somewhere else "
                 "(SPEC.md 96.36)")
        print("dosmem: the group is at %r, pitch %d"
              % (rect, rec(m, pseg, dm, RD_PITCH)))

        # --- 2: the record says what the page is ------------------------------
        if rec(m, pseg, dm, RD_N) != NARM:
            fail("OS88UI_RD_N is %d and the page has %d arms"
                 % (rec(m, pseg, dm, RD_N), NARM))
        if rec(m, pseg, dm, RD_SEL) != INOS:
            fail("the default arm is %d and DOS_MEM_IN is %d - a fresh box "
                 "runs the program inside the OS (SPEC.md 96.25)"
                 % (rec(m, pseg, dm, RD_SEL), INOS))
        dis = rec(m, pseg, dm, RD_DIS)
        if dis & (1 << WHOLE):
            fail("OS88UI_RD_DIS is 0x%04X - the SHUT DOWN arm IS GREYED on a "
                 "shipped system disk. dos_mem_whole reads the part table's "
                 "own length word, so this means the DOS.O88 in APPS/ does "
                 "not carry kern_dos: $(SYSROOT) in the Makefile is the one "
                 "variable that decides it (SPEC.md 96.40.3, 96.36.1)" % dis)
        if dis:
            fail("OS88UI_RD_DIS is 0x%04X and nothing on this page is supposed "
                 "to be greyed at all" % dis)
        print("dosmem: N=%d SEL=%d DIS=0x%04X - every arm live, arm %d "
              "included" % (NARM, INOS, dis, WHOLE))

        # --- 3: EVERY row is SOLID, which is the pixel half of step 2 --------
        # SPEC.md 47 rules 2 and 3, asserted in pixels because that is the only
        # thing that says the two halves of the control agree - a DIS word with
        # bit 2 clear and a label still drawn in a stipple is a group that
        # disagrees with itself, and the user believes the pixels.
        x1, y1, _, _ = rect
        pitch = rec(m, pseg, dm, RD_PITCH)
        lx = x1 + 12 + 6                     # OS88UI_RDBOX + OS88UI_RDGAP
        # **THE LABEL'S y IS NOT y1 + arm*pitch + 4 ANY MORE** (SPEC.md
        # 13.17.5): a pitch of sixty is a SUBSECTION, and the library centres
        # the label in a ROW capped at OS88UI_RDROWH - so the offset inside
        # the row is (RDROWH - 7) / 2 and is the same on every pitch this page
        # could have.
        ly = (RDROWH - 7) // 2
        for arm in (INOS, WHOLE):
            ink, pairs = dithered(m, lx, y1 + arm * pitch + ly, 13 * 8, 8)
            if ink < 40:
                fail("arm %d's label has %d dark pixels - it did not draw at "
                     "all" % (arm, ink))
            if pairs * 8 < ink:           # a stipple leaves almost no pairs
                fail("arm %d's label is DITHERED: %d dark pixels in %d "
                     "adjacent pairs. The record says nothing is greyed, so "
                     "the two halves of the control disagree (SPEC.md 47 "
                     "rule 2)" % (arm, ink, pairs))
            print("dosmem: arm %d label %d px / %d pairs -> solid"
                  % (arm, ink, pairs))

        # --- 4: ARM 1'S ROW CARRIES ITS OPTION AND NOT A REASON --------------
        # One row, two things, and they never want it at once (SPEC.md
        # 96.36.8): `dos_mem_whole` answers in SI, the painter draws SI when
        # it is non-zero and draws the mouse box when it is not.  The arm is
        # live here, so the box is what is there - and this asserts it by
        # WORKING it, which is stronger than counting pixels: a reason drawn
        # at the same indent would be a sentence the user can press and
        # nothing would happen.  It also proves 96.36.4's hit order, the
        # radio's own rect covering this box.
        bx1, by1, bx2, by2 = dosmap.rect(m, pseg, dm, "dos_mmou")
        if bx2 <= bx1 or by2 <= by1:
            fail("the Disable the mouse box has an empty rect %r - "
                 "dos_mck_place owes every box one before any hit test "
                 "(SPEC.md 96.36.8)" % [bx1, by1, bx2, by2])
        # **THE FIRST PRESS PICKS THE ARM AND THE SECOND WORKS THE BOX**
        # (SPEC.md 96.36.9): the box is greyed while arm 0 is the pick, so it
        # claims nothing and the press falls through to the radio under it.
        # Asserting BOTH halves is what says the two rules agree.
        was = m.read((pseg << 4) + dm["dos_mmou"] + CK_ON, 1)[0]
        mo.click(bx1 + 4, by1 + 5)
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("a press inside arm 1's own subsection left the pick at %d. "
                 "A greyed box claims nothing, so the press is the radio's "
                 "and its rect covers the subsection (SPEC.md 96.36.4, "
                 "96.36.9)" % rec(m, pseg, dm, RD_SEL))
        if m.read((pseg << 4) + dm["dos_mmou"] + CK_ON, 1)[0] != was:
            fail("...and it ALSO toggled the box, which is a greyed control "
                 "acting on a press (SPEC.md 47 rule 2)")
        mo.click(bx1 + 4, by1 + 5)
        os88marty.settle(m)
        now = m.read((pseg << 4) + dm["dos_mmou"] + CK_ON, 1)[0]
        if now == was:
            fail("a second press inside the Disable the mouse box left it at "
                 "%d. Arm 1 is the pick now, so the box is live - and it is "
                 "hit-tested BEFORE the radio whose rect covers it (SPEC.md "
                 "96.36.4, 96.36.8)" % now)
        mo.click(bx1 + 4, by1 + 5)              # ...and back, so what follows
        os88marty.settle(m)                     # starts where it did
        mo.click(*arm_centre(m, pseg, dm, INOS))
        os88marty.settle(m)
        print("dosmem: arm 1's subsection picks its arm, then works")

        # --- 5: a press on the SHUT DOWN arm PICKS IT, and redraws two dots ---
        # This is the assertion the greying used to make from the other side,
        # and it is the stronger one: a control that is offered has to work.
        #
        # **PARK THE POINTER FIRST.** crop_rgb reads the card's RENDERED
        # framebuffer, so the arrow is in it: a capture taken before the move
        # and one taken after differ by the arrow wherever the control is.
        mo.to(*arm_centre(m, pseg, dm, WHOLE))
        os88marty.settle(m)
        before = band(m, *rect)
        mo.click(*arm_centre(m, pseg, dm, WHOLE))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("a press on arm %d left the pick at %d. The record says the "
                 "arm is live, so os88ui_radhit is reading the DIS bits "
                 "wrongly or the arm's rect is somewhere else (SPEC.md 96.36)"
                 % (WHOLE, rec(m, pseg, dm, RD_SEL)))
        if band(m, *rect) == before:
            fail("the pick moved to arm %d and NOTHING was redrawn - "
                 "os88ui_radhit owes the two dots that changed (SPEC.md "
                 "13.17.4)" % WHOLE)
        print("dosmem: arm %d picked, and the group redrew" % WHOLE)

        # --- 6: ...and back --------------------------------------------------
        mo.click(*arm_centre(m, pseg, dm, INOS))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != INOS:
            fail("a press on arm %d left the pick at %d"
                 % (INOS, rec(m, pseg, dm, RD_SEL)))
        print("dosmem: ...and back to arm %d" % INOS)

        # --- 7: CONSUMER THREE - a pick that IS honourable SURVIVES ----------
        # `dos_mem_fix` asks `dos_mem_whole` at the block's COMMIT point,
        # because [dos_keepc] can arrive from a .LNK written on another machine
        # and a greyed control refuses a CLICK and not a FILE (SPEC.md
        # 96.36.1).  Poking the word is that link with the file system left out
        # of it, and LEAVING THE PAGE is what commits the block.
        #
        # **THE ASSERTION IS THE OTHER WAY ROUND NOW AND IT IS NOT A WEAKER
        # ONE.** This build CAN honour the arm, so the demotion must NOT fire:
        # a `dos_mem_fix` that demoted anyway would size a link asking for the
        # whole machine against the arm below it and say nothing, which is the
        # same silence the greying exists to prevent. What it can no longer
        # assert is the demotion ITSELF - that needs a box whose package has no
        # part, and no shipped disk carries one.
        at = (pseg << 4) + dm["dos_keepc"]
        m.write(at, bytes((WHOLE, 0)))
        if u16(m, at) != WHOLE:
            fail("could not poke the pick")
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))       # Return
        os88marty.settle(m)
        got = u16(m, at)
        if got != WHOLE:
            fail("leaving the page with the pick at DOS_MEM_WHOLE left it at "
                 "%d. dos_mem_fix demotes when dos_mem_whole refuses, and on "
                 "this build it does not refuse - so a link asking for the "
                 "whole machine has been quietly sized against the arm below "
                 "(SPEC.md 96.36.1)" % got)
        print("dosmem: a DOS_MEM_WHOLE pick survived the page's commit")

        # ...and the CONTROL agrees with it, which is the point of the pick
        # living in the record rather than beside it (SPEC.md 96.36).
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("the record says arm %d after the commit left %d - "
                 "[dos_keepc] is OS88UI_RD_SEL's low byte and there is not "
                 "supposed to be a second copy" % (rec(m, pseg, dm, RD_SEL),
                                                   WHOLE))
        print("dosmem: ...and the control came back showing it")

        # --- ...AND A DIAL PICK MOVES THE FIGURE ABOVE IT (SPEC.md 96.36.6.2)
        # The one control on this page whose consequence is a NUMBER somewhere
        # else, so it is the one that can be wired up wrong and still look
        # right: os88ui_drbox repaints the caption on the same path, so the box
        # said `32K` while the line kept the value it had before the press.
        #
        # IT IS DRIVEN BY REAL CLICKS AND IT HAS TO BE.  Poking [dos_cache] and
        # re-entering the page repaints everything and passes on the broken
        # build - which is exactly how this shipped, and how a first attempt at
        # measuring it reported the arithmetic as fine.  The bug is in the
        # ANSWER os88ui_drpress gives (CF=1 is the refused save-under, AH=1 is
        # any spent press, and only AL says a pick), so nothing but a press
        # through the control can see it.
        rows = []
        for ca in (CA_32, CA_18, CA_9, CA_OFF):
            d = rec4(m, pseg, dm, "dos_mdr")
            mo.click(d[0] + 8, (d[1] + d[3]) // 2)               # open
            os88marty.settle(m)
            if not m.read((pseg << 4) + dm["dos_mdr"] + DR_OPEN, 1)[0]:
                fail("the disk cache list did not open (SPEC.md 96.36.6)")
            top = u16(m, (pseg << 4) + dm["dos_mdr"] + DR_TOP)
            mo.click(d[0] + 8, top + ca * DRIH + DRIH // 2)      # ...and pick
            os88marty.settle(m)
            sel = m.read((pseg << 4) + dm["dos_cache"], 1)[0]
            if sel != ca:
                fail("clicking row %d of the disk cache list left [dos_cache] "
                     "at %d" % (ca, sel))
            rows.append((ca, marn(m, pseg, dm)))
            print("dosmem: cache row %d -> the line reads %sK" % rows[-1][::1])
        seen = [kb for _, kb in rows]
        if len(set(seen)) != len(seen):
            fail("the four cache rows put %r on the line - a PICK is AL and "
                 "only AL (SPEC.md 13.14.1), so a handler testing CF or AH "
                 "reads one as an open and leaves the figure STANDING while "
                 "the box's own caption changes underneath it. That is what "
                 "the field reported as `changing the disk cache does not "
                 "change the estimate`" % (seen,))
        if not (seen[0] < seen[1] < seen[2] < seen[3]):
            fail("the cache rows read %r and a wider cache must leave the "
                 "program LESS: 32K < 18K < 9K < Off (SPEC.md 96.36.6)"
                 % (seen,))

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosmem.png", wd, ht, data)
        print("dosmem: build/dosmem.png written")

    print("dosmem: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
