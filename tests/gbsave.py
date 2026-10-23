#!/usr/bin/env python3
"""REALDMG.O88's battery RAM and MBC3 clock, saved on close and read back.

    python3 tests/gbsave.py

docs/REALDMG-SPEC.md section 10.1 and docs/plans/REALDMG-PLAN.md wave 5's
`gbsave`. tools/gbours.py builds GBSAVE.GB (tools/gbprof/gbsave.asm, ours):
an MBC3 with the timer, 32KB of RAM in four banks and a battery. Each boot it
adds one to a counter at A000 in every bank, writes each bank's A001 with its
own number and reads all four back, and latches the clock's seconds before
and after 120 frames, leaving the results in WRAM. This

  1. boots os8088 on MartyPC with the package and the ROM on B:, opens the
     ROM, and reads WRAM out of the package's view: the counters are 00
     (FFh + 1: a cartridge never saved), the four banks are four, and the
     clock has counted the two seconds of game time;
  2. closes the window - the close negotiator writes GBSAVE.SAV beside the
     game - flushes B: and reads the file on the host: 32,768 bytes of
     RAM with each bank's counter and number in it, and VBA's 48-byte clock
     trailer;
  3. boots that floppy again and opens the ROM: the counters are 01, and the
     clock went on from where it was put away rather than from zero.

Breaking the save (rw_onclose answering without writing) leaves step 2
without a file, and breaking the banking (rdg_rammap returning at once) reads
one bank four times in step 1. Each fails naming itself.
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88flush  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402

MACHINE = "os8088_xt_vga_720b"
SYS = os.path.join(ROOT, "build", "os8088-720.img")


def results(ui, viewoff):
    """The ROM's WRAM, C000-C00F, once it has written C00F = A5h."""
    m = ui.m
    seg = view = None
    for _ in range(90):
        M.guest_sleep(m, 1)
        seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
        if seg:
            view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
        if view:
            w = m.read(view * 16 + 0xC000, 16)
            if w[15] == 0xA5:
                return w
    return None


def run(disk, viewoff):
    """One boot: open the ROM, read its results, close the window, flush B:.
    Answers (wram, the flushed image's path)."""
    out = disk + ".after"
    with os88ui.boot(SYS, apps=disk, machine=MACHINE) as ui:
        w = ui.path("B:/GBSAVE.GB")
        got = results(ui, viewoff)
        M.guest_sleep(ui.m, 1)
        ui.close(w)
        M.guest_sleep(ui.m, 3)                  # the save's disk writes
        os88flush.Flush(marty=ui.m).save(1, out)
    return got, out


def main():
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbsave"], text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="gbsave-")
    gb = os.path.join(tmp, "GBSAVE.GB")
    with open(rom, "rb") as i, open(gb, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720",
                           os.path.join(ROOT, "build", "realdmg.o88"), gb],
                          stdout=subprocess.DEVNULL)
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    fails = []

    w1, after = run(disk, viewoff)
    if w1 is None:
        print("FAIL gbsave: the ROM never finished its first boot")
        return 1
    print("gbsave boot 1: counters %s, banks %s, clock %d -> %d"
          % (w1[0:4].hex(), w1[4:8].hex(), w1[8], w1[9]))
    if list(w1[0:4]) != [0, 0, 0, 0]:
        fails.append("boot 1: counters %s, not 00 (a cartridge never saved is FFh)" % w1[0:4].hex())
    if list(w1[4:8]) != [0x50, 0x51, 0x52, 0x53]:
        fails.append("boot 1: the banks read back %s - not four banks" % w1[4:8].hex())
    if not 1 <= (w1[9] - w1[8]) % 60 <= 3:
        fails.append("boot 1: 120 frames moved the clock %d -> %d, not ~2 s" % (w1[8], w1[9]))

    vol = os88flush.Volume(open(after, "rb").read())
    try:
        sav = vol.read("GBSAVE.SAV")
    except Exception as e:                      # noqa: BLE001
        sav = None
        fails.append("the close wrote no GBSAVE.SAV beside the game (%s)" % e)
    if sav is not None:
        print("gbsave: GBSAVE.SAV %d bytes" % len(sav))
        if len(sav) != 32768 + 48:
            fails.append("GBSAVE.SAV is %d bytes, not 32,768 + 48" % len(sav))
        else:
            banks = [(sav[b * 8192], sav[b * 8192 + 1]) for b in range(4)]
            if banks != [(0, 0x50), (0, 0x51), (0, 0x52), (0, 0x53)]:
                fails.append("GBSAVE.SAV's banks hold %s" % banks)
            regs = struct.unpack_from("<10I", sav, 32768)
            stamp = struct.unpack_from("<Q", sav, 32768 + 40)[0]
            print("gbsave: clock trailer live %s latched %s stamp %d"
                  % (regs[0:5], regs[5:10], stamp))
            if regs[0] < w1[10] % 60 and regs[1] == 0:
                fails.append("the trailer's seconds %d are behind the game's %d"
                             % (regs[0], w1[10]))

    if not fails:
        w2, _ = run(after, viewoff)
        if w2 is None:
            fails.append("the ROM never finished its second boot")
        else:
            print("gbsave boot 2: counters %s, banks %s, clock %d -> %d"
                  % (w2[0:4].hex(), w2[4:8].hex(), w2[8], w2[9]))
            if list(w2[0:4]) != [1, 1, 1, 1]:
                fails.append("boot 2: counters %s, not 01 - the save was not read back"
                             % w2[0:4].hex())
            if w2[8] < w1[9]:
                fails.append("boot 2: the clock reads %d s, behind the %d it was saved past"
                             % (w2[8], w1[9]))
    for f in fails:
        print("FAIL gbsave: " + f)
    print("gbsave: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
