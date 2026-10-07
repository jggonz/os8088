#!/usr/bin/env python3
"""A 1.44MB floppy in B: under kern_dos (SPEC.md 96.40.5).

    make kdostest && python3 tests/kdmix.py

**THIS ROW EXISTS BECAUSE EVERY OTHER kd* ROW BOOTS TWO 360KB DRIVES.** Not
by choice - every machine in `tools/martypc/configs/os8088_machines.toml` had
two drives of ONE type, so the one geometry `kern_dos` could mount was the
only one under test, and it shipped able to mount nothing else. The field
found it in a day: a 286 with a 5.25" A: and a 3.5" B: could not run a
program off B: whatever disk was in it, and the refusal said only that the
disk could not be mounted.

The cause is one constant. `kerndos/kdshim.inc` carried `DSK_FAT_SECS equ 2`
- kern_small's, taken when the shim was cut out of `kernel/kernel.asm` and
never re-argued - and mount rule 10 (SPEC.md 18.2) refuses a FLOPPY whose
declared `FATSz16` is over it. A FAT12 floppy's FAT is a property of the
geometry: 2 sectors at 360KB, 3 at 720KB, 7 at 1.2MB and **9 at 1.44MB**.

**AND `--fatcap` IS WHY THE PACKAGE HALF NEVER SAW IT.** `kern_small` mounts
disks this project formats, and `tools/os88disk.py --fatcap 2` gives its
1.44MB floppies 4KB clusters and a 2-sector FAT so every geometry stays
available to it. `kern_dos` mounts disks DOS formatted. `build/doscom144.img`
is deliberately built with NO `--fatcap`, so its FAT is the nine sectors a
real DOS writes - which is the whole of what this row is about.

VERIFIED TO FAIL: with `DSK_FAT_SECS equ 2` restored in `kerndos/kdshim.inc`
this row reads `kern_dos: could not mount drive B` off the text screen and
says so, where the 360KB rows all still pass.

It runs on MartyPC and on `os8088_5150_cga_gla_mix`, which is the mixed pair
- 360KB in A:, 1.44MB in B: - and is the only machine in the tree with two
drives of different types.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402
import kdhand                                                  # noqa: E402

SYS = "build/os8088-360.img"
BIG = "build/doscom144.img"
MACH = "os8088_5150_cga_gla_mix"


def fail(msg):
    print("kdmix: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, BIG):
        if not os.path.exists(p):
            fail("%s is missing - run `make kdostest`" % p)

    with os88ui.boot(SYS, apps=BIG, machine=MACH) as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("could not open DOS.O88 off the system disk")
        os88marty.settle(m)

        dm = dosmap.package()
        pseg = dosmap.instance(m)
        base = pseg << 4
        mo = os88mouse.Mouse(marty=m)

        # --- 1. B: IN THE WINDOW FIRST, which is the control -----------------
        # The package reaches B: through the kernel's own mounted volume, so
        # this half passing and the kern_dos half failing is exactly the shape
        # the field reported: the same disk, readable by one and not the other.
        m.type_text("B:\n")
        os88marty.settle(m)
        vol = m.read(base + dm["dos_vol"], 1)[0]
        if vol != 1:
            fail("typing B: left [dos_vol] at %d - the WINDOW cannot reach "
                 "the 1.44MB disk either, so this is not 96.40.5's defect "
                 "but the kernel's own mount" % vol)
        print("kdmix: the window reached B: - a 1.44MB floppy, FAT 9 sectors")

        # --- 2. the Shut down the OS arm ------------------------------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        dis = kdhand.rec(m, pseg, dm, kdhand.RD_DIS)
        if dis & (1 << kdhand.WHOLE):
            fail("the Shut down the OS arm is GREYED - this build does not carry "
                 "kern_dos as a part (SPEC.md 96.36.1)")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = kdhand.rec(m, pseg, dm, kdhand.RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + kdhand.WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if kdhand.rec(m, pseg, dm, kdhand.RD_SEL) != kdhand.WHOLE:
            fail("clicking the Shut down the OS arm did not pick it")

        # --- 3. name the program and go --------------------------------------
        # **THE PATH BOX HAS TO BE CLICKED FIRST**, and the `B:` above is why:
        # a fresh window opens with the box focused, Enter RUNS what is in it,
        # and a bare drive letter changes drive and CLEARS it - so by the time
        # the Memory page has been visited and left, nothing has focus and
        # `type_text` goes nowhere at all. The failure is silent and lands two
        # steps later: Run with an empty path does nothing, and the row reports
        # "never reached READY" about a machine that never left the desktop.
        # **AND THE EXTENSION IS PART OF THE NAME**: the box does not append
        # `.COM`, so `DOSHELLO` gets as far as kern_dos and comes back "the
        # program could not be loaded" - which reads exactly like the mount
        # defect this row is about and is not it.
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_pln"))
        os88marty.settle(m)
        m.type_text("DOSHELLO.COM")
        os88marty.settle(m)
        ln = base + dm["dos_pln"]
        if int.from_bytes(m.read(ln + 12, 2), "little") != 12:
            fail("the path box did not take the program's name - LN_LEN is %d"
                 % int.from_bytes(m.read(ln + 12, 2), "little"))
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if kdhand.alert_up(m, base, dm):
            mo.click(*kdhand.alert_button(m, base, dm, 1))    # Proceed

        # --- 4. AND THE ASSERTION, which is the mount ------------------------
        seen = {"rs": []}

        def ended(_):
            seen["rs"] = [r.rstrip() for r in (m.screen() or [])]
            return any("could not mount" in r or "READY" in r
                       for r in seen["rs"])
        try:                            # a GUEST-time budget
            os88marty.until(m, ended, "the program to mount and reach READY",
                            poll=0.25, limit=150.0)
        except os88marty.MartyError:
            fail("the program never reached READY under kern_dos. The last "
                 "screen was %r"
                 % [r for r in (m.screen() or []) if r.strip()][:10])
        rs = seen["rs"]
        if any("could not mount" in r for r in rs):
            fail("kern_dos refused the 1.44MB disk: %r. That is "
                 "SPEC.md 96.40.5 - `DSK_FAT_SECS` in kerndos/kdshim.inc "
                 "is below the NINE sectors a 1.44MB FAT12 floppy "
                 "declares, so mount rule 10 turns every disk bigger "
                 "than 360KB away"
                 % [r for r in rs if "could not mount" in r])

        rs = [r.rstrip() for r in (m.screen() or [])]
        for want in ("os8088 DOS gate", "DOS version 3.30"):
            if not any(want in r for r in rs):
                fail("%r is not on the screen - the program did not run" % want)
        kb = kdhand.topmem(rs, "the run under kern_dos")
        print("kdmix: ok - a .COM on a 1.44MB floppy ran under kern_dos "
              "with %d KB above its PSP" % kb)


if __name__ == "__main__":
    main()
