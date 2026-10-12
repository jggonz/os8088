#!/usr/bin/env python3
"""The MPU-401's port pin, on a machine that has one (SPEC.md 51.9.2, 31.15).

    make miditest && python3 tests/mpupin.py

MartyPC's os8088_5150_herc_mpu_720_gla carries a recording MPU-401 at 330h
(tools/martypc/patches/09-mpu401-uart.patch). `make miditest`'s system disk
asks for SOUND.DRV and nothing else; this row boots a COPY of it four times,
each with a different 'SP' record - the sound row's one-byte blob, which the
Drivers page's Ports view writes - and reads where SOUND.DRV's probe put the
MPU, out of the driver's own image:

  (no record)  Auto  -> 330h, found by the search
  SP = 98h     330h  -> 330h, the pinned port alone
  SP = 80h     300h  -> none: the pin names a port with no MPU on it, and the
                        search that would have found 330h does not run
  SP = FFh     Off   -> none, and not one byte reached the MPU's data port

The driver still attaches in every case, for its Covox candidate: the
Hercules's own printer port at 3BCh answers (SPEC.md 34.14). So `mpu_base`
is the reading, not whether the row loaded.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = "os8088_5150_herc_mpu_720_gla"
IMG = "build/midisys720.img"
SND_ROW, DRVR_SZ, DRVR_SEG = 0, 16, 2
CASES = [("Auto", None, 0x330), ("330h", 0x98, 0x330),
         ("300h", 0x80, 0), ("Off", 0xFF, 0)]

fails = []


def check(ok, what):
    print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    if not ok:
        fails.append(what)


def sound_syms():
    """SOUND.DRV's map, refused unless it describes build/sound.bin."""
    with tempfile.TemporaryDirectory() as d:
        src, mp, out = (os.path.join(d, n) for n in ("s.asm", "s.map",
                                                      "s.bin"))
        shutil.copy("drivers/sound/sound.asm", src)
        with open(src, "a") as f:
            f.write("\n[map all %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                        "drivers/sound/", "-I", "drivers/", "-I", "apps/",
                        "-o", out, src], check=True)
        if open(out, "rb").read() != open(os88build.at("build/sound.bin"),
                                          "rb").read():
            sys.exit("mpupin: the mapped build is not build/sound.bin")
        syms = {}
        for line in open(mp):
            m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$",
                         line)
            if m:
                syms[m.group(3)] = int(m.group(1), 16)
        return syms


def cfg(pin):
    """midicfg's SYSTEM.CFG (SOUND.DRV wanted), plus an 'SP' record."""
    body = b"DW" + bytes([1, 2]) + (1 << SND_ROW).to_bytes(2, "little")
    if pin is not None:
        body += b"SP" + bytes([1, 1, pin])
    return b"O88CFG\0\0" + (3).to_bytes(2, "little") + body + b"\0\0"


def main():
    os.chdir(ROOT)
    for p in (IMG, "build/sound.bin"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("mpupin: no %s - run `make miditest`" % p)
    syms = sound_syms()
    work = tempfile.mkdtemp(prefix="mpin-")
    try:
        for name, pin, want in CASES:
            img = os.path.join(work, "sys.img")
            shutil.copy(os88build.at(IMG), img)
            c = os.path.join(work, "SYSTEM.CFG")
            open(c, "wb").write(cfg(pin))
            subprocess.run(["mcopy", "-o", "-i", img, c, "::SYSTEM.CFG"],
                           check=True)
            subprocess.run(["mattrib", "-i", img, "+h", "+s",
                            "::SYSTEM.CFG"], check=True)
            print("mpupin: %s" % name, flush=True)
            with os88ui.boot(img, machine=MACHINE, verbose=False) as ui:
                m = ui.m
                row = os88sym.linear("drv_tab") + SND_ROW * DRVR_SZ
                seg = int.from_bytes(bytes(m.read(row + DRVR_SEG, 2)),
                                     "little")
                check(seg != 0, "%s: SOUND.DRV attached" % name)
                if not seg:
                    continue
                base = int.from_bytes(bytes(m.read((seg << 4)
                                                   + syms["mpu_base"], 2)),
                                      "little")
                check(base == want, "%s: the MPU-401 is at %s (driver says "
                      "%s)" % (name, "%03Xh" % want if want else "none",
                               "%03Xh" % base if base else "none"))
                if pin == 0xFF:
                    r = m.cmd(cmd="midi", reset=True)
                    check(not r["uart"] and not r["bytes"], "Off: the MPU "
                          "was never reset into UART mode and not one byte "
                          "reached it (uart %r, %d bytes)"
                          % (r["uart"], len(r["bytes"])))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("mpupin: %s" % ("FAILED" if fails else "ok"), flush=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
