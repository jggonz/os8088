#!/usr/bin/env python3
"""REALDMG.O88 reading a cartridge's header before it runs it, .GBC, and the
joypad interrupt.

    python3 tests/gbheader.py

docs/REALDMG-SPEC.md section 10 and section 7. On MartyPC's VGA XT, off a
720KB disk of crafted ROMs (each CLOCK0.GB, tools/gbclock.py's, ours, with
its header changed and its checksum made right again unless the point is
that it is wrong):

  1. BADSUM.GB, the header checksum wrong: refused, "not a GB ROM";
  2. CGBONLY.GB, 0143h = C0h: refused, "needs GB Color";
  3. MBC22.GB, an MBC7 (22h), which rdgsys has not: refused, "MBC 22h
     unknown" - where it used to run as a cartridge with no MBC at all;
  4. CLOCK0.GBC, a game for both: opens from the Disk window by its
     extension, and runs;
  5. GBJOY.GB (tools/gbprof/gbjoy.asm), asleep in HALT with only the
     joypad interrupt enabled: each press of B wakes it once. Without
     rw_keys raising IF bit 4 it never wakes.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402

MACHINE = "os8088_xt_vga_720b"
SYS = os.path.join(ROOT, "build", "os8088-720.img")


def patched(src, dst, changes, fix=True):
    d = bytearray(open(src, "rb").read())
    for at, val in changes.items():
        d[at] = val
    if fix:
        x = 0
        for b in d[0x134:0x14D]:
            x = (x - b - 1) & 255
        d[0x14D] = x
    else:
        d[0x14D] ^= 0x5A
    open(dst, "wb").write(d)


def main():
    for args in (["tools/gbclock.py", "--busy", "0"], ["tools/gbours.py", "gbjoy"]):
        subprocess.check_call([sys.executable, os.path.join(ROOT, args[0])] + args[1:],
                              stdout=subprocess.DEVNULL)
    clock = os.path.join(ROOT, "build", "gbclock", "CLOCK0.GB")
    tmp = tempfile.mkdtemp(prefix="gbheader-")
    files = []
    for name, changes, fix in (("BADSUM.GB", {}, False), ("CGBONLY.GB", {0x143: 0xC0}, True),
                               ("MBC22.GB", {0x147: 0x22}, True), ("CLOCK0.GBC", {}, True)):
        files.append(os.path.join(tmp, name))
        patched(clock, files[-1], changes, fix)
    joy = os.path.join(tmp, "GBJOY.GB")
    with open(os.path.join(ROOT, "build", "gbclock", "GBJOY.GB"), "rb") as i, open(joy, "wb") as o:
        o.write(i.read())
    files.append(joy)
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/os88disk.py"), "-o", disk,
                           "--size", "720", "--folder", "SYSTEM/APPDATA",
                           os.path.join(ROOT, "build", "realdmg.o88")] + files,
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    fails = []
    with os88ui.boot(SYS, apps=disk, machine=MACHINE) as ui:
        m = ui.m

        def view():
            for _ in range(40):
                M.guest_sleep(m, 2)
                seg = gbwindow.find_seg(m, b"REALDMG", viewoff)
                if seg:
                    v = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
                    if v and gbwindow.frames(m, v) >= 30:
                        return v
            return None

        for name, says in (("BADSUM.GB", "not a GB ROM"), ("CGBONLY.GB", "needs GB Color"),
                           ("MBC22.GB", "MBC 22h unknown")):
            w = ui.path("B:/" + name)
            try:
                got = ui.wait_toast(says=says)
                print("gbheader %s: refused, %r" % (name, got))
            except Exception as e:              # noqa: BLE001
                fails.append("%s was not refused with %r (%s)" % (name, says, e))
            ui.close(w)
            M.guest_sleep(m, 2)

        w = ui.path("B:/CLOCK0.GBC")
        v = view()
        if not v:
            fails.append("CLOCK0.GBC did not run")
        else:
            print("gbheader CLOCK0.GBC: opened by its extension, frames %d" % gbwindow.frames(m, v))
        ui.close(w)
        M.guest_sleep(m, 2)

        w = ui.path("B:/GBJOY.GB")
        v = view()
        if not v:
            fails.append("GBJOY.GB did not run")
        else:
            wakes = lambda: m.read(v * 16 + 0xC000, 1)[0]
            w0 = wakes()
            for _ in range(3):
                m.key("KeyZ", up=False)
                M.guest_sleep(m, 0.5)
                m.key("KeyZ", down=False)
                M.guest_sleep(m, 0.5)
            M.guest_sleep(m, 1)
            w1 = wakes()
            print("gbheader GBJOY.GB: three presses of B, wakes %d -> %d" % (w0, w1))
            if w1 - w0 != 3:
                fails.append("GBJOY.GB woke %d times for three presses" % (w1 - w0))
    for f in fails:
        print("FAIL gbheader: " + f)
    print("gbheader: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
