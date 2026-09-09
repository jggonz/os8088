#!/usr/bin/env python3
"""The Apple II+ ROM is INSIDE APPLE2.O88 (SPEC.md 20.12, APPLE2-SPEC 1.5).

    make apple2disk && python3 tests/apple2part.py [machine] [system-image]

`tests/c64part.py`'s shape one machine along, and the differences are the
port's own. The C64 CONVERTED a sidecar and this package never had one: the
14,848 bytes of Applesoft, the Autostart Monitor, the character generator and
the Disk II boot ROM have been part 0 since wave 1, so there is no deleted
halted-machine state to point at - what this row defends is that the shape
stayed that way.

SIX ASSERTIONS, and the last two are this package's rather than the C64's:

  1. APPLE2.ROM IS NOT ON THE DISK, read out of the guest's own directory
     listing, because that is what a user would see. The four geometries
     carry five files - the package, the overlay, README.TXT, COPYING and
     WELCOME.BAS (APPLE2-SPEC 16.2) - and a sixth named `.ROM` would be a
     file a copy could lose;
  2. the package declares parts - version 3, flags bit 2 - its IMAGE is
     smaller than its FILE, and the one part is an ASSET of exactly 14,848
     bytes: 12,288 of ROM, 2,048 of CHARGEN, 256 of DISK2 and a 256-byte pad
     (APPLE2-SPEC 1.4's layout);
  3. it launched: a window, and ld_status 0. A refusal here is op_load's,
     which runs BEFORE any C and toasts why;
  4. `os88_part_seg(0)` answered and it is the segment the C put in
     `a2_m.romseg` - the standard's answer and the package's use of it, which
     are two reads of one fact - and it is at or above A2_ROM_MINSEG, because
     the core fetches ROM through `ES = romseg - ($D000 >> 4)` and that
     arithmetic UNDERFLOWS silently below it;
  5. THREE WINDOWS OF THE ROM read out of the guest equal
     `build/apple2-rom/APPLE2.ROM` byte for byte - the first bytes, the
     CHARGEN block at 0x3000, and the LAST SIXTEEN BYTES OF THE PART, which
     is why three and not one: a carve one sector short reads perfectly at
     the front. **And the RESET vector at `$FFFC` reads `$FA62`**, which is
     the one number that says the file is the AUTOSTART Monitor and not some
     other Apple II ROM (APPLE2-SPEC 1.4);
  6. AND BOTH DISPLAY TABLES EXIST AFTER `os88_main` AND BEFORE ANY WAKE -
     the 512-byte decoded character generator and the 128-byte 7-bit reverse
     table. That is the NEGATIVE CONTROL for keeping them off the overlay
     (APPLE2-SPEC 7.3): a disk with no `APPLE2.OVL` has to be a program whose
     MENUS refuse, not a window that draws nothing, and a table that quietly
     moved into the module would leave the flush composing blanks. It is read
     at the moment the row reaches it, which is after the launch and before
     anything has been clicked.

...and the machine is RUNNING, which is as far as this row goes: `a2_state`
is A2_ST_RUN and not A2_ST_JAM, and either the 6502's PC moves across 180
frames or the speed window accumulated cycles in one of them - two reads of
one question, because `a2_m.pc` is written back at the END of a slice and the
Autostart Monitor's opening WAIT loop plus a constant cycle budget make the
exit PC repeat legitimately. What the machine PUTS ON THE GLASS - the banner,
the `]` prompt, the flashing cursor - belongs to the driven QMP runs and to
`a2uitest`; a screen assertion here would be a second copy of theirs.

VERIFIED TO FAIL - pointed at a ROM file one byte different from the one the
package was built with, assertion 5 names the offset and the two byte
strings; with `_a2_chr` read before `os88_main` has run it is assertion 6.
"""
import os
import struct
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
APPS_IMG = "build/apple2.img"
O88 = "build/apple2.o88"
ROM = os.path.join("build", "apple2-rom", "APPLE2.ROM")
ROM_PART = 0
ROM_LEN = 14848                         # APPLE2-SPEC 1.4's fixed layout
AM_RAMSEG, AM_ROMSEG, AM_PC = 0, 2, 4   # apps/apple2/a2cpu.inc's record
A2_ROM_MINSEG = 0x0D00                  # $D000 >> 4 - below it the fetch bias
                                        # underflows (apple2.c)
A2_ST_RUN, A2_ST_JAM = 1, 2
fails = []

A2 = Syms("apps/apple2/apple2.asm", "build/apple2.bin", ["apps", "build"])


