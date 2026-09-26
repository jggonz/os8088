#!/usr/bin/env python3
"""THE MEMORY PAGE'S FIGURE IS THE FIGURE THE PROGRAM GETS (SPEC.md 96.36.3).

    python3 tests/dosram.py [--machine os8088_5150_cga_gla]

`For the program: ~NNNNN K` is one number now, live, that every control under
it moves.  Two of its terms can be checked against the machine and the third
cannot be checked against anything else at all:

  1  **ARM 0 IS EXACT** - the base is `OSAPI_MEM_COMPACT`'s what-if at the
     floor `dos_run` will really set, so a launch on that arm must hand out
     what the row promised.  That half is `tests/dirwshed.py`'s and is not
     re-driven here;
  2  **THE DRIVER BOXES MOVE IT BY WHAT THEY SAY** (96.36.7).  The figure in
     a box's label and the figure the total moves by are ONE word read once,
     so clearing a live box must move the row by exactly what its own label
     printed.  A box that is greyed says 0 and must move it by nothing;
  3  **ARM 1 CANNOT BE ASKED ANYTHING** (96.36.3).  The machine it describes
     has no kernel in it, so every term is a constant this build knows -
     `DOS_KDKB` most of all, which is deliberately NOT gated against
     `kern_dos`'s own arithmetic because that moves with its image.  **THIS
     ROW IS THAT GATE**: put a program through arm 3 and compare what the
     page promised against what the PROGRAM says it was given.

What it would catch, and every one is a silent failure:

  - `DOS_KDKB` drifting as `kern_dos` grows      -> the page promises memory
                                                    the program does not get,
                                                    and nothing says so
  - the dial's ladder disagreeing with kern_dos  -> `DOS_CA_AUTORUN` is gated
    (96.36.6)                                       at assembly, but 32K/18K/
                                                    9K are only checked here
  - a box's label and the total disagreeing      -> the label is the CLASS's
                                                    constant ceiling and the
                                                    total is the live call
                                                    (96.36.7.1, 51.12.1)

It needs a machine with a fixed disk for arm 3 (the session has to have
somewhere to go), and the figure it compares against is **DOSHELLO's own
`Memory to top of block`** - `int 21h AH=4Ah`'s answer for its PSP, which is
the arena `dos_build_psp` handed it.  The BDA mailbox (96.41.1) carries the
same number and was the first shape of this row: it is written by `kd_bda` at
`kd_leave`, so reading it means exiting the program AND letting the live
resume run, which is `tests/kdreturn.py`'s subject and a great deal of
machinery for a figure the program is already printing.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

TEMPLATE = os.path.join(ROOT, "build/martypc/run/media/hdds/default_xtide.vhd")
KERNEL = os.path.join(ROOT, "build", "kernel.sys")
VHD = os.path.join(ROOT, "build", "dosram.vhd")
FLOPPY = os.path.join(ROOT, "build", "dosram360.img")
PROG = "DOSHELLO.COM"
# ...resolved through os88marty.machine(), NOT named bare: the ROM
# `os8088_5150_cga_hdd` asks for is IBM's and is not in this tree
# (CONTRIBUTING.md 6), so MartyPC's pinned build exits at once with `ROM set
# ibm5150_82_v4 not found` - which is how this row failed the soak, before its
# first assertion. machine() answers the GLaBIOS twin always, so the row
# behaves the same on a contributor's box and on one with the ROM staged.
MACHINE = M.machine("os8088_5150_cga_hdd")

CK_ON = 10                              # OS88UI_CK_ON
HDD_CFGBIT = 1                          # kernel/driver.inc's drv_cfgbit, row 1
SLACK = 24                              # KB.  Arm 1's terms are ESTIMATES and
                                        # the row is about DRIFT, not about
                                        # arithmetic: a kern_dos that grew a
                                        # kilobyte is fine and one that grew
                                        # twenty-four is a figure nobody
                                        # re-measured


def fail(msg):
    print("dosram: FAIL: %s" % msg)
    sys.exit(1)


def fixture():
    for p in (TEMPLATE, KERNEL, os.path.join(ROOT, "build/kdos/DOS.O88")):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the DOS pieces and "
                 "`make marty` the template" % p)
    prog = os.path.join(ROOT, "build", PROG)
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", KERNEL, "--vbr", "build/boothd.bin",
         "--mbr", "build/mbr.bin",
         "--file", "HIBER.DRV=build/hiber.drv",
         "--file", "CTRL.DRV=build/ctrl.drv",
         "--file", "HDD.DRV=build/hdd.drv",
         "--file", "DOS.O88=build/kdos/DOS.O88",
         "--file", "%s=%s" % (PROG, prog),
         "--file", "SYSTEM.CFG=" + syscfg()], cwd=ROOT)
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", FLOPPY, "--size", "360", prog],
        cwd=ROOT)


def syscfg():
    """...and the file that ASKS for HDD.DRV (SPEC.md 51.3).

    **NOTHING LOADS UNLESS SYSTEM.CFG ASKS**, and this row would pass its own
    first assertion without noticing: the machine boots off the fixed disk
    either way, because the boot partition is a DVK_BIOS row served by
    `int 13h` and not by the driver (SPEC.md 18.7.1).  So C: is there, the
    page opens, and `OSAPI_DRV_CLASSK` answers 0 for a class that really is
    holding nothing - a true answer about a machine this row did not mean to
    build.  kdreturn's own eighteen bytes: the signature, the generation, one
    DW record and the terminator, every other key ABSENT so the reader answers
    each with its default (51.5 rule 3).
    """
    p = os.path.join(ROOT, "build", "dosram-cfg-%d.bin" % os.getpid())
    with open(p, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                + b"DW" + bytes([1, 2])
                + (1 << HDD_CFGBIT).to_bytes(2, "little") + b"\0\0")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()
    fixture()

    m = M.launch(None, apps=FLOPPY, machine=a.machine,
                 extra=["--mount", "hd:0:" + VHD])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        mo = os88mouse.Mouse(marty=m)
        # **OPEN THE PROGRAM, NOT THE BOX** (SPEC.md 54): a double click on a
        # .COM opens a DOS window already pointed at it, which is what every
        # kd* row does - typing a path into the arguments field opens nothing
        # and the Run below then has no program.
        if not ui.path("C:/" + PROG):
            fail("double-clicking %s on the fixed disk opened no window"
                 % PROG)
        M.settle(m)
        dm = dosmap.package()
        ps = dosmap.instance(m)

        def pb():
            return dosmap.instance(m) << 4

        def byte(n):
            return m.read(pb() + dm[n], 1)[0]

        def word(n):
            return int.from_bytes(m.read(pb() + dm[n], 2), "little")

        def rect(n):
            b = pb() + dm[n]
            return [int.from_bytes(m.read(b + 2 * i, 2), "little")
                    for i in range(4)]

        def arena():
            raw = m.read(pb() + dm["dos_marn"], 8).split(b"\0")[0]
            try:
                return int(raw.decode("latin-1").rstrip("KB").strip())
            except ValueError:
                fail("the arena row reads %r and should be digits and a KB - "
                     "the page has never been painted (SPEC.md 96.36.3)" % raw)

        # **AN ASSOCIATION OPEN OF A `.COM` RUNS IT** (SPEC.md 54), so the box
        # arrives inside its own fsx bracket with the program's output on the
        # screen - no window, no page, and every read below would be of a box
        # that has not painted one.  DOSHELLO waits on AH=08h so its screen
        # can be read; a key dismisses it and leaves the box with the program
        # still NAMED, which is exactly the state arm 3 wants.
        def inbr(_=None):
            return m.read(pb() + dm["dos_inbr"], 1)[0]

        M.until(m, inbr, "%s to be running" % PROG, limit=120.0, guest=240.0)
        m.type_text(" ")
        M.until(m, lambda _=None: not inbr(), "%s to exit" % PROG, limit=120.0)
        M.settle(m)

        mo.click(*dosmap.centre(m, ps, dm, "dos_erect"))
        M.settle(m)
        if byte("dos_page") != dm["DOS_PAGE_SET"]:
            fail("the bar's button left [dos_page] = %d and Setup is %d"
                 % (byte("dos_page"), dm["DOS_PAGE_SET"]))

        # --- 1: a LIVE box moves the row by what its own label says ----------
        # The hard disk is mounted here, so DRVC_DISK has a figure and the box
        # is live.  Clearing it is the whole of 96.36.7: what the label prints
        # and what the total moves by are one word read once.
        hdkb = word("dos_mhkb")
        print("dosram: DRVC_DISK holds %d KB, DRVC_NET %d KB"
              % (hdkb, word("dos_mnkb")))
        if not hdkb:
            fail("OSAPI_DRV_CLASSK says DRVC_DISK is holding nothing on a "
                 "machine that BOOTED off the fixed disk. This fixture's "
                 "SYSTEM.CFG asks for HDD.DRV, so either the driver is not "
                 "mounted or OSAPI_DRV_CLASSK's cell is answering wrongly "
                 "(SPEC.md 51.12)")
        before = arena()
        r = rect("dos_mhdd")
        mo.click(r[0] + 4, r[1] + 5)
        M.settle(m)
        if m.read(pb() + dm["dos_mhdd"] + CK_ON, 1)[0]:
            fail("a press inside the Hard drives box did not clear it - the "
                 "box is live on this machine (SPEC.md 96.36.7)")
        after = arena()
        if after - before != hdkb:
            fail("clearing `Hard drives (%d K)` moved the arena %d -> %d, a "
                 "step of %d. The label's figure and the total's are supposed "
                 "to be ONE word read once (SPEC.md 96.36.7, 47 rule 5)"
                 % (hdkb, before, after, after - before))
        print("dosram: clearing the box moved the row %d -> %d, exactly its "
              "own %d K" % (before, after, hdkb))
        mo.click(r[0] + 4, r[1] + 5)            # ...and back
        M.settle(m)
        if arena() != before:
            fail("re-ticking the box left the arena at %d and it was %d"
                 % (arena(), before))

        # --- 1b: the CAPTION is the class ceiling, not this machine ----------
        # This fixture has a hard disk and NO network card, which is exactly
        # the discriminator: DRVC_NET's live figure is 0 and its caption must
        # not be.  Fed the live figure - which is what shipped - the row read
        # `Network (Up to  0K)`: true of this machine, meaningless as a
        # description of what the box would cost on the machine the .LNK is
        # carried to, and indistinguishable from a page whose arithmetic died
        # (SPEC.md 51.12.1, 96.36.7.1).
        def caption(n):
            raw = m.read(pb() + dm[n], 48).split(b"\0")[0].decode("latin-1")
            d = "".join(c for c in raw if c.isdigit())
            if not d:
                fail("the %s caption reads %r and carries no figure at all"
                     % (n, raw))
            return int(d), raw

        netkb, netraw = caption("dos_l_mnet")
        hddkb, hddraw = caption("dos_l_mhdd")
        print("dosram: captions %r / %r against live mnkb=%d mhkb=%d"
              % (hddraw, netraw, word("dos_mnkb"), hdkb))
        if word("dos_mnkb"):
            fail("this fixture is supposed to have NO network card, and "
                 "OSAPI_DRV_CLASSK says DRVC_NET holds %d KB. The check below "
                 "cannot tell a ceiling from a live figure on a machine where "
                 "they are the same" % word("dos_mnkb"))
        if not netkb:
            fail("`%s` prints a ZERO on a machine with no card. The caption is "
                 "the CLASS's CEILING - DRVM_CEIL_NET, a build-time constant "
                 "out of the SDK (SPEC.md 51.12.1); fed OSAPI_DRV_CLASSK's "
                 "live figure it reports what is mounted, which here is "
                 "nothing" % netraw)
        # A CEILING IS NEVER BELOW WHAT IS HELD, and on this fixture it is
        # strictly above: DRVM_HDD counts HD_MAXVOL listing claims and one
        # volume is mounted, so 32 against 14. That gap IS the two being two
        # questions (SPEC.md 51.12.1, 51.12.2) - fed one figure they were
        # equal here, which is exactly why the equality used to be asserted.
        # The caption is a CONSTANT now and the live figure is a call, so
        # this row is also what says the constant reached the label at all.
        if hddkb < hdkb:
            fail("`%s` says %d and the mounted class is HOLDING %d. A ceiling "
                 "below what is held is one of the two forms walking the "
                 "wrong rows (SPEC.md 51.12.1)" % (hddraw, hddkb, hdkb))
        # **THE CAPTION IS CHECKED AGAINST THE CONSTANT, NOT AGAINST THE
        # LIVE FIGURE.** This used to assert that the two DIFFER, on the
        # ground that DRVM_HDD counted HD_MAXVOL's listing claims while the
        # live call weighs the heap - 32 against 14. That gap is GONE:
        # SPEC.md 52's per-partition listing claim was retired as buying
        # nothing, so `DRVM_HDD equ DRVM_IMG_HDD` and the ceiling is the image
        # alone. With one volume mounted and nothing else on the heap the two
        # are now legitimately EQUAL, and the row failed the kernel for it.
        #
        # An inequality was only ever a PROXY for the question, which is
        # whether the label was filled from the build-time constant or from
        # OSAPI_DRV_CLASSK. The NET half above already asks that directly - no
        # card, so a live figure would print 0 - and this is the same question
        # asked the same way: the caption must BE DRVM_CEIL_DISK, scraped out
        # of the SDK the package would read it from. That is strictly stronger
        # than "they differ", and it cannot rot with the fixture.
        want = dosmap.sdk_const("DRVM_CEIL_DISK")
        if hddkb != want:
            fail("`%s` says %d and the SDK's DRVM_CEIL_DISK is %d. The "
                 "caption is the CLASS's CEILING, a build-time constant "
                 "(SPEC.md 51.12.1); any other number means the label was "
                 "filled from OSAPI_DRV_CLASSK's live figure instead "
                 "(51.12.2)" % (hddraw, hddkb, want))

        # --- 1c: ...and the LIMIT moves the figure AS IT IS TYPED ------------
        # dos_mem_arena has clamped to [dos_memkb] since the page was reworked
        # and it was never seen to work, because the parse ran only at
        # dos_mem_take - whose callers are all leaving the page (SPEC.md
        # 96.36.10).  Poking [dos_memkb] and repainting would pass on that
        # build; only the keystroke tells them apart.
        lim = before - 11
        fr = rect("dos_mln")
        mo.click((fr[0] + fr[2]) // 2, (fr[1] + fr[3]) // 2)
        M.settle(m)
        for ch in str(lim):
            m.type_text(ch)
        M.settle(m)
        if word("dos_memkb") != lim:
            fail("typing %d into the limit field left [dos_memkb] = %d - the "
                 "keystroke never reached dos_mem_parse (SPEC.md 96.36.10)"
                 % (lim, word("dos_memkb")))
        if arena() != lim:
            fail("the limit is %d and the arena row still reads %d. The clamp "
                 "is in dos_mem_arena's `.cap` and the row is redrawn by "
                 "dos_mem_row; a figure that only catches up when the page is "
                 "left is a control the user cannot see working "
                 "(SPEC.md 96.36.10)" % (lim, arena()))
        print("dosram: typing a %d K limit took the row %d -> %d, live"
              % (lim, before, arena()))
        for _ in str(lim):
            m.type_text("\b")
        M.settle(m)
        if word("dos_memkb") or arena() != before:
            fail("clearing the limit left [dos_memkb] = %d and the row at %d, "
                 "where an empty field means NO cap and the row should be "
                 "back at %d" % (word("dos_memkb"), arena(), before))
        for ch in str(lim):                     # ...and set again, STANDING,
            m.type_text(ch)                     # for 2a below
        M.settle(m)

        # --- 2: ARM 1's estimate, against what the machine really hands over --
        rr = rect("dos_mrad")
        pitch = int.from_bytes(m.read(pb() + dm["dos_mrad"] + 14, 2), "little")
        mo.click((rr[0] + rr[2]) // 2, rr[1] + pitch + pitch // 2)
        M.settle(m)
        if byte("dos_keepc") != 1:
            fail("clicking the Shut down the OS arm left the pick at %d"
                 % byte("dos_keepc"))

        # --- 2a: ...and the LIMIT greys with its arm, and is ignored ---------
        # It is arm 0's control exactly as the two boxes above it are, and the
        # click path always knew that - dos_click_mem's `.notck` tests
        # [dos_keepc] before it asks the field.  The DRAWING did not, so the
        # field sat there black, framed and typeable on an arm whose launch
        # never reads [dos_memkb] (SPEC.md 96.36.10.2).
        LN_FOCUS, LN_DIS = 18, 19
        if not m.read(pb() + dm["dos_mln"] + LN_DIS, 1)[0]:
            fail("the arm is `Shut down the OS` and the limit field's LN_DIS "
                 "is 0 - it is drawn live, framed in black and typeable, for "
                 "a launch that never reads it (SPEC.md 96.36.10.2)")
        if m.read(pb() + dm["dos_mln"] + LN_FOCUS, 1)[0]:
            fail("the limit field still holds the caret on an arm it does not "
                 "belong to - a greyed box with a bar blinking in it is "
                 "offering something (SPEC.md 47 rule 6)")
        promised = arena()
        if promised == word("dos_memkb"):
            fail("a %d K limit is standing and arm 1's estimate reads the "
                 "same. The cap is arm 0's: dos_lbfill fills KDL_CAP from the "
                 "BDA and never from [dos_memkb], so a figure clamped here "
                 "would be a promise the launch does not keep (SPEC.md "
                 "96.36.9)" % word("dos_memkb"))

        # ...and a press on it belongs to the ARM under it, not to the field
        fr = rect("dos_mln")
        mo.click((fr[0] + fr[2]) // 2, (fr[1] + fr[3]) // 2)
        M.settle(m)
        if m.read(pb() + dm["dos_mln"] + LN_FOCUS, 1)[0]:
            fail("a press on the greyed limit field took the caret - a greyed "
                 "control says nothing more, and the press belongs to the arm "
                 "it is standing in (SPEC.md 47 rule 6, 96.36.4)")
        if byte("dos_keepc") != 0:
            fail("a press on the greyed limit field left the pick at %d - it "
                 "should have fallen through to the radio and picked arm 0, "
                 "the way a press on a greyed check box does (SPEC.md 96.36.4)"
                 % byte("dos_keepc"))
        mo.click((rr[0] + rr[2]) // 2, rr[1] + pitch + pitch // 2)   # back
        M.settle(m)
        if byte("dos_keepc") != 1:
            fail("could not get back to arm 1 after the fall-through test")
        print("dosram: the limit greys with its arm, keeps no caret, and "
              "arm 1 promises %d K with a %d K cap standing"
              % (arena(), word("dos_memkb")))
        promised = arena()
        print("dosram: the page promises ~%d K on arm 1" % promised)

        mo.click(*dosmap.centre(m, ps, dm, "dos_trect"))         # Return
        M.settle(m)
        mo.click(*dosmap.centre(m, ps, dm, "dos_rrect"))         # ...and Run

        # The handoff tears the whole machine down and `kern_dos` boots in its
        # place, so what is on the screen afterwards is the PROGRAM's own
        # output on real text VRAM - no window, no instance, and `dos_inbr`
        # is in a segment that no longer exists.  DOSHELLO prints what
        # `int 21h AH=4Ah` says its block reaches, which IS the arena.
        want = "Memory to top of block:"

        def said(_=None):
            try:
                return any(want in r for r in (m.screen() or []))
            except Exception:                                  # noqa: BLE001
                return False                                   # mid-teardown

        M.until(m, said, "the program to run under kern_dos",
                limit=300.0, guest=900.0)
        got = None
        for r in (m.screen() or []):
            if want in r:
                got = int(r.split(want, 1)[1].strip().split()[0])
                break
        print("dosram: the machine handed the program %d K" % got)
        if not got:
            fail("the program reports an arena of 0 - kern_dos never got as "
                 "far as dos_build_psp, so there is nothing here about memory")
        if abs(got - promised) > SLACK:
            fail("the page promised ~%d K on arm 1 and the machine handed out "
                 "%d K, a drift of %d against a slack of %d. **DOS_KDKB IS "
                 "NOT GATED AT ASSEMBLY ON PURPOSE** (SPEC.md 96.36.3): "
                 "kern_dos's floor moves with its own image, so a mirror "
                 "would fail this build every time that image changed a byte "
                 "and would be raised rather than read. THIS ROW IS THE GATE, "
                 "and the fix is to re-measure DOS_KDKB in apps/dos/dos.asm "
                 "against the figure above"
                 % (promised, got, got - promised, SLACK))
        print("dosram: ok - promised ~%d K, got %d K (slack %d)"
              % (promised, got, SLACK))
        m.type_text(" ")                # ...and let kd_leave put the session
                                        # back, so the machine is not left
                                        # holding the screen
    finally:
        m.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
