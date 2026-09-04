#!/usr/bin/env python3
"""The C64's ROM is INSIDE the package now (SPEC.md 20.12, C64-SPEC 1.4).

    make c64disk && python3 tests/c64part.py [machine] [system-image]

THE FIRST REAL CONSUMER of the parts standard, and the one the design was
argued for (docs/O88-MULTISEG-PLAN.md 11.1). `C64.ROM` was a 20,480-byte
SIDECAR: 8KB of KERNAL, 8KB of BASIC and 4KB of character generator, sitting
beside C64.O88 in one folder, that a file copy could separate from the program
it is useless without. The port carried a whole halted-machine state to say so
when it went missing - a permanent status row naming the file, a four-line
notice on the glass with its own expose repair, three greyed menu items and a
SECOND host-test process to draw the screen a mis-copied disk showed.

It is part 0 of C64.O88 now. All of that is deleted rather than disabled,
because a greying may not outlive its reason (SPEC.md 47).

FIVE ASSERTIONS, and the last is the one that says the bytes are real:

  1. C64.ROM IS NOT ON THE DISK. Read out of the guest's own directory
     listing, because that is what a user would see;
  2. the package declares parts - version 3, flags bit 2 - and its IMAGE is
     smaller than its FILE. There is no .o88 v6 and never was;
  3. it launched: a window, and ld_status 0;
  4. os88_part_seg(0) answered, and it is the segment the C put in
     c64_m.romseg - the standard's answer and the package's use of it, which
     are two different reads of the same fact;
  5. AND ITS BYTES ARE THE ROM. Five 16-byte windows read out of the guest at
     the segment the package uses - the KERNAL's first bytes, its reset vector
     at $FFFC, BASIC's first bytes, the character generator's first row and
     the LAST SIXTEEN BYTES OF THE PART - each compared with
     build/c64-rom/C64.ROM. The last window is why the set is five: a carve
     one sector short reads perfectly at the FRONT, and a package handed a
     claim that was filled most of the way looks exactly like one that was
     filled.

WHAT THIS ROW DELIBERATELY DOES NOT ASSERT, and why it is written down here:
**that the KERNAL boots.** The obvious fifth assertion is
`**** COMMODORE 64 BASIC V2 ****` in the C64's own screen matrix - nothing but
the real KERNAL executing out of the real ROM puts it there. It is not
asserted because IT IS NOT TRUE BEFORE THIS CHANGE EITHER: measured by A/B
against the unconverted package - the same probe, on a build with the sidecar
back and this conversion stashed - the 6510 runs (cycles accumulate, `1% cpu`,
no JAM) and never writes a byte of its own RAM: the matrix holds
c64_ram_pattern's factory fill, zero page $00/$01 is 0000 where the KERNAL's
reset writes $2F/$37 within a few hundred cycles, and $0314 carries no IRQ
vector. That is the state of apps/c64's 6510 core on this branch and it is not
this wave's to fix; asserting it here would be a row that fails for a reason
it does not name. What CAN be said is said: the machine is running and not
jammed, and the bytes it is fetching from are the ROM.

VERIFIED TO FAIL - with the part's row given a length one sector short, the
carve is short, op_read's own [op_want] check refuses the launch and there is
no window at all (assertion 3).
"""
import os
import struct
import subprocess
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88parts
import os88sym
import dispcp
import os88fixture
from os88map import Syms                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/c64.img"
O88 = "build/c64.o88"
ROM_PART = 0
CM_RAMSEG, CM_ROMSEG = 0, 2             # apps/c64/c64cpu.inc's machine record
C64_MATRIX = 0x0400                     # the default screen matrix
fails = []

C64 = Syms("apps/c64/c64.asm", "build/c64.bin", ["apps", "build"])