def say(s):
    print("  " + s)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def run():
    S = os88sym.linear
    blob = open(os.path.join(ROOT, O88), "rb").read()
    image = u16(blob, 8)
    rows = os88parts.rows(blob[:image])
    say("APPLE2.O88: image %d, file %d, %d part(s), flags 0x%02X"
        % (image, len(blob), len(rows), blob[3]))

    # --- 2. the package declares parts, and the file is longer -------------
    if blob[2] != 3:
        fails.append("APPLE2.O88 says version %d and must say 3: a package "
                     "carrying parts is a v3 package with one flag bit"
                     % blob[2])
    if not blob[3] & 4:
        fails.append("APPLE2.O88's flags are 0x%02X and bit 2 is clear, so "
                     "the kernel is not being told its file is longer than "
                     "its image and ld_check_hdr would refuse it" % blob[3])
    if image >= len(blob):
        fails.append("APPLE2.O88's image (%d) is not smaller than the file "
                     "(%d) - the ROM was not appended" % (image, len(blob)))
    if len(rows) != 1 or rows[ROM_PART]["len"] != ROM_LEN:
        fails.append("the part table is %r and should be one ASSET of %d "
                     "bytes - the ROM, CHARGEN, DISK2 and pad of APPLE2-SPEC "
                     "1.4" % (rows, ROM_LEN))

    rom = open(os.path.join(ROOT, ROM), "rb").read()
    if len(rom) != ROM_LEN:
        raise SystemExit("apple2part: %s is %d bytes and the layout is %d - "
                         "run `make apple2rom`" % (ROM, len(rom), ROM_LEN))

    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPLE2")
        os88marty.settle(m)

        # --- 1. ...and the ROM is not a file in the folder ----------------
        listed = [n.upper() for n, _ in dispcp.listing(m, S)]
        say("APPLE2/ holds %r" % listed)
        if "APPLE2.ROM" in listed:
            fails.append(
                "APPLE2.ROM is on the disk. The ROM is part 0 of the package "
                "(APPLE2-SPEC 1.5) precisely so that it cannot be separated "
                "from the program it is useless without - a folder that "
                "carries it is one where a copy can still lose it")
        if "APPLE2.O88" not in listed:
            raise SystemExit("apple2part: APPLE2.O88 is not in APPLE2/ - run "
                             "`make apple2disk`. The folder lists %r" % listed)
        if "WELCOME.BAS" not in listed:
            fails.append(
                "WELCOME.BAS is not in APPLE2/ and APPLE2-SPEC 16.2 puts it "
                "there: the folder is the binding shape, and a listing that "
                "does not ride with the program is one nobody can open "
                "(the .OVL and the document are resolved in the SAME "
                "launched-from directory, SPEC.md 73.14 and 54.9)")

        wx2, wy2 = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx2, wy2, "APPLE2.O88")

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
                "apple2part: APPLE2 did not launch (ld_status %d). 4 is the "
                "entry proc refusing, which for this package is op_load: it "
                "claims the ROM's 14,848 bytes and reads them before any C "
                "runs, and toasts why if it cannot" % st)

        # --- 4. the standard's answer, and the package's use of it ---------
        pseg = u16(m.read((seg << 4) + A2.sym("op_base"), 2))
        slack = u16(m.read((seg << 4) + A2.sym("op_slack"), 2))
        want = (pseg + slack // 16) & 0xFFFF
        mach = A2.sym("_a2_m")
        romseg = u16(m.read((seg << 4) + mach + AM_ROMSEG, 2))
        ramseg = u16(m.read((seg << 4) + mach + AM_RAMSEG, 2))
        say("op_base %04X + slack %d -> %04X; a2_m.romseg %04X, ramseg %04X"
            % (pseg, slack, want, romseg, ramseg))
        if not romseg or romseg != want:
            fails.append(
                "a2_m.romseg is %04X and op_seg(0) is %04X. The C reads the "
                "ROM's base straight out of the standard, so these are two "
                "reads of one fact and a disagreement means the package is "
                "pointing its 6502 somewhere the ROM is not" % (romseg, want))
        if romseg and romseg < A2_ROM_MINSEG:
            fails.append(
                "the ROM part landed at %04X, below %04X. ROM is fetched "
                "through `ES = romseg - ($D000 >> 4)` (a2cpu.inc) and that "
                "arithmetic UNDERFLOWS below this - it reads somewhere else "
                "entirely, silently" % (romseg, A2_ROM_MINSEG))

        # --- 5. the bytes ARE the ROM ---------------------------------------
        for off, what in ((0x0000, "Applesoft's first bytes at $D000"),
                          (0x3000, "the character generator's first row"),
                          (ROM_LEN - 16, "the LAST 16 bytes of the part")):
            got = bytes(m.read((romseg << 4) + off, 16))
            if got != rom[off:off + 16]:
                fails.append(
                    "%s differ at ROM offset 0x%04X: the guest holds %s and "
                    "%s holds %s. The part is claimed and read by "
                    "apps/os88parts.inc alone, so this is the standard "
                    "placing %d bytes at a paragraph boundary past a head "
                    "slack - the last window is here because a carve one "
                    "sector short reads correctly at the FRONT"
                    % (what, off, got.hex(), ROM, rom[off:off + 16].hex(),
                       ROM_LEN))
        # ...AND THE RESET VECTOR, which is the one number that says WHICH
        # Apple II ROM this is (APPLE2-SPEC 1.4). $FFFC is offset 0x2FFC of a
        # part whose $D000 is offset 0.
        rst = u16(bytes(m.read((romseg << 4) + 0x2FFC, 2)))
        say("the RESET vector at $FFFC reads $%04X" % rst)
        if rst != 0xFA62:
            fails.append(
                "the RESET vector at $FFFC reads $%04X and the Autostart "
                "Monitor's is $FA62. A II+ ROM that is not the Autostart one "
                "boots to a different machine, and every screendump of it "
                "looks like a screendump" % rst)

        # --- 6. both display tables exist, and it is BEFORE any wake -------
        chr512 = bytes(m.read((seg << 4) + A2.sym("_a2_chr"), 512))
        rev = bytes(m.read((seg << 4) + A2.sym("_a2_rev"), 128))
        nz = sum(1 for b in chr512 if b)
        say("a2_chr: %d of 512 bytes non-zero; a2_rev[0x01]=%02X "
            "a2_rev[0x40]=%02X" % (nz, rev[0x01], rev[0x40]))
        if nz < 256:
            fails.append(
                "the decoded character generator is %d/512 non-zero bytes, "
                "so a2_chargen did not run in os88_main. It is RESIDENT on "
                "purpose (APPLE2-SPEC 7.3): a disk with no APPLE2.OVL has to "
                "be a program whose MENUS refuse, not a window that draws "
                "nothing" % nz)
        # the table is a 7-bit mirror: bit 0 <-> bit 6, and nothing outside
        # the low seven bits is ever set.
        bad = [i for i in range(128)
               if rev[i] != int('{:07b}'.format(i)[::-1], 2)]
        if bad:
            fails.append(
                "the 7-bit reverse table is wrong at %d index(es), first "
                "0x%02X (holds %02X): a2_rev is built in os88_main beside "
                "the chargen decode and every hi-res byte on the glass goes "
                "through it" % (len(bad), bad[0], rev[bad[0]]))

        # --- and the machine is RUNNING ------------------------------------
        st = u16(m.read((seg << 4) + A2.sym("_a2_state"), 2))
        pcs, spent = [], []
        for _ in range(6):
            pcs.append(u16(m.read((seg << 4) + mach + AM_PC, 2)))
            spent.append(u16(m.read((seg << 4) + A2.sym("_a2_c64u"), 2)))
            m.advance(frames=30)
        say("a2_state %d, 6502 PC %s" % (st, " ".join("$%04X" % p
                                                      for p in pcs)))
        say("64-cycle units in the speed window: %s"
            % " ".join(str(u) for u in spent))
        if st == A2_ST_JAM:
            fails.append(
                "the 6502 JAMMED (A2_ST_JAM) - it fetched an illegal opcode, "
                "which is one of the things executing something that is not "
                "the Monitor looks like")
        elif st != A2_ST_RUN:
            fails.append(
                "a2_state is %d and A2_ST_RUN is %d: the slice driver is not "
                "running the machine, and a ROM nothing executes is a ROM "
                "this row cannot speak for" % (st, A2_ST_RUN))
        # TWO LIVENESS READS AND EITHER WILL DO, WHICH IS NOT SLACKNESS.
        # a2_m.pc is written back at the END of a2_run, and the machine spends
        # its first seconds in the Autostart Monitor's WAIT loop at
        # $FCA8-$FCB3 with a CONSTANT cycle budget - so the exit PC repeats,
        # legitimately and often (measured: three distinct values in six
        # samples). a2_c64u is the speed window's own accumulator and is
        # non-zero whenever cycles were spent since the last one-second fold,
        # which is the direct question; the PC set is the one that survives a
        # fold landing on every sample.
        if len(set(pcs)) == 1 and not any(spent):
            fails.append(
                "the 6502's PC read $%04X six times across 180 frames and the "
                "speed window accumulated nothing in any of them: the slice "
                "driver is not running the machine, and a ROM nothing "
                "executes is a ROM this row cannot speak for" % pcs[0])


def main():
    # NOTHING IN `all` BUILDS APPLE2 (it needs the C toolchain AND the pinned
    # ROM fetch), so this row asks make for its disk rather than testing for
    # the file: an image that exists but predates a change to
    # apps/os88parts.inc is the stale fixture os88map's byte-identity check
    # then refuses to describe, several steps after the point where make could
    # simply have rebuilt it.
    os88fixture.need(APPS_IMG)
    run()
    if fails:
        print("\napple2part: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\napple2part: the ROM is in the package, the standard placed it, "
          "its bytes are the Autostart Monitor's and both display tables are "
          "resident - PASS on %s" % MACHINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
