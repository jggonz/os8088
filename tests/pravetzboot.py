#!/usr/bin/env python3
"""An unsupported INT 15h must not disable the boot's timer (SPEC.md 9.7.2).

    make && python3 tests/pravetzboot.py

The Pravetz 16S / CPU12 Plus ROM rejects AH=C0h with STC / MOV AH,86h /
RETF 2. INT cleared IF, and that return discards the saved FLAGS rather
than restoring them. The keyboard-intercept probe introduced in #313 left
IF clear and the next serial-mouse reset waited forever for timer ticks:
mode 12h, a black screen, ticks=2, no desktop.

Boot that real ROM on 86Box's Pravetz, V20/12 MHz, 8087 and OTI-037C.
Check the ROM's adverse return, the fallback to the int 09h hook, a live
desktop, advancing ticks and a responsive serial mouse. kbd4f is the
complementary advertised-intercept path. For the negative control, pass
--system and --map from the original build: the desktop gate must fail.
The optional paths never rebuild or replace the kernel under test.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests" / "unit"))
import os88box                                              # noqa: E402
import os88build                                            # noqa: E402
from harness import check, done                             # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--system", default=os88build.at("build/os8088.img"))
    ap.add_argument("--apps", default=os88build.at("build/apps.img"))
    ap.add_argument("--map", default=os88build.at("build/kernel.map"))
    ap.add_argument("--evidence", default=os88build.at("build/pravetzboot.png"))
    a = ap.parse_args()
    with os88box.launch(
            "xt640", name="pravetzboot", a=a.system, b=a.apps, kmap=a.map,
            hold=True, fresh=True, temp=True,
            sets=("Machine.machine=pravetz16s", "Machine.cpu_family=necv20",
                  "Machine.cpu_speed=12000000", "Machine.fpu_type=8087",
                  "Machine.mem_size=1024", "Video.gfxcard=oti037c",
                  "Storage controllers.fdc=fdc_at",
                  "Floppy and CD-ROM drives.fdd_01_type=35_2hd",
                  "Floppy and CD-ROM drives.fdd_02_type=35_2hd")) as b:
        try:
            b.kernel_up(guest_ms=45000)
        except SystemExit as e:
            b.shot(a.evidence)
            check(False, "Pravetz reaches a live desktop",
                  "The C0h probe must restore IF before the serial reset's "
                  "timer wait; a black screen with ticks=2 is the regression.",
                  got={"error": str(e), "ticks": b.word("ticks"),
                       "regs": b.cmd("regs")})
        else:
            # The failed C0h probe leaves INT 15h pointing at the original
            # ROM, so verify that this really exercised the RETF 2 case.
            v = b.memr(0x54, 4)
            rom = int.from_bytes(v[2:], "little") * 16 + int.from_bytes(v[:2], "little")
            check(b.memr(rom, 6) == bytes.fromhex("f9b486ca0200"),
                  "ROM rejects the query with STC / MOV AH,86h / RETF 2")
            check(b.word("kbm_ivec") == 0x24,
                  "unsupported C0h keeps the int 09h keyboard fallback")
            check(b.byte("cpu_tier") == 0 and b.byte("cpu_feat") & 8,
                  "V20 is 8086 class and the 8087 is detected")
            t0 = b.word("ticks")
            b.until(lambda: b.word("ticks") != t0, "IRQ0 after the desktop",
                    guest_ms=1000, step_ms=20)
            check(True, "timer ticks advance after the desktop")
            b.to(250, 180)
            check(tuple(b.pointer()) == (250, 180), "serial mouse responds")
            b.shot(a.evidence)
    done("pravetzboot")


if __name__ == "__main__":
    main()
