#!/usr/bin/env python3
"""megapmmouse - a PicoMEM's USB mouse on kern_mega (KERN-MEGA-PLAN 14.117,
SPEC.md 9.12.7).

    make mega megarun && python3 tests/megapmmouse.py [tick|boot] [--keep]

The card's BIOS hands each USB mouse report to `int 33h AX=0060h` from inside
its own IRQ handler, and on kern_mega that handler runs in REAL mode - the
kernel reflects an option ROM's IRQ (mega_irqrom) - where USBMOUSE.DRV's
handler is protected-mode code that calls the kernel. The driver used to
refuse the PicoMEM outright under protected mode for that reason, and a BOOT
attach (real mode, before the switch) was not refused at all and left the
card calling the kernel's real-mode feed from a reflection. Now the
reflection CATCHES the int 33h (mega_capon/mega_cap33) and delivers the
report once it is back in protected mode (mega_m33go).

No emulator has a PicoMEM, so this is the narrowest model of one that still
takes the real road: drivers/usbmouse/pmsim.inc answers the three PM BIOS
functions attach and detach ask (assembled -DPMSIM, never shipped) and
points IRQ 7's vector at tests/megapmmouse/pmrom.asm - a Bochs OPTION ROM at
D000h whose handler loads a report and does `int 33h AX=0060h`, as the
card's does. tests/megapmmouse/pmtrig.asm, run off C: by MEGARUN, puts four
reports of (+20, +10) in 0040:00F0 and raises IRQ 7 for each as a software
interrupt; its gate is the kernel's reflection, so everything from there on
is what a field machine runs.

Two legs, one boot each:
  tick  the driver is NOT wanted at boot; PMTRIG ticks it in through
        OSAPI_DRV_USE - a PROTECTED-MODE attach, int 33h stamped with the
        driver's code SELECTOR - which must answer CF = 0
  boot  SYSTEM.CFG wants it: attached in REAL mode before the switch, int 33h
        a real segment, the reports arriving after it
Each must see the ROM take all four, the pointer move exactly (+40, +20) -
the driver halves (SPEC.md 9.12.3) - and no `kern_mega FAULT`/`CLOSED`.

Broken on purpose (docs/WRITING-TESTS.md 1): with mega_capon's int 33h
capture taken out (kernel/mega.inc) the boot leg runs the driver's handler
in real mode and the tick leg jumps through a selector there; with the
shipped driver's old CPU_F_PM refusal put back the tick leg answers CF = 1
(DRVE_HW).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402

CFG_USBM = 1 << 6               # SYSTEM.CFG's USBMOUSE.DRV bit (SPEC.md 9.12)
N, DX, DY = 4, 20, 10           # pmtrig.asm's reports
FAT = os.path.join(ROOT, "tools", "os88fat.py")


def nasm(out, src, *args):
    subprocess.run(["nasm", "-f", "bin", "-w+error"] + list(args) +
                   ["-o", out, os.path.join(ROOT, src)], check=True)


def build(d, up):
    """The model ROM, the sim driver on a copy of build/mega.img, and
    PMTRIG -> (rom, system disk, package)"""
    rom = os.path.join(d, "pmrom.bin")
    nasm(rom, "tests/megapmmouse/pmrom.asm")
    b = bytearray(open(rom, "rb").read())
    b[-1] = (-sum(b[:-1])) & 0xFF
    open(rom, "wb").write(b)
    drv = os.path.join(d, "usbmpm.bin")
    nasm(drv, "drivers/usbmouse/usbmouse.asm", "-I", "drivers/",
         "-I", "drivers/usbmouse/", "-I", "apps/", "-DPMSIM")
    os.makedirs(os.path.join(d, "drv"), exist_ok=True)
    dfile = os.path.join(d, "drv", "USBMOUSE.DRV")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88drv.py"),
                    drv, "-o", dfile], check=True, stdout=subprocess.DEVNULL)
    a = os.path.join(d, "a.mega.img")
    shutil.copy(os.path.join(ROOT, "build", "mega.img"), a)
    subprocess.run([sys.executable, FAT, "del", a, "USBMOUSE.DRV"],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, FAT, "add", a, dfile], check=True,
                   stdout=subprocess.DEVNULL)
    pbin = os.path.join(d, "pmtrig.bin")
    nasm(pbin, "tests/megapmmouse/pmtrig.asm", "-I", "apps/",
         *(["-DPMT_UP"] if up else []))
    pkg = os.path.join(d, "pmtrig.o88")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
                    pbin, "-o", pkg], check=True, stdout=subprocess.DEVNULL)
    return rom, a, pkg


def leg(name, keep):
    up = name == "tick"
    d = tempfile.mkdtemp(prefix="megapmmouse-%s-" % name)
    os.chdir(ROOT)
    rom, a, pkg = build(d, up)
    out = os.path.join(d, "box")
    os.makedirs(out)
    box = os88bochs.Box(out, [pkg], cfg=None if up else CFG_USBM, aimg=a,
                        rcextra="optromimage1: file=%s, address=0xd0000\n"
                        % rom)
    fails = []
    lines = []
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            raise RuntimeError("kern_mega never reached protected mode")
        box.up()
        box.megarun()
        end = time.time() + 120
        while time.time() < end:
            lines = box.lines()
            if any(l.startswith(("PMT: now", "kern_mega FAULT"))
                   for l in lines):
                break
            time.sleep(0.2)
        time.sleep(1)
        lines = box.lines()
    except RuntimeError as e:
        fails.append(str(e))
    finally:
        box.stop()
    for l in lines:
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append(l)
    got = {}
    for l in lines:
        if l.startswith("PMT: "):
            print("megapmmouse[%s]: %s" % (name, l))
            f = l.split()
            got[f[1]] = f[2:]
    if up:
        u = got.get("up")
        if not u or u[1] != "0":
            fails.append("the protected-mode tick did not attach the driver: "
                         "%s (CF 1 0005 is the old refusal, DRVE_HW)"
                         % (" ".join(u) if u else "no answer"))
    if "at" not in got or "now" not in got:
        fails.append("PMTRIG did not report the pointer before and after")
    else:
        x0, y0 = int(got["at"][0], 16), int(got["at"][1], 16)
        x1, y1 = int(got["now"][0], 16), int(got["now"][1], 16)
        took = int(got["now"][3], 16)
        if took != N:
            fails.append("the model ROM took %d of %d reports" % (took, N))
        want = (x0 + N * DX // 2, y0 + N * DY // 2)
        if (x1, y1) != want:
            fails.append("the pointer went (%d,%d) -> (%d,%d), wanted (%d,%d)"
                         ": the card's int 33h did not reach the driver's "
                         "feed" % ((x0, y0, x1, y1) + want))
    if keep or fails:
        print("megapmmouse[%s]: the boot is in %s" % (name, d))
    else:
        shutil.rmtree(d, ignore_errors=True)
    return fails


def main():
    keep = "--keep" in sys.argv
    legs = [a for a in sys.argv[1:] if not a.startswith("-")] or ["tick",
                                                                 "boot"]
    for f in ("build/mega.img", "build/megarun.o88", "build/mbr.bin",
              "build/megak/kernel.sys"):
        if not os.path.exists(os.path.join(ROOT, f)):
            sys.exit("megapmmouse: %s is missing - `make mega megarun` first"
                     % f)
    t0 = time.time()
    fails = []
    for n in legs:
        fails += ["%s: %s" % (n, f) for f in leg(n, keep)]
    print("megapmmouse: %s in %.0fs" % ("FAILED" if fails else "ok",
                                         time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
