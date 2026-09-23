#!/usr/bin/env python3
"""SPEC.md 96.45.2: the POINTER is alive after a LIVE resume from kern_dos.

    make kdostest && make marty && python3 tests/mouresume.py

`kd_mou_stop` gives the serial port back QUIET on the way into the box - IER = 0
and the line masked - and restored nothing, on the ground that *"the live
restore['s] os8088 runs `mouse_init` again on the way up"*.  It does not.
`mouse_init` is in `.ovlw`, which `mem_unblob` handed back at the end of
`kmain`, and SPEC.md 96.49's live resume re-enters at `hbm_wake` rather than at
a boot - so the one site in this tree that writes IER = 1 could not run, and the
IER = 0 stood for the rest of the session.

**IT PRESENTS AS A DEAD POINTER ON A PERFECTLY WORKING MACHINE**, which is what
made it hard to place: the 8259 mask comes back (`kdresume.inc` samples
`HS_MASKM` BEFORE `kd_mou_stop` masks the line), the IVT is the image's so
`mou_isr` is vectored right, and `kern_dos` never touches LCR or the divisor so
the line settings survive.  Only the UART's own enable is missing, and none of
it is the keyboard's - the reporter could type in the DOS window throughout.

WHY NOTHING CAUGHT IT.  `tests/kdreturn.py` drives this exact resume and
passes; it asserts the program ran, the exit code came home and the mailbox was
cleared, and **nothing in the suite looked at the pointer after a return**.
`tests/kdmouse.py` is about the mouse INSIDE the box.  This row is that gap.

FOUR READINGS, and the third is what stops the other three passing vacuously:

  1. **The pointer works BEFORE the handoff.**  IER reads 01 and a move lands.
     The apparatus check: if the mouse is not alive here, nothing below is
     about the return.
  2. **The program gets the whole machine.**  More KB above the PSP under
     `kern_dos` than in the window - the handoff actually happened, rather than
     the box refusing and the machine never leaving.
  3. **THE RETURN IS THE LIVE ONE.**  Measured in guest CYCLES, because
     `kd_leave`'s fallback is a POST and a whole boot - and a boot runs
     `mouse_init`, which arms the UART and would make readings 4 GREEN ON A
     BROKEN KERNEL.  Without this the row tests nothing on the machine it
     exists for.
  4. **The pointer works AFTER it.**  IER reads 01 again, and the cursor
     actually MOVES - the register and the user-visible claim, because the
     register alone would not catch a mask or a vector that had also gone.

VERIFIED TO FAIL: on the kernel before 96.45.2's `hbm_wake` step 3d, reading 4
finds IER 00 and a cursor that does not move, with 1-3 unchanged.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import dosmap                                                  # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402
import kdreturn as KR                                          # noqa: E402

CPS = 4772727.0                 # a 4.77 MHz guest second
LIVE_MAX = 15 * CPS             # kdreturn's own bound: past this is a boot


def fail(msg):
    print("mouresume: FAIL: %s" % msg)
    sys.exit(1)


def uart(m):
    """(port row, UART base) of the port the contest settled on."""
    k = M.KERNEL_SEG * 16
    sym = os88sym.syms()
    row = m.read(k + sym["mou_port"], 1)[0]
    if row >= 4:                # MOU_P2ROW or no row: a PS/2 pointer, and
        return row, 0           # 96.45.2 says plainly that it is not covered
    base = struct.unpack("<H", m.read(k + sym["mou_bases"] + row, 2))[0]
    return row, base


def alive(m, mo, to):
    """Move the pointer and report whether the guest agrees it moved."""
    k = M.KERNEL_SEG * 16
    sym = os88sym.syms()
    before = struct.unpack("<H", m.read(k + sym["mouse_x"], 2))[0]
    try:
        mo.to(*to)
    except Exception:
        pass                    # a verb that cannot confirm is the symptom
    after = struct.unpack("<H", m.read(k + sym["mouse_x"], 2))[0]
    return before, after


def main():
    # --machine lets a reader point this at a GENUINE IBM ROM machine
    # (os8088_5150_cga_hdd) rather than the GLaBIOS XT the row registers, which
    # is what docs/FIELD-NOTES.md's 8088 reports are taken on
    machine = KR.MACHINE
    if "--machine" in sys.argv:
        machine = sys.argv[sys.argv.index("--machine") + 1]
    KR.fixture()
    m = M.launch(None, apps=KR.FLOPPY, machine=machine,
                 extra=["--mount", "hd:0:" + KR.VHD])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        mo = os88mouse.Mouse(marty=m)

        # --- 1. the pointer works BEFORE the handoff ------------------------
        row, base = uart(m)
        if not base:
            print("mouresume: SKIP - the pointer is on the PS/2 aux port "
                  "(row %d), which SPEC.md 96.45.2 does not cover" % row)
            return 0
        ier0 = m.inb(base + 1)
        b, a = alive(m, mo, (200, 120))
        if ier0 != 1 or a == b:
            fail("the pointer is not alive before the handoff: port row %d "
                 "base %04X, IER %02X, mouse_x %d -> %d. Nothing below is "
                 "about the return" % (row, base, ier0, b, a))
        print("mouresume: before - port row %d base %04X, IER %02X, "
              "mouse_x %d -> %d" % (row, base, ier0, b, a))

        # --- 2. ...and the program gets the whole machine -------------------
        win = ui.path("B:/DOSHELLO.COM")
        if not win:
            fail("double-clicking DOSHELLO.COM opened no window")
        win_kb = KR.topmem(KR.wait_text(m, "READY", 150, "the windowed run"))
        m.type_text("x")
        M.settle(m)
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        # **[dos_keepc] IS THE MEMORY ARM'S RADIO** (SPEC.md 96.36), so this
        # write is what picks DOS_MEM_WHOLE and not decoration: without it the
        # Run button below re-runs the program WINDOWED, reading 2 finds the
        # same KB it found the first time, and the row never reaches a handoff
        # at all. The other three are zeroed for kdreturn's reason - whatever
        # is found afterwards is then the return's.
        m.write((pseg << 4) + dm["dos_exit"], bytes([0]))
        m.write((pseg << 4) + dm["dos_akb"], bytes([0, 0]))
        m.write((pseg << 4) + dm["dos_state"], bytes([0]))
        m.write((pseg << 4) + dm["dos_keepc"], bytes([1, 0]))   # DOS_MEM_WHOLE
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        kd_kb = KR.topmem(KR.wait_text(m, "READY", 300, "the run under kern_dos"))
        if kd_kb <= win_kb:
            fail("%d KB under kern_dos against %d in the window - the handoff "
                 "gave the program no more memory, so the machine never left"
                 % (kd_kb, win_kb))
        print("mouresume: handed over - %d KB against %d in the window"
              % (kd_kb, win_kb))

        # --- 3. ...and it comes back WITHOUT A BOOT -------------------------
        c0 = m.status()["cycles"]
        m.type_text("x")
        c1 = []
        try:
            KR.wait_desktop(m, ui, stamp=c1)
        except Exception as e:
            fail("the machine never came back after the return: %s" % e)
        spent = c1[0] - c0
        print("mouresume: back in %.2f guest seconds" % (spent / CPS))
        if spent > LIVE_MAX:
            fail("the return took %.1f guest seconds, which is a POST and a "
                 "BOOT - kd_leave fell back to int 19h. THAT BOOT RUNS "
                 "mouse_init, so reading 4 below would be green on a kernel "
                 "with the defect. Fix the live route before trusting this row"
                 % (spent / CPS))

        # --- 4. ...with the pointer still alive -----------------------------
        ier1 = m.inb(base + 1)
        b, a = alive(m, mo, (420, 150))
        k = M.KERNEL_SEG * 16
        sym = os88sym.syms()
        g = lambda n: m.read(k + sym[n], 1)[0]
        print("mouresume: after  - IER %02X MCR %02X LSR %02X MSR %02X  "
              "PIC21 %02X  seen %d port %d line %02X drain %d hpst %d  "
              "mouse_x %d -> %d"
              % (ier1, m.inb(base + 4), m.inb(base + 5), m.inb(base + 6),
                 m.inb(0x21), g("mou_seen"), g("mou_port"), g("mou_line"),
                 g("mou_drain"), g("mou_hpst"), b, a))
        if ier1 != 1 or a == b:
            fail("THE POINTER CAME HOME DEAD (SPEC.md 96.45.2): IER %02X and "
                 "mouse_x %d -> %d after a LIVE resume. kd_mou_stop set IER = "
                 "0 on the way out and hbm_wake step 3d is what puts it back - "
                 "mouse_init cannot, being in .ovlw, which mem_unblob freed"
                 % (ier1, b, a))
    finally:
        m.close()
        for p in (KR.VHD, KR.FLOPPY):
            try:
                os.unlink(p)
            except OSError:
                pass
    print("mouresume: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
