#!/usr/bin/env python3
"""REALDMG.O88's save states: the machine taken and put back, exactly.

    python3 tests/gbstate.py [--harness | --package]

docs/REALDMG-SPEC.md section 10.4. Two halves:

THE HARNESS (QEMU, the bare core of tests/rdgcore/rdghost.asm, MODE 13). A
state is taken 200 frames before the end; at the end a sum of the picture
and of the Game Boy's 8000-FFFF is taken, the state put back and the same
200 frames run again, on the same scripted buttons - and the sum must be the
same. Twice per ROM: back into a machine just reset (rdg_init, the
recompiler's cache emptied), which is what makes a state-block field
rdg_stapply fails to bring back show, and back into the machine that ran on,
its translated code kept, as the package's is. CLOCK25.GB always; with
$OS88_GB_ROMS, Super Mario Land and Kirby's Dream Land. Leaving the clocks
out of rdg_stapply's table turns both games' reset legs red.

THE PACKAGE (MartyPC's VGA XT). CLOCK0.GB opened from a 720KB disk; 3 picks
slot 3 and F5 saves it - CLOCK0.SS3 is then on the floppy, read back by
tools/os88flush.py's own FAT walker, at 34,816 bytes with its record where
the format puts it; the game runs on, and F8 takes its frame counter back to
the save; 5 then F8 is an empty slot, which must change nothing. Then the
same F5 and F8 in full screen, where the machine is the UI task's own.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgimg  # noqa: E402
import rdgqemu  # noqa: E402

LINE = re.compile(r"RDGHOST DONE frames (\d+).*? state (\w+) (\w+)( refused)?")
MACHINE = "os8088_xt_vga_720b"
SYS = os.path.join(ROOT, "build", "os8088-720.img")
STATE_BYTES = 0x8000 + 2048             # the view's half and the record


def harness(fails):
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gbclock.py"), "--busy", "25"],
                          stdout=subprocess.DEVNULL)
    roms = [("CLOCK25", os.path.join(ROOT, "build", "gbclock", "CLOCK25.GB"))]
    d = os.environ.get("OS88_GB_ROMS")
    if d:
        roms += [("SML", os.path.join(d, "SML.GB")), ("KIRBY", os.path.join(d, "KIRBY.GB"))]
    out = os.path.join(ROOT, "build", "realdmg")
    os.makedirs(out, exist_ok=True)
    for name, rom in roms:
        for leg, il in (("reset", 0), ("kept", 1)):
            img = os.path.join(out, "gbstate-%s-%s.img" % (name, leg))
            rdgimg.make_image(rom, img, mode="state", frames=1200, ilace=il)
            text = rdgqemu.run(img, [" END\n", "RDGHOST BADOP"], 300, quiet=True)
            m = LINE.search(text)
            if not m:
                fails.append("%s/%s: no DONE line" % (name, leg))
                continue
            frames, a, b, refused = m.groups()
            print("gbstate %-8s %-5s %s frames: %s, put back %s%s"
                  % (name, leg, frames, a, b, " - REFUSED" if refused else ""))
            if refused:
                fails.append("%s/%s: rdg_stcheck refused its own state" % (name, leg))
            elif a != b:
                fails.append("%s/%s: the state put back ran to %s, not %s" % (name, leg, b, a))


def package(fails):
    import os88marty as M
    import os88ui
    import os88flush
    import gbwindow
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/gbclock.py"), "--busy", "0"],
                          stdout=subprocess.DEVNULL)
    tmp = tempfile.mkdtemp(prefix="gbstate-")
    rom = os.path.join(tmp, "CLOCK0.GB")
    with open(os.path.join(ROOT, "build", "gbclock", "CLOCK0.GB"), "rb") as i, open(rom, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/os88disk.py"), "-o", disk,
                           "--size", "720", "--folder", "SYSTEM/APPDATA",
                           os.path.join(ROOT, "build", "realdmg.o88"), rom],
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    with os88ui.boot(SYS, apps=disk, machine=MACHINE) as ui:
        m = ui.m
        ui.path("B:/CLOCK0.GB")
        seg = view = None
        for _ in range(60):
            M.guest_sleep(m, 2)
            seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view and gbwindow.frames(m, view) >= 30:
                break
        else:
            fails.append("package: the game never started")
            return
        fr = lambda: gbwindow.frames(m, view)

        def tap(k):
            m.key(k, up=False)
            M.guest_sleep(m, 0.3)
            m.key(k, down=False)

        def roundtrip(where, slot, name):
            tap("Digit%d" % slot)
            M.guest_sleep(m, 1)
            f_a = fr()
            tap("F5")
            M.guest_sleep(m, 8)                 # (the floppy's: ~35KB)
            try:
                data = os88flush.Flush(marty=m).volume(1).read(name)
            except Exception as e:              # noqa: BLE001
                fails.append("%s: %s is not on the disk (%s)" % (where, name, e))
                return
            print("gbstate package %s: F5 wrote %s, %d bytes" % (where, name, len(data)))
            if len(data) != STATE_BYTES:
                fails.append("%s: %s is %d bytes, not %d" % (where, name, len(data), STATE_BYTES))
            elif data[0x8000:0x8004] != b"RDST":
                fails.append("%s: %s's record is not at 32768" % (where, name))
            M.guest_sleep(m, 4)
            f_c = fr()
            tap("F8")
            M.guest_sleep(m, 6)
            f_d = fr()
            print("gbstate package %s: frames %d at F5, %d at F8, %d after" % (where, f_a, f_c, f_d))
            if not (f_a <= f_d < f_c):
                fails.append("%s: F8 did not take the game back to the save (%d at F5, "
                             "%d at F8, %d after)" % (where, f_a, f_c, f_d))
            tap("Digit5")                       # an empty slot: nothing
            M.guest_sleep(m, 1)
            f_e = fr()
            tap("F8")
            M.guest_sleep(m, 4)
            f_f = fr()
            print("gbstate package %s: slot 5 (empty) F8: frames %d -> %d" % (where, f_e, f_f))
            if f_f <= f_e:
                fails.append("%s: loading an empty slot moved the game (%d -> %d)" % (where, f_e, f_f))

        roundtrip("window", 3, "CLOCK0.SS3")
        ui.menu_pick("Machine", "Full Screen")
        M.guest_sleep(m, 3)
        roundtrip("full screen", 7, "CLOCK0.SS7")
        m.key("Escape", up=False)
        M.guest_sleep(m, 1)
        m.key("Escape", down=False)
        M.guest_sleep(m, 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--harness", action="store_true")
    g.add_argument("--package", action="store_true")
    a = ap.parse_args()
    fails = []
    if not a.package:
        harness(fails)
    if not a.harness:
        package(fails)
    for f in fails:
        print("FAIL gbstate: " + f)
    print("gbstate: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
