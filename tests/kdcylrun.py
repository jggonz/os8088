#!/usr/bin/env python3
"""kern_dos does not cross a HEAD it was never given leave to cross (96.44.14).

    make && make kdostest && python3 tests/kdcylrun.py

SPEC.md 18.93.1 settles ONCE whether this machine's FDC may carry a transfer
run onto the other head: the loader crosses one on purpose, a canary verifies
the bytes came back, and `boot_cylrun` is written on that path and no other.
`dsk_geom_check` reads it at EVERY MOUNT - `cmp word [boot_cylrun], 0` - and
sets `[dsk_cylrun]`.

`kern_dos` has no loader and no canary, so it cannot ask that question and
nothing over there writes the cell.  It was declared `resb 1` - ONE BYTE - and
`kd_top`, the bump allocator's ceiling, was declared next.  The word read was
`boot_cylrun` plus `kd_top`'s low byte, which is 0xC0 once the read-ahead has
been claimed, so **every mount under kern_dos turned head crossing ON, on
every machine, with nothing behind it**.  On a BIOS that will not cross a head
- MR BIOS 286, measured in docs/FIELD-NOTES.md 31 - that ROM answers CF=0 for
the whole request and transfers the first half only, so the back half of every
crossing run is left as whatever was in the buffer.  Silent, deterministic and
reported from the field as Prince of Persia asking for its own disk.

WHAT IT ASSERTS, and why it is three things and not one:

  1. the WORD at `boot_cylrun` is 0 or 1 and nothing else - which is the
     defect itself, because the broken build reads 0xC000 there;
  2. `[dsk_cylrun]` AGREES with that word, which is `dsk_geom_check` having
     run at all;
  3. and it MATCHES the kernel's own `boot_cylrun` on the machine that handed
     over (96.44.14.1) - the carry, without which the honest answer costs
     18.91.1's cylinder run on every machine that earned it.

Check 3 is what a plain `resw 1` would fail: it reads 0 where the kernel found
1.  Check 1 is what the SHIPPED defect fails.  Neither can stand in for the
other, which is why both are here.

VERIFIED TO FAIL: `boot_cylrun: resb 1` in kerndos/kdshim.inc takes check 1
red with WORD=0xC000 and `dsk_cylrun`=1; taking the `KDL_CYLRUN` store out of
`hbm_dosrun` takes check 3 red with 0 against the kernel's 1.

MartyPC, and it must be: the subject is a real 8088 running a DOS program with
the operating system gone, and the cells are read out of `kern_dos`'s own
segment while the program is up.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402
from kdcwd import kdsyms                                       # noqa: E402
from kdhand import (alert_button, alert_up, rec, wait_text,    # noqa: E402
                    RD_DIS, RD_PITCH, RD_SEL, WHOLE)

SYS = "build/os8088-360.img"
COM = "build/doscom360.img"
MACH = "os8088_5150_cga_gla"
KD_SEG = 0x0060
WANT = ("boot_cylrun", "dsk_cylrun", "kd_top")


def fail(msg):
    print("kdcylrun: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, COM):
        if not os.path.exists(p):
            fail("%s is missing - `make` and `make kdostest`" % p)
    syms = kdsyms(WANT)

    with os88ui.boot(SYS, apps=COM, machine=MACH) as ui:
        m = ui.m

        # --- the KERNEL's own finding, before the machine is handed over -----
        # It is a WORD at 0060:0004 here and the loader writes it; this is the
        # value check 3 compares against, and reading it on THIS machine is
        # what makes the row work on a ROM that crosses and on one that does
        # not, without either being hardcoded.
        kern = int.from_bytes(m.read(os88sym.linear("boot_cylrun"), 2),
                              "little")
        print("kdcylrun: the kernel's boot_cylrun is %04X (%s)"
              % (kern, "crossed and verified" if kern else "cautious"))

        if not ui.path("B:/DOSHELLO.COM"):
            fail("double-clicking DOSHELLO.COM opened no window")
        wait_text(m, "READY", what="the windowed run")
        m.type_text("x")
        os88marty.settle(m)

        dm = dosmap.package()
        pseg = dosmap.instance(m)
        base = pseg << 4
        mo = os88mouse.Mouse(marty=m)

        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_DIS) & (1 << WHOLE):
            fail("the Shut down the OS arm is greyed - this build carries no kern_dos "
                 "part, so there is nothing to measure (SPEC.md 96.36.1)")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = rec(m, pseg, dm, RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("clicking the Shut down the OS arm left OS88UI_RD_SEL at %d"
                 % rec(m, pseg, dm, RD_SEL))

        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if not alert_up(m, base, dm):
            fail("Run on the Shut down the OS arm asked nothing (SPEC.md 96.42)")
        mo.click(*alert_button(m, base, dm, 1))         # Proceed
        wait_text(m, "READY", secs=150, what="the run under kern_dos")

        # **THE PROGRAM IS UP, SO A MOUNT HAS HAPPENED** - which is the whole
        # precondition: dsk_geom_check is what reads boot_cylrun, and it runs
        # at a mount and nowhere else.
        def rd(n, w=2):
            b = m.read((KD_SEG << 4) + syms[n], 2)
            return (b[0] | (b[1] << 8)) if w == 2 else b[0]

        word, cyl, top = rd("boot_cylrun"), rd("dsk_cylrun", 1), rd("kd_top")
        print("kdcylrun: under kern_dos, WORD@boot_cylrun=%04X  dsk_cylrun=%02X"
              "  kd_top=%04X" % (word, cyl, top))

        if word not in (0, 1):
            fail("the WORD at kern_dos's boot_cylrun is %04X, which is neither "
                 "0 nor 1. kd_top is %04X - if the low byte of that is the high "
                 "byte of this, the cell is a `resb 1` being read by a "
                 "`cmp word` again (SPEC.md 96.44.14)" % (word, top))
        if bool(cyl) != bool(word):
            fail("[dsk_cylrun] is %d and the cell it comes from is %04X. "
                 "dsk_geom_check reads one from the other at every mount, so "
                 "they cannot disagree with a volume mounted" % (cyl, word))
        if word != (1 if kern else 0):
            fail("the kernel found %04X and kern_dos has %04X. The launch "
                 "block carries KDL_CYLRUN for exactly this (SPEC.md "
                 "96.44.14.1): without it kern_dos gives up 18.91.1's cylinder "
                 "run on every machine that earned it, and with it WRONG it "
                 "crosses a head on a BIOS that cannot" % (kern, word))
        print("kdcylrun: ok - the kernel's finding crossed, and %s"
              % ("a run may cross a head" if word
                 else "every run is bounded at the track"))


if __name__ == "__main__":
    main()
