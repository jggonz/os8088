#!/usr/bin/env python3
"""A KNOB kernel, installed to a hard disk and booted off it - both adapters.

    python3 tests/knobhd.py

**Nothing else does this, and that is exactly how SPEC.md 52.10.2.1 survived.**
The build matrix ASSEMBLES every knob kernel and never boots one; `hdboot.py`
boots off a hard disk and only ever the shipped kernel; every other boot row
uses a floppy.  52.10.2.1 needed all three at once - a kernel whose memory
ladder differs from the shipped one, reached through the volume boot record,
which is the only loader that has to be TOLD where the heap starts.  The
floppy path cannot show it: `boot/boot.asm` learns the address from the kernel
it has just loaded.

`t_vbrseg.py` is the static half and costs one assembly.  This is the half
that runs the machine, and it checks the thing the static one cannot: that the
address the volume boot record wrote is the address the kernel READS BACK,
live, while the loading screen is still up.

**Two adapters, and the VGA one is the point.**  `os8088_xt_hdd` is CGA, so on
it the loading screen draws the bar and nothing else - no dialog, no caption,
no percentage (SPEC.md 15.3), and none of §15.3.2's blitting.  A defect in the
chrome that only a hard-disk boot reaches (2.9.9.1's `spl_step` starting the
screen itself, 2.9.9.2's `spl_rechrome`) is invisible there.
`os8088_xt_vga_hdd` exists for that and this row is why.

THE KNOB PAIR IS `BOOTPROF=1 MOUDIAG=1` on purpose: it is the configuration
that moves the ladder furthest from the shipped kernel's (three rungs), it is
the one that failed, and both halves draw a table on the finished desktop, so
a boot that limps shows up as a table that is not there.

IT REBUILDS `build/` and puts it back.  `tests/gfxlk.py`'s pattern, for its
reason: `os88sym` refuses an address unless the map matches `build/kernel.bin`
byte for byte, so the kernel under test has to BE the one in `build/`.  The
`finally` runs a plain `make`.  It also ERASES THE VHD, like `instdeep.py`.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty                                           # noqa: E402
import os88sym                                             # noqa: E402

KNOBS = ["BOOTPROF=1", "MOUDIAG=1"]
DEFINES = ("BOOT_PROFILE", "MOU_DIAG")
MACHINES = ("os8088_xt_hdd", "os8088_xt_vga_hdd")
BLANK = os.path.join(ROOT, "build", "hdboot-blank.img")

fail = []


def menu_bar_lit(px, w, h):
    """Ink in the top eight rows, which no loading screen has and every
    desktop does - hdboot.py's own gate, counted rather than grouped."""
    return sum(1 for y in range(2, 10) for x in range(0, w)
               if px[(y * w + x) * 3] < 128)


def boot(machine, heap):
    print("  %s ..." % machine)
    lin_live = os88sym.linear("spl_live", DEFINES)
    lin_fseg = os88sym.linear("spl_fseg", DEFINES)
    seen, fseg_seen, desktop = False, set(), False
    with os88marty.launch(BLANK, machine=machine, boot=0) as m:
        lin_entry = os88sym.linear("cold_entry", DEFINES)
        for i in range(500):
            m.advance(frames=6)
            if 20 <= i <= 260 and i % 10 == 0:
                m.key("KeyC")               # GLaBIOS: boot the hard disk. More
                                            # than once: the VGA machine's ROM
                                            # init pushes POST past frame 300
                                            # and the menu times out to A:
            if m.read(lin_entry, 1)[0] != 0xE9:
                continue                    # the kernel is not resident yet
            if m.read(lin_live, 1)[0] == 1:
                seen = True
                fseg_seen.add(int.from_bytes(m.read(lin_fseg, 2), "little"))
                continue
            if seen:
                break
        for _ in range(120):                # ...and on to a desktop
            m.advance(frames=10)
            w, h, px = m.fbuf()
            if menu_bar_lit(px, w, h) > 200:
                desktop = True
                break

    if not seen:
        fail.append("%s: the loading screen never came up - a hard-disk boot "
                    "has no loader that ticks, so spl_step starts it itself "
                    "(SPEC.md 2.9.9.1) and that is what this catches" % machine)
        return
    if fseg_seen != {heap}:
        fail.append("%s: the kernel read [spl_fseg] = %s while the splash was "
                    "live and its own HEAP_SEG is %04X. SPEC.md 52.10.2.1: the "
                    "volume boot record was built from a different define set "
                    "than the kernel it loads"
                    % (machine, sorted("%04X" % v for v in fseg_seen), heap))
    if not desktop:
        fail.append("%s: no desktop. The loading screen came up and the boot "
                    "did not finish" % machine)
    print("     ok  splash up, [spl_fseg] %s, desktop reached: %s"
          % (",".join("%04X" % v for v in sorted(fseg_seen)), desktop))


def main():
    print("  building the knob pair (%s)..." % " ".join(KNOBS))
    r = subprocess.run(["make"] + KNOBS, cwd=ROOT,
                       capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit("knobhd: `make %s` failed, so nothing below would "
                         "mean what it says:\n%s"
                         % (" ".join(KNOBS), r.stdout[-2000:] + r.stderr[-2000:]))
    try:
        os88sym.default_defines(*DEFINES)
        heap = os88sym.equates(DEFINES)["HEAP_SEG"]
        print("  the knob kernel's HEAP_SEG is %04X" % heap)

        env = dict(os.environ, OS88_DEFINES=" ".join(DEFINES))
        print("  installing (tests/instdeep.py drives the installer)...")
        r = subprocess.run([sys.executable, os.path.join(HERE, "instdeep.py")],
                           cwd=ROOT, capture_output=True, text=True, env=env,
                           timeout=1800)
        if r.returncode != 0:
            raise SystemExit("knobhd: the install failed:\n" + r.stdout[-2000:])

        if not os.path.exists(BLANK):
            with open(BLANK, "wb") as f:    # no boot signature, so GLaBIOS
                f.write(bytes(368640))      # offers its menu and C: is the disk
        for machine in MACHINES:
            boot(machine, heap)
    finally:
        subprocess.check_call(["make"], cwd=ROOT, stdout=subprocess.DEVNULL)

    if fail:
        for f in fail:
            print("  FAIL " + f)
        raise SystemExit("knobhd: %d failure(s)" % len(fail))
    print("knobhd: a knob kernel installs, boots off the disk and reaches a "
          "desktop on both adapters")


main()