def say(s):
    print("  " + s)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def run():
    S = os88sym.linear
    blob = open(os.path.join(ROOT, O88), "rb").read()
    image = u16(blob, 8)
    rows = os88parts.rows(blob[:image])
    say("C64.O88: image %d, file %d, %d part(s), flags 0x%02X"
        % (image, len(blob), len(rows), blob[3]))

    # --- 2. the package declares parts, and the file is longer -------------
    if blob[2] != 3:
        fails.append("C64.O88 says version %d and must say 3: a package "
                     "carrying parts is a v3 package with one flag bit, for "
                     "SPEC.md 54.6's reason" % blob[2])
    if not blob[3] & 4:
        fails.append("C64.O88's flags are 0x%02X and bit 2 is clear, so the "
                     "kernel is not being told its file is longer than its "
                     "image and ld_check_hdr would refuse it" % blob[3])
    if image >= len(blob):
        fails.append("C64.O88's image (%d) is not smaller than the file (%d) "
                     "- the ROM was not appended" % (image, len(blob)))
    if len(rows) != 1 or rows[ROM_PART]["len"] != 20480:
        fails.append("the part table is %r and should be one ASSET of 20,480 "
                     "bytes - the KERNAL, BASIC and CHARGEN of C64-SPEC 1.4"
                     % (rows,))

    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)           # the KERNAL's boot is a MINUTE of
                                        # guest time on this machine, and the
                                        # saver draws - so a settle inside it
                                        # would be watching the saver
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "C64")
        os88marty.settle(m)

        # --- 1. ...and the sidecar is not in the folder -------------------
        listed = [n.upper() for n, _ in dispcp.listing(m, S)]
        say("C64/ holds %r" % listed)
        if "C64.ROM" in listed:
            fails.append(
                "C64.ROM is still on the disk. The whole point of the "
                "conversion is that the ROM cannot be separated from the "
                "program - a folder that still carries it is one where a "
                "copy can still lose it (docs/O88-MULTISEG-PLAN.md 11.1)")
        if "C64.O88" not in listed:
            raise SystemExit("c64part: C64.O88 is not in C64/ - run `make "
                             "c64disk`. The folder lists %r" % listed)

        wx2, wy2 = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx2, wy2, "C64.O88")

        # --- 3. it launched ------------------------------------------------
        st = m.read(S("ld_status"), 1)[0]
        seg = 0
        for i in dispcp.win_list(m, S):
            rec = m.read(S("wm_wins") + i * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
            sg = u16(rec, 22)           # W_SEG
            if sg and m.read(sg << 4, 2) == b"O8":
                seg = sg
        say("ld_status = %d, package segment %04X" % (st, seg))
        if st != 0 or not seg:
            raise SystemExit(
                "c64part: C64 did not launch (ld_status %d). 4 is the entry "
                "proc refusing, which for this package is op_load: it claims "
                "the ROM's 20KB and reads it before any C runs, and toasts "
                "why if it cannot" % st)

        # --- 4. the standard's answer, and the package's use of it ---------
        pseg = u16(m.read((seg << 4) + C64.sym("op_base"), 2))
        slack = u16(m.read((seg << 4) + C64.sym("op_slack"), 2))
        want = (pseg + slack // 16) & 0xFFFF
        mach = C64.sym("_c64_m")
        romseg = u16(m.read((seg << 4) + mach + CM_ROMSEG, 2))
        ramseg = u16(m.read((seg << 4) + mach + CM_RAMSEG, 2))
        say("op_base %04X + slack %d -> %04X; c64_m.romseg %04X, ramseg %04X"
            % (pseg, slack, want, romseg, ramseg))
        if not romseg or romseg != want:
            fails.append(
                "c64_m.romseg is %04X and op_seg(0) is %04X. The C reads the "
                "ROM's base straight out of the standard, so these are two "
                "reads of one fact and a disagreement means the package is "
                "pointing its 6510 somewhere the ROM is not" % (romseg, want))
        if romseg and romseg < 0x0E00:
            fails.append(
                "the ROM part landed at %04X, below 0x0E00. The KERNAL is "
                "fetched through `ES = romseg - $0E00` (c64cpu.inc 4.3) and "
                "that arithmetic UNDERFLOWS below this - it reads somewhere "
                "else entirely, silently" % romseg)

        # --- 5a. the bytes ARE the ROM, spot-checked against the file -----
        # Cheap and exact, and it is not the same question as 5b: this says
        # the part arrived intact at the address the package uses, and 5b says
        # the machine ran out of it.
        rom = open(os.path.join(ROOT, "build", "c64-rom", "C64.ROM"), "rb").read()
        for off, what in ((0x0000, "the KERNAL's first bytes"),
                          (0x1FFC, "the KERNAL's reset vector at $FFFC"),
                          (0x2000, "BASIC's first bytes"),
                          (0x4000, "the character generator's first row"),
                          (0x4FF0, "the LAST 16 bytes of the part")):
            got = bytes(m.read((romseg << 4) + off, 16))
            if got != rom[off:off + 16]:
                fails.append(
                    "%s differ at ROM offset 0x%04X: the guest holds %s and "
                    "build/c64-rom/C64.ROM holds %s. The part is claimed and "
                    "read by apps/os88parts.inc alone, so this is the "
                    "standard placing 20,480 bytes at a paragraph boundary "
                    "past a head slack - the last window is here because a "
                    "carve one sector short reads correctly at the FRONT"
                    % (what, off, got.hex(), rom[off:off + 16].hex()))

        # --- and the machine is RUNNING, which is as far as this row goes -
        st = u16(m.read((seg << 4) + C64.sym("_c64_state"), 2))
        c0 = u16(m.read((seg << 4) + C64.sym("_c64_cyc_lo"), 2))
        m.advance(frames=60)
        c1 = u16(m.read((seg << 4) + C64.sym("_c64_cyc_lo"), 2))
        say("c64_state %d, emulated cycles %d -> %d" % (st, c0, c1))
        if st == 1:
            fails.append(
                "the 6510 JAMMED (C64_ST_JAM) - it fetched an illegal "
                "opcode, which is one of the things executing something that "
                "is not the KERNAL looks like")
        if c1 == c0:
            fails.append(
                "the emulated cycle counter did not move over 60 frames, so "
                "the slice driver is not running the machine at all - and a "
                "ROM nothing executes is a ROM this row cannot speak for")


def main():
    # NOTHING IN `all` BUILDS THE C64 (it needs the C toolchain), so this row
    # asks make for its disk rather than testing for the file: an image that
    # exists but was built before a change to apps/os88parts.inc is exactly
    # the stale fixture os88map's byte-identity check then refuses to describe
    # - correctly, and several steps after the point where make could simply
    # have rebuilt it (tools/os88fixture.py).
    os88fixture.need(APPS_IMG)
    run()
    if fails:
        print("\nc64part: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\nc64part: the ROM is in the package, the standard placed it and "
          "its bytes are the ROM - PASS on %s" % MACHINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
