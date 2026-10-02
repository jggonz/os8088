#!/usr/bin/env python3
"""v20boot - os8088 boots and runs on a NEC V20, and calls it an 8086.

The V20 is the classic XT upgrade: the same socket, bus and clock as the
8088, faster microcode, the 80186 instructions, and one thing an 8088 does not
have - FLAGS bit 15 is MD, the MODE flag, and clearing it puts the chip in
8080 emulation. A real V20 WRITE-PROTECTS MD in native mode: only BRKEM opens
it (and RETEM closes it), so POPF and IRET leave it alone and the classic
FLAGS probe reads bits 12-15 stuck at one, exactly as on an 8088.

MartyPC's V20 did not: POPF and IRET wrote MD like any other bit. So
cpu_detect's "try to clear bits 12-15" cleared MD, read 0x7000 back, decided
the bits were writable and called the machine a 386 (`[cpu_tier]` = 2). The
kernel then ran the AT-class probes against an XT's ports, the tick stopped,
and the CPU ran off into `.bss` with MD clear - which read exactly like an
os8088 fault. tools/martypc/patches/07-v20-mode-flag-protect.patch is the fix
and this row is its gate: take the patch out and step 1 reads tier 2.

Three steps, each a positive control for the next:
  1. the V20 profile boots to a settled desktop with `[cpu_tier]` = CPU_8086
     and MD still set in the live FLAGS;
  2. the machine RUNS - the tick advances and a package opens off B:;
  3. the CPU really is a V20: `shl ax, 4` (C1 E0 04, an 80186 form) is
     executed on it. An 8088 aliases C1 to RET and leaves AX alone, so a
     profile that silently lost its `upgrade_type` fails here rather than
     passing steps 1 and 2 on an 8088.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

import os88marty
import os88ui

MACHINE = "os8088_5150_herc_hdd_v20_gla"
MODE = 0x8000                   # FLAGS bit 15: the V20's MD
PROBE_SEG = 0x0050              # the BIOS data area's scratch end - the
PROBE = bytes([0xB8, 0x01, 0x00,    # mov ax, 1     machine is finished with
               0xC1, 0xE0, 0x04])   # shl ax, 4     by the time step 3 runs


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    root = os.path.join(HERE, "..")
    sysimg = os.path.join(root, "build", "os8088-360.img")
    apps = os.path.join(root, "build", "apps360.img")
    with os88ui.boot(sysimg, apps=apps, machine=MACHINE) as ui:
        m = ui.m if hasattr(ui, "m") else ui.marty

        # --- 1. the tier, and MD ------------------------------------------
        tier = m.read(m.sym("cpu_tier"), 1)[0]
        flags = m.regs()["flags"]
        print("cpu_tier %d, FLAGS %04X" % (tier, flags))
        if tier != 0:
            fail("cpu_tier is %d on a V20, want 0 (CPU_8086): the FLAGS "
                 "probe saw bits 12-15 move, so MD is not write-protected - "
                 "is tools/martypc/patches/07 applied?" % tier)
        if not flags & MODE:
            fail("FLAGS %04X has MD clear: the V20 is in 8080 mode" % flags)

        # --- 2. it runs ---------------------------------------------------
        taddr = m.sym("ticks")
        t0 = int.from_bytes(m.read(taddr, 2), "little")
        w = ui.path("B:/APPS/CALC.O88")
        t1 = int.from_bytes(m.read(taddr, 2), "little")
        print("opened %s; ticks %d -> %d" % (w, t0, t1))
        if (t1 - t0) & 0xFFFF == 0:     # a word, and it wraps
            fail("the tick did not advance (%d -> %d)" % (t0, t1))
        flags = m.regs()["flags"]
        if not flags & MODE:
            fail("MD clear after opening a package (FLAGS %04X)" % flags)

        # --- 3. it is a V20 -----------------------------------------------
        m.pause()
        m.write(PROBE_SEG * 16, PROBE)
        m.cmd(cmd="park", cs=PROBE_SEG, ip=0)
        m.step(2)
        ax = m.regs()["ax"]
        print("shl ax, 4 -> AX %04X" % ax)
        if ax != 0x0010:
            fail("AX %04X after `mov ax,1 / shl ax,4`: C1 is not an 80186 "
                 "shift here, so this machine is not a V20" % ax)
    print("ok: a V20 boots, runs and reads CPU_8086")


if __name__ == "__main__":
    main()
